#include "Core/Common.h"
#include "Game/Components.h"
#include "Editor/EditorSession.h"
#include "Game/ExecutionContext.h"
#include "Game/Scene.h"
#include "Game/Systems.h"
#include "Graphics/DescriptorAllocator.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Renderer.h"
#include "Graphics/ResourceManager.h"
#include "Player/PlayerStartup.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    class TestFailure : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            throw TestFailure(message);
        }
    }

    void CheckNear(float actual, float expected, const char* message)
    {
        if (std::fabs(actual - expected) > 1e-6f)
        {
            throw TestFailure(message);
        }
    }

    // Takes the string by reference rather than passing error.c_str() beside
    // the operation that mutates it. Function argument evaluation order is
    // intentionally irrelevant now: the reference observes the final error.
    void CheckSucceeded(bool succeeded, const std::string& error)
    {
        if (!succeeded)
        {
            throw TestFailure(error.empty() ? "operation failed without an error" : error);
        }
    }

    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        Check(bool(file), "could not open test output");
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    Entity FirstEntity(World& world)
    {
        Entity result;
        world.ForEachEntity([&](Entity entity) {
            if (!result.IsValid())
            {
                result = entity;
            }
        });
        return result;
    }

    void SetName(World& world, Entity entity, const char* text)
    {
        Name name;
        std::snprintf(name.value, Name::kCapacity, "%s", text);
        world.Add<Name>(entity, name);
    }

    World MakeCompleteWorld(ResourceManager& resources, const char* nameText)
    {
        World world;
        const Entity entity = world.Create();
        SetName(world, entity, nameText);
        world.Add<Transform>(entity,
                            { { 1.25f, -2.5f, 3.75f },
                              { 0.125f, -0.25f, 0.5f },
                              { 0.5f, 1.5f, 2.5f } });

        Material material;
        material.texture       = resources.DefaultTexture();
        material.normalTexture = resources.DefaultNormalTexture();
        material.diffuseAlbedo = { 0.1f, 0.2f, 0.3f, 0.4f };
        material.specularColor = { 0.6f, 0.7f, 0.8f };
        material.shininess     = 73.25f;
        material.normalStrength = 0.35f;
        material.blendMode = Material::BlendMode::AlphaBlend;

        MeshRenderer renderer;
        renderer.mesh     = resources.ResolveMesh(L"#cube");
        renderer.material = material;
        world.Add<MeshRenderer>(entity, renderer);
        world.Add<CameraComponent>(entity, { 0.9f, 0.2f, 450.0f });
        world.Add<Light>(entity, { Light::Type::Point, { 0.9f, 0.8f, 0.7f }, 42.0f });
        world.Add<Spin>(entity, { -1.25f });
        world.Add<ActiveCamera>(entity);

        const Entity environmentEntity = world.Create();
        SetName(world, environmentEntity, "Environment");
        Environment environment;
        environment.ambient        = { 0.11f, 0.22f, 0.33f };
        environment.shadowsEnabled = false;
        environment.shadowBias     = 0.0025f;
        environment.shadowStrength = 0.45f;
        world.Add<Environment>(environmentEntity, environment);
        return world;
    }

    std::string Snapshot(World& world, const ResourceManager& resources)
    {
        std::string snapshot;
        std::string error;
        CheckSucceeded(CaptureSceneSnapshot(world, resources, snapshot, error), error);
        return snapshot;
    }

    World ParseFixture(const std::string& fixture, ResourceManager& resources)
    {
        World world;
        std::string error;
        CheckSucceeded(RestoreSceneSnapshot(fixture, resources, world, error), error);
        return world;
    }

    class TempDirectory
    {
    public:
        TempDirectory()
            : path(std::filesystem::temp_directory_path() /
                   (L"Dx12Engine-Phase12-Tests-" + std::to_wstring(GetCurrentProcessId())))
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
            std::filesystem::create_directories(path);
        }

        ~TempDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };

    struct TestContext
    {
        RuntimePaths       paths = RuntimePaths::FromRoot(GetExecutableDir());
        GraphicsDevice      device{ GraphicsDevice::AdapterPolicy::SoftwareOnly };
        DescriptorAllocator srv{ device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                 128, true };
        ResourceManager     resources{ device, srv, paths };
        TempDirectory      temp;
    };

    void FileAndMemoryUseIdenticalBytes(TestContext& context)
    {
        World world = MakeCompleteWorld(context.resources, "Byte identical");
        const std::string memory = Snapshot(world, context.resources);
        const auto path = context.temp.path / L"identical.scene";
        std::string error;
        CheckSucceeded(SaveScene(world, context.resources, path, error), error);
        Check(ReadAll(path) == memory, "file and memory serialization differ");
    }

    void MemoryRoundTripIsStable(TestContext& context)
    {
        World source = MakeCompleteWorld(context.resources, "Round trip");
        const std::string before = Snapshot(source, context.resources);
        World restored;
        std::string error;
        CheckSucceeded(RestoreSceneSnapshot(before, context.resources, restored, error), error);
        Check(Snapshot(restored, context.resources) == before,
              "memory round trip changed serialized bytes");
    }

    void DiskMemoryDiskRoundTripIsStable(TestContext& context)
    {
        World source = MakeCompleteWorld(context.resources, "Disk memory disk");
        const auto beforePath = context.temp.path / L"roundtrip-before.scene";
        const auto afterPath  = context.temp.path / L"roundtrip-after.scene";
        std::string error;
        CheckSucceeded(SaveScene(source, context.resources, beforePath, error), error);

        const std::string snapshot = Snapshot(source, context.resources);
        World restored;
        CheckSucceeded(RestoreSceneSnapshot(snapshot, context.resources, restored, error), error);
        CheckSucceeded(SaveScene(restored, context.resources, afterPath, error), error);

        Check(ReadAll(beforePath) == ReadAll(afterPath),
              "disk-memory-disk round trip changed serialized bytes");
    }

    void InvalidMemoryKeepsLiveWorld(TestContext& context)
    {
        World live = MakeCompleteWorld(context.resources, "Must survive");
        const std::string before = Snapshot(live, context.resources);
        std::string error;
        Check(!RestoreSceneSnapshot("scene 5\nentity\n  transform 1 2\n",
                                    context.resources, live, error),
              "malformed snapshot unexpectedly loaded");
        Check(!error.empty(), "malformed snapshot did not report an error");
        Check(Snapshot(live, context.resources) == before,
              "failed snapshot restore mutated the live World");
    }

    void AtomicSavePreservesLastGoodFile(TestContext& context)
    {
        const auto path = context.temp.path / L"replace.scene";
        World first = MakeCompleteWorld(context.resources, "First");
        World second = MakeCompleteWorld(context.resources, "Second");
        std::string error;
        CheckSucceeded(SaveScene(first, context.resources, path, error), error);
        CheckSucceeded(SaveScene(second, context.resources, path, error), error);
        const std::string lastGood = ReadAll(path);
        Check(lastGood == Snapshot(second, context.resources),
              "replacement did not publish the second scene");

        World invalid;
        const Entity entity = invalid.Create();
        MeshRenderer renderer;
        renderer.mesh = MeshHandle{ 0x00FFFFFFu };
        invalid.Add<MeshRenderer>(entity, renderer);
        Check(!SaveScene(invalid, context.resources, path, error),
              "unnamed valid asset handle unexpectedly serialized");
        Check(ReadAll(path) == lastGood, "failed save damaged the last good file");
        std::filesystem::path tempPath = path;
        tempPath += L".tmp";
        Check(!std::filesystem::exists(tempPath), "failed save left a sibling .tmp file");
    }

    void EditorSessionResetPolicy()
    {
        EditorSession session;
        session.selected = Entity{ 7, 3 };
        session.scenePath = L"Assets/Scenes/Keep.scene";
        session.sceneStatus = "keep status";
        session.runStatus = "clear run status";
        session.openSaveAs = true;
        session.saveAsName[0] = 'X';
        EditorCommand command;
        command.kind = EditorCommand::Kind::Destroy;
        command.target = session.selected;
        session.commands.push_back(command);

        session.OnWorldReplaced();
        Check(!session.selected.IsValid(), "selection survived World replacement");
        Check(session.commands.empty(), "deferred commands survived World replacement");
        Check(!session.openSaveAs, "popup request survived World replacement");
        Check(std::string(session.saveAsName.data()) == "MyScene",
              "popup input was not reset");
        Check(session.scenePath == L"Assets/Scenes/Keep.scene",
              "scene path did not survive replacement");
        Check(session.sceneStatus == "keep status",
              "scene status did not survive replacement");
        Check(session.runStatus.empty(), "run status survived World replacement");
    }

    void InputContextsRespectHostBoundaries()
    {
        FrameContext host;
        host.deltaSeconds = 0.25f;
        host.renderAspect = 16.0f / 9.0f;
        host.input.mouseDeltaX = 7.0f;
        host.input.mouseDeltaY = -3.0f;
        host.input.keyDown['W'] = true;
        host.input.keyPressed['V'] = true;

        const FrameContext blocked = MakeEditorFrameContext(host, false, true);
        CheckNear(blocked.input.mouseDeltaX, 0.0f, "editor accepted mouse off viewport");
        CheckNear(blocked.input.mouseDeltaY, 0.0f, "editor accepted mouse off viewport");
        Check(!blocked.input.IsDown('W'), "editor ignored ImGui keyboard capture");
        Check(!blocked.input.WasPressed('V'), "captured edge leaked into editor frame");

        const FrameContext editor = MakeEditorFrameContext(host, true, false);
        CheckNear(editor.input.mouseDeltaX, 7.0f, "editor lost viewport mouse input");
        Check(editor.input.IsDown('W'), "editor lost held keyboard state");
        Check(editor.input.WasPressed('V'), "editor lost keyboard edge");

        const FrameContext player = MakePlayerFrameContext(host);
        CheckNear(player.input.mouseDeltaX, 7.0f, "Player input was viewport-masked");
        Check(player.input.IsDown('W'), "Player input was ImGui-masked");
    }

    void PlayerStartupArguments()
    {
        RuntimePaths paths = RuntimePaths::FromRoot(L"C:\\PlayerPackage");
        PlayerStartup startup;
        std::wstring error;

        const wchar_t* defaults[] = { L"Player.exe" };
        Check(ParsePlayerStartup(1, defaults, paths, startup, error),
              "default Player arguments failed");
        Check(startup.scenePath ==
                  (paths.root / L"Assets/Scenes/Demo.scene").lexically_normal(),
              "default Player Scene was not rooted beside the executable");

        const wchar_t* relative[] = {
            L"Player.exe", L"--scene", L"Assets/Scenes/ShadowA.scene"
        };
        Check(ParsePlayerStartup(3, relative, paths, startup, error),
              "relative --scene failed");
        Check(startup.scenePath ==
                  (paths.root / L"Assets/Scenes/ShadowA.scene").lexically_normal(),
              "relative --scene used the working directory");

        const wchar_t* absolute[] = {
            L"Player.exe", L"--scene", L"D:\\Scenes\\Custom.scene"
        };
        Check(ParsePlayerStartup(3, absolute, paths, startup, error),
              "absolute --scene failed");
        Check(startup.scenePath == L"D:\\Scenes\\Custom.scene",
              "absolute --scene was unexpectedly re-rooted");

        const wchar_t* unknown[] = { L"Player.exe", L"--wat" };
        Check(!ParsePlayerStartup(2, unknown, paths, startup, error) &&
                  error.find(L"unknown Player option") != std::wstring::npos,
              "unknown Player option lacked a clear error");

        const wchar_t* missing[] = { L"Player.exe", L"--scene" };
        Check(!ParsePlayerStartup(2, missing, paths, startup, error) &&
                  error.find(L"requires a path") != std::wstring::npos,
              "missing --scene value lacked a clear error");
    }

    void EditorCameraDoesNotMutateSceneCamera()
    {
        World world;
        const Entity cameraEntity = world.Create();
        Transform gameTransform;
        gameTransform.position = { 2.0f, 3.0f, -4.0f };
        gameTransform.rotation = { 0.1f, 0.2f, 0.0f };
        world.Add<Transform>(cameraEntity, gameTransform);
        world.Add<CameraComponent>(cameraEntity, { 0.8f, 0.2f, 300.0f });
        world.Add<ActiveCamera>(cameraEntity);

        EditorCamera editorCamera;
        Check(InitializeEditorCamera(world, editorCamera),
              "editor camera did not initialize from ActiveCamera");

        FrameContext frame;
        frame.deltaSeconds = 0.5f;
        frame.input.mouseDeltaX = 20.0f;
        frame.input.keyDown['W'] = true;
        RunEditorSystems(editorCamera, frame);

        const Transform* unchanged = world.Get<Transform>(cameraEntity);
        Check(unchanged != nullptr, "game camera disappeared");
        CheckNear(unchanged->position.x, gameTransform.position.x,
                  "editor camera movement changed scene camera X");
        CheckNear(unchanged->position.y, gameTransform.position.y,
                  "editor camera movement changed scene camera Y");
        CheckNear(unchanged->position.z, gameTransform.position.z,
                  "editor camera movement changed scene camera Z");
        CheckNear(unchanged->rotation.y, gameTransform.rotation.y,
                  "editor mouse look changed scene camera rotation");
        Check(std::fabs(editorCamera.transform.position.z - gameTransform.position.z) > 0.1f,
              "editor camera did not move independently");

        const CameraView view = GetEditorCameraView(editorCamera);
        CheckNear(view.fovY, 0.8f, "editor camera lost copied lens");
        CheckNear(view.nearZ, 0.2f, "editor camera lost copied near plane");
        CheckNear(view.farZ, 300.0f, "editor camera lost copied far plane");
    }

    void PlaySystemsUseSessionTimeAndInput()
    {
        World world;

        const Entity camera = world.Create();
        world.Add<Transform>(camera);
        world.Add<CameraComponent>(camera);
        world.Add<ActiveCamera>(camera);

        const Entity spinner = world.Create();
        world.Add<Transform>(spinner);
        world.Add<Spin>(spinner, { 2.0f });

        const Entity orbit = world.Create();
        world.Add<Transform>(orbit);
        world.Add<Light>(orbit, { Light::Type::Point, { 1.0f, 1.0f, 1.0f }, 30.0f });
        world.Add<Spin>(orbit, { 0.5f });

        FrameContext frame;
        frame.deltaSeconds = 0.5f;
        frame.input.keyDown['W'] = true;

        PlaySession play;
        play.Begin();
        play.BeginFrame(frame);
        RunPlaySystems(world, play);
        play.EndFrame();

        CheckNear(world.Get<Transform>(camera)->position.z, 4.0f,
                  "Play camera did not consume flattened held input");
        CheckNear(world.Get<Transform>(spinner)->rotation.y, 1.0f,
                  "Spin did not run in Play group");
        CheckNear(world.Get<Transform>(orbit)->position.x, 14.0f,
                  "orbit did not start at play elapsed zero");
        CheckNear(world.Get<Transform>(orbit)->position.z, 0.0f,
                  "orbit start depended on Engine total time");
        CheckNear(play.ElapsedSeconds(), 0.5f, "play clock did not advance by frame dt");

        play.End();
        CheckNear(play.ElapsedSeconds(), 0.0f, "play clock survived Stop");
        Check(!play.Input().IsDown('W'), "held input survived Stop");

        // Waiting in Edit means no BeginFrame/EndFrame calls. A new Play
        // starts the absolute orbit system from the exact same point.
        play.Begin();
        play.BeginFrame(frame);
        RunPlaySystems(world, play);
        CheckNear(world.Get<Transform>(orbit)->position.x, 14.0f,
                  "re-Play orbit did not restart from the same point");
        CheckNear(world.Get<Transform>(orbit)->position.z, 0.0f,
                  "Edit wait leaked into play elapsed time");
        play.EndFrame();
    }

    void PlayStopRestoresExactEditWorld(TestContext& context)
    {
        World world = MakeCompleteWorld(context.resources, "Rollback");
        const std::string before = Snapshot(world, context.resources);
        const Entity original = FirstEntity(world);
        const MeshHandle originalMesh = world.Get<MeshRenderer>(original)->mesh;

        EditorSession editor;
        editor.scenePath = L"Assets/Scenes/Keep.scene";
        editor.sceneStatus = "saved Keep.scene";
        editor.selected = original;
        PlaySession play;

        Check(editor.EnterPlay(world, context.resources, play),
              "valid World could not enter Play");
        Check(editor.runMode == RunMode::Play, "EnterPlay did not change the mode");
        Check(play.IsActive(), "EnterPlay did not begin the PlaySession");
        Check(editor.HasPlaySnapshot(), "EnterPlay did not retain a rollback snapshot");
        Check(editor.sceneStatus == "saved Keep.scene",
              "EnterPlay overwrote file status");

        world.Get<Transform>(original)->position = { 99.0f, 88.0f, 77.0f };
        world.Destroy(original);
        const Entity runtimeOnly = world.Create();
        SetName(world, runtimeOnly, "Runtime only");
        world.Add<Transform>(runtimeOnly, { { -9.0f, -8.0f, -7.0f } });

        editor.selected = runtimeOnly;
        EditorCommand deferred;
        deferred.kind = EditorCommand::Kind::Destroy;
        deferred.target = runtimeOnly;
        editor.commands.push_back(deferred);

        FrameContext frame;
        frame.deltaSeconds = 0.5f;
        frame.input.keyDown['W'] = true;
        play.BeginFrame(frame);
        play.EndFrame();

        Check(editor.StopPlay(world, context.resources, play),
              "valid snapshot could not stop Play");
        Check(editor.runMode == RunMode::Edit, "StopPlay did not return to Edit");
        Check(!play.IsActive(), "StopPlay left the PlaySession active");
        CheckNear(play.ElapsedSeconds(), 0.0f, "StopPlay did not reset play time");
        Check(!editor.HasPlaySnapshot(), "successful Stop retained its snapshot");
        Check(Snapshot(world, context.resources) == before,
              "StopPlay did not restore the exact Edit World bytes");
        Check(!editor.selected.IsValid(), "Play selection survived rollback");
        Check(editor.commands.empty(), "queued Play edits survived rollback");
        Check(editor.scenePath == L"Assets/Scenes/Keep.scene",
              "StopPlay changed scene path identity");
        Check(editor.sceneStatus == "saved Keep.scene",
              "StopPlay overwrote file status");
        Check(world.Get<MeshRenderer>(FirstEntity(world))->mesh.index == originalMesh.index,
              "StopPlay did not resolve the mesh through the shared resource cache");
    }

    void DemoSceneAndSpinInteraction(TestContext& context)
    {
        World world;
        std::string error;
        CheckSucceeded(LoadScene(context.paths.SceneDir() / L"Demo.scene",
                                 context.resources, world, error), error);

        CameraView camera;
        Check(GetActiveCameraView(world, camera),
              "Demo.scene has no usable ActiveCamera");

        Entity interactive;
        int alphaMeshes = 0;
        int normalMeshes = 0;
        bool hasSkybox = false;
        bool hasShadows = false;
        world.ForEach<Spin>([&](Entity entity, Spin&) {
            const Transform* transform = world.Get<Transform>(entity);
            if (transform && transform->position.z < 10.0f)
                interactive = entity;
        });
        world.ForEach<MeshRenderer>([&](Entity, MeshRenderer& renderer) {
            if (renderer.material.blendMode == Material::BlendMode::AlphaBlend)
                ++alphaMeshes;
            if (renderer.material.normalTexture.IsValid())
                ++normalMeshes;
        });
        world.ForEach<Environment>([&](Entity, Environment& environment) {
            hasSkybox |= environment.skybox.IsValid();
            hasShadows |= environment.shadowsEnabled;
        });
        Check(interactive.IsValid(), "Demo.scene has no interactive Spin target");
        Check(alphaMeshes > 0, "Demo.scene does not cover the transparent pass");
        Check(normalMeshes > 0, "Demo.scene does not cover normal mapping");
        Check(hasSkybox, "Demo.scene does not cover the skybox pass");
        Check(hasShadows, "Demo.scene does not enable shadows");

        const MeshRenderer targetRenderer = *world.Get<MeshRenderer>(interactive);
        const Entity farSpinner = world.Create();
        Transform farTransform;
        farTransform.position = { 0.0f, 2.0f, 8.0f };
        world.Add<Transform>(farSpinner, farTransform);
        world.Add<MeshRenderer>(farSpinner, targetRenderer);
        world.Add<Spin>(farSpinner, { 0.0f });

        InputContext input;
        input.keyPressed['E'] = true;
        Check(InteractSpinSystem(world, context.resources, input),
              "E did not hit the centered Spin target");
        Check(world.Get<Spin>(interactive)->speed > 0.0f,
              "E did not start the Spin target");
        Check(InteractSpinSystem(world, context.resources, input),
              "second E edge did not hit the same Spin target");
        CheckNear(world.Get<Spin>(interactive)->speed, 0.0f,
                  "second E edge did not stop the Spin target");

        input.keyPressed['E'] = false;
        Check(!InteractSpinSystem(world, context.resources, input),
              "held/non-edge E toggled interaction");

        world.Destroy(interactive);
        input.keyPressed['E'] = true;
        Check(!InteractSpinSystem(world, context.resources, input),
              "interaction ignored its maximum distance");
        CheckNear(world.Get<Spin>(farSpinner)->speed, 0.0f,
                  "out-of-range Spin target was toggled");
    }

    void FailedPlayCaptureIsAtomic(TestContext& context)
    {
        World world;
        const Entity entity = world.Create();
        MeshRenderer invalid;
        invalid.mesh = MeshHandle{ 0x00FFFFFFu };
        world.Add<MeshRenderer>(entity, invalid);

        EditorSession editor;
        PlaySession play;
        Check(!editor.EnterPlay(world, context.resources, play),
              "invalid World unexpectedly entered Play");
        Check(editor.runMode == RunMode::Edit,
              "failed snapshot capture changed the mode");
        Check(!play.IsActive(), "failed snapshot capture began PlaySession");
        Check(!editor.HasPlaySnapshot(), "failed snapshot capture published bytes");
        Check(world.IsAlive(entity), "failed snapshot capture replaced the World");
        Check(world.Get<MeshRenderer>(entity)->mesh.index == invalid.mesh.index,
              "failed snapshot capture mutated a component");
        Check(editor.runStatus.find("Play failed:") == 0,
              "failed snapshot capture did not expose an error");
    }

    void RepeatedPlayStopIsStable(TestContext& context)
    {
        World world = MakeCompleteWorld(context.resources, "Repeat");
        const std::string expected = Snapshot(world, context.resources);
        EditorSession editor;
        PlaySession play;

        for (int cycle = 0; cycle < 100; ++cycle)
        {
            Check(editor.EnterPlay(world, context.resources, play),
                  "repeated EnterPlay failed");
            const Entity entity = FirstEntity(world);
            world.Get<Transform>(entity)->rotation.y += float(cycle + 1);
            const Entity runtimeOnly = world.Create();
            world.Add<Transform>(runtimeOnly);
            Check(editor.StopPlay(world, context.resources, play),
                  "repeated StopPlay failed");
            Check(Snapshot(world, context.resources) == expected,
                  "repeated Play/Stop drifted scene bytes");
        }
    }

    void Version1Defaults(TestContext& context)
    {
        const std::string fixture =
            "scene 1\nentity\n"
            "  meshrenderer mesh \"#cube\" texture \"#white\" "
            "albedo 1 0.5 0.25 1 specular 0.1 0.2 0.3 shininess 8\n"
            "  environment 0.2 0.3 0.4\n";
        World world = ParseFixture(fixture, context.resources);
        const Entity entity = FirstEntity(world);
        MeshRenderer* renderer = world.Get<MeshRenderer>(entity);
        Environment* environment = world.Get<Environment>(entity);
        Check(renderer != nullptr && environment != nullptr, "v1 components missing");
        Check(!renderer->material.normalTexture.IsValid(), "v1 gained a normal map");
        Check(renderer->material.blendMode == Material::BlendMode::Opaque,
              "v1 material did not default to opaque");
        Check(!environment->skybox.IsValid(), "v1 environment gained a skybox");
        Check(environment->shadowsEnabled, "v1 shadows default changed");
        CheckNear(environment->shadowBias, 0.001f, "v1 shadow bias default changed");
        CheckNear(environment->shadowStrength, 1.0f,
                  "v1 shadow strength default changed");
    }

    void Version2Skybox(TestContext& context)
    {
        World world = ParseFixture(
            "scene 2\nentity\n  environment 0.2 0.3 0.4 skybox \"Test\"\n",
            context.resources);
        Environment* environment = world.Get<Environment>(FirstEntity(world));
        Check(environment && environment->skybox.IsValid(), "v2 skybox was not restored");
        Check(environment->shadowsEnabled, "v2 shadow default changed");
    }

    void Version3NormalMap(TestContext& context)
    {
        World world = ParseFixture(
            "scene 3\nentity\n"
            "  meshrenderer mesh \"#cube\" texture \"#white\" "
            "albedo 1 1 1 1 specular 1 1 1 shininess 32 "
            "normal \"TestNormal.png\" strength 0.25\n",
            context.resources);
        MeshRenderer* renderer = world.Get<MeshRenderer>(FirstEntity(world));
        Check(renderer && renderer->material.normalTexture.IsValid(),
              "v3 normal map was not restored");
        CheckNear(renderer->material.normalStrength, 0.25f,
                  "v3 normal strength changed");
        Check(renderer->material.blendMode == Material::BlendMode::Opaque,
              "v3 material did not default to opaque");
    }

    void Version4ShadowControls(TestContext& context)
    {
        World world = ParseFixture(
            "scene 4\nentity\n  environment 0.2 0.3 0.4 shadow 0 0.002 0.4\n",
            context.resources);
        Environment* environment = world.Get<Environment>(FirstEntity(world));
        Check(environment && !environment->shadowsEnabled,
              "v4 shadow enabled flag changed");
        CheckNear(environment->shadowBias, 0.002f, "v4 shadow bias changed");
        CheckNear(environment->shadowStrength, 0.4f, "v4 shadow strength changed");
    }

    void Version5BlendMode(TestContext& context)
    {
        World world = ParseFixture(
            "scene 5\nentity\n"
            "  meshrenderer mesh \"#cube\" texture \"#white\" "
            "albedo 1 1 1 0.5 specular 1 1 1 shininess 64 blend alpha\n",
            context.resources);
        MeshRenderer* renderer = world.Get<MeshRenderer>(FirstEntity(world));
        Check(renderer && renderer->material.blendMode == Material::BlendMode::AlphaBlend,
              "v5 alpha blend mode was not restored");
    }

    void ExistingFixturesStillRoundTrip(TestContext& context)
    {
        for (const wchar_t* fileName : { L"LegacyV2.scene", L"ShadowA.scene" })
        {
            World world;
            std::string error;
            const auto path = context.resources.Paths().SceneDir() / fileName;
            CheckSucceeded(LoadScene(path, context.resources, world, error), error);
            const std::string first = Snapshot(world, context.resources);
            World restored;
            CheckSucceeded(RestoreSceneSnapshot(first, context.resources, restored, error),
                           error);
            Check(Snapshot(restored, context.resources) == first,
                  "existing fixture changed after memory round trip");
        }
    }

    void FutureVersionAndBomPolicy(TestContext& context)
    {
        World live = MakeCompleteWorld(context.resources, "Future guard");
        const std::string before = Snapshot(live, context.resources);
        std::string error;
        Check(!RestoreSceneSnapshot("scene 6\n", context.resources, live, error),
              "future scene version unexpectedly loaded");
        Check(Snapshot(live, context.resources) == before,
              "future version rejection mutated the live World");

        const std::string bomFixture =
            std::string("\xEF\xBB\xBF") + "scene 1\r\nentity\r\n  name \"BOM\"\r\n";
        World bomWorld = ParseFixture(bomFixture, context.resources);
        Name* name = bomWorld.Get<Name>(FirstEntity(bomWorld));
        Check(name && std::string(name->value) == "BOM", "UTF-8 BOM/CRLF policy regressed");
    }

    class CommaDecimalPoint : public std::numpunct<char>
    {
    protected:
        char do_decimal_point() const override { return ','; }
    };

    void SerializationForcesClassicLocale(TestContext& context)
    {
        World world = MakeCompleteWorld(context.resources, "Locale");
        std::ostringstream output;
        output.imbue(std::locale(std::locale::classic(), new CommaDecimalPoint));
        std::string error;
        CheckSucceeded(SerializeScene(output, world, context.resources, error), error);
        Check(output.str().find("1.25") != std::string::npos,
              "serializer inherited a decimal-comma locale");

        std::istringstream input(output.str());
        input.imbue(std::locale(std::locale::classic(), new CommaDecimalPoint));
        World restored;
        CheckSucceeded(DeserializeScene(input, context.resources, restored, error), error);
        Transform* transform = restored.Get<Transform>(FirstEntity(restored));
        Check(transform != nullptr, "locale test transform missing");
        CheckNear(transform->position.x, 1.25f, "parser inherited a decimal-comma locale");
    }

    void D3D12DebugLayerStaysClean(TestContext& context)
    {
        if (context.device.HasDebugLayer())
        {
            Check(context.device.DebugMessageCount() == 0,
                  "D3D12 debug layer recorded a message during the test suite");
        }
    }

    void RuntimePathsAndCompiledShaderCache(TestContext& context)
    {
        const std::filesystem::path executableDir = GetExecutableDir();
        Check(!executableDir.empty(), "executable directory was empty");
        Check(context.paths.root == std::filesystem::absolute(executableDir),
              "runtime root is not the executable directory");
        Check(context.paths.assetDir == context.paths.root / L"Assets",
              "asset directory is not rooted beside the executable");
        Check(context.paths.shaderDir == context.paths.root / L"Shaders",
              "shader directory is not rooted beside the executable");

        const ResourceManager::Stats before = context.resources.GetStats();
        const ShaderBytecode& first = context.resources.LoadShader(L"Basic.VS.cso");
        const ShaderBytecode& again = context.resources.LoadShader(L"Basic.VS.cso");
        Check(first.Size() > 0, "compiled shader bytecode was empty");
        Check(&first == &again, "compiled shader cache returned two objects");
        Check(context.resources.GetStats().shaderLoads == before.shaderLoads + 1,
              "compiled shader was read more than once");
        Check(context.resources.GetStats().shaderRequests == before.shaderRequests + 2,
              "compiled shader requests were not counted");

        try
        {
            context.resources.LoadShader(L"Missing.Phase12.5.cso");
            throw std::runtime_error("missing compiled shader unexpectedly loaded");
        }
        catch (const std::runtime_error& error)
        {
            const std::string message = error.what();
            Check(message.find("Missing.Phase12.5.cso") != std::string::npos,
                  "missing shader error omitted the logical file name");
            Check(message.find("Runtime root:") != std::string::npos,
                  "missing shader error omitted the runtime root");
        }
    }

    void PresentationPathsStayClean(TestContext&)
    {
        constexpr wchar_t className[] = L"Dx12EnginePhase12PresentationTest";
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        const ATOM atom = RegisterClassExW(&windowClass);
        Check(atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
              "could not register presentation test window");

        HWND hwnd = CreateWindowExW(0, className, L"Phase 12.3 Test",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                    320, 200, nullptr, nullptr,
                                    windowClass.hInstance, nullptr);
        Check(hwnd != nullptr, "could not create presentation test window");

        try
        {
            Renderer renderer(hwnd, 320, 200,
                              RuntimePaths::FromRoot(GetExecutableDir()),
                              GraphicsDevice::AdapterPolicy::SoftwareOnly);
            CameraView camera;
            LightingData lighting;
            std::vector<DrawItem> items;

            renderer.SetMsaaEnabled(false);
            renderer.RenderFrame(camera, lighting, items,
                                 Renderer::SceneOutput::OffscreenTexture);
            renderer.RenderFrame(camera, lighting, items,
                                 Renderer::SceneOutput::SwapChain);

            if (renderer.Is4xMsaaSupported())
            {
                renderer.SetMsaaEnabled(true);
                renderer.RenderFrame(camera, lighting, items,
                                     Renderer::SceneOutput::OffscreenTexture);
                renderer.RenderFrame(camera, lighting, items,
                                     Renderer::SceneOutput::SwapChain);
            }

            renderer.Resize(400, 240);
            renderer.RenderFrame(camera, lighting, items,
                                 Renderer::SceneOutput::SwapChain);
            if (renderer.HasDebugLayer())
            {
                Check(renderer.DebugMessageCount() == 0,
                      "presentation paths recorded a D3D12 debug message");
            }
        }
        catch (...)
        {
            DestroyWindow(hwnd);
            UnregisterClassW(className, windowClass.hInstance);
            throw;
        }

        DestroyWindow(hwnd);
        UnregisterClassW(className, windowClass.hInstance);
    }

    struct TestCase
    {
        const char* name;
        void (*run)(TestContext&);
    };

    bool RunCpuTests(int& passed, int& total)
    {
        struct CpuTestCase
        {
            const char* name;
            void (*run)();
        };
        const CpuTestCase tests[] = {
            { "functional/editor-session-reset", EditorSessionResetPolicy },
            { "functional/input-host-boundaries", InputContextsRespectHostBoundaries },
            { "functional/editor-camera-isolation", EditorCameraDoesNotMutateSceneCamera },
            { "functional/play-system-context", PlaySystemsUseSessionTimeAndInput },
            { "functional/player-startup-arguments", PlayerStartupArguments },
        };

        bool allPassed = true;
        for (const CpuTestCase& test : tests)
        {
            ++total;
            try
            {
                test.run();
                ++passed;
                std::cout << "[PASS] " << test.name << '\n';
            }
            catch (const std::exception& e)
            {
                allPassed = false;
                std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
            }
        }
        return allPassed;
    }
}

int main()
{
    int result = 0;
    int passed = 0;
    int total  = 0;
    RunCpuTests(passed, total);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult))
    {
        std::cerr << "COM initialization failed: 0x" << std::hex
                  << static_cast<unsigned>(comResult) << '\n';
        std::cerr << passed << '/' << total << " CPU tests passed before COM failure\n";
        return 2;
    }

    try
    {
        TestContext context;
        const TestCase tests[] = {
            { "functional/play-stop-rollback", PlayStopRestoresExactEditWorld },
            { "functional/demo-scene-interaction", DemoSceneAndSpinInteraction },
            { "functional/play-capture-failure", FailedPlayCaptureIsAtomic },
            { "regression/repeated-play-stop", RepeatedPlayStopIsStable },
            { "functional/file-memory-identical", FileAndMemoryUseIdenticalBytes },
            { "functional/memory-round-trip", MemoryRoundTripIsStable },
            { "functional/disk-memory-disk-round-trip", DiskMemoryDiskRoundTripIsStable },
            { "functional/invalid-memory-is-transactional", InvalidMemoryKeepsLiveWorld },
            { "functional/atomic-save", AtomicSavePreservesLastGoodFile },
            { "regression/scene-v1-defaults", Version1Defaults },
            { "regression/scene-v2-skybox", Version2Skybox },
            { "regression/scene-v3-normal-map", Version3NormalMap },
            { "regression/scene-v4-shadow", Version4ShadowControls },
            { "regression/scene-v5-blend", Version5BlendMode },
            { "regression/existing-fixtures", ExistingFixturesStillRoundTrip },
            { "regression/future-version-and-bom", FutureVersionAndBomPolicy },
            { "regression/classic-locale", SerializationForcesClassicLocale },
            { "functional/presentation-paths", PresentationPathsStayClean },
            { "regression/d3d12-debug-layer-clean", D3D12DebugLayerStaysClean },
            { "functional/runtime-paths-and-compiled-shaders",
              RuntimePathsAndCompiledShaderCache },
        };

        for (const TestCase& test : tests)
        {
            ++total;
            try
            {
                test.run(context);
                ++passed;
                std::cout << "[PASS] " << test.name << '\n';
            }
            catch (const std::exception& e)
            {
                std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
            }
        }

        std::cout << passed << '/' << total << " tests passed\n";
        result = passed == total ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "test setup failed: " << e.what() << '\n';
        std::cerr << passed << '/' << total << " CPU tests passed before setup failure\n";
        result = 2;
    }

    CoUninitialize();
    return result;
}
