#include "Core/Common.h"
#include "Game/Components.h"
#include "Game/EditorSession.h"
#include "Game/Scene.h"
#include "Graphics/DescriptorAllocator.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/ResourceManager.h"

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
        Check(CaptureSceneSnapshot(world, resources, snapshot, error), error.c_str());
        return snapshot;
    }

    World ParseFixture(const std::string& fixture, ResourceManager& resources)
    {
        World world;
        std::string error;
        Check(RestoreSceneSnapshot(fixture, resources, world, error), error.c_str());
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
        GraphicsDevice      device;
        DescriptorAllocator srv{ device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                 128, true };
        ResourceManager     resources{ device, srv };
        TempDirectory      temp;
    };

    void FileAndMemoryUseIdenticalBytes(TestContext& context)
    {
        World world = MakeCompleteWorld(context.resources, "Byte identical");
        const std::string memory = Snapshot(world, context.resources);
        const auto path = context.temp.path / L"identical.scene";
        std::string error;
        Check(SaveScene(world, context.resources, path, error), error.c_str());
        Check(ReadAll(path) == memory, "file and memory serialization differ");
    }

    void MemoryRoundTripIsStable(TestContext& context)
    {
        World source = MakeCompleteWorld(context.resources, "Round trip");
        const std::string before = Snapshot(source, context.resources);
        World restored;
        std::string error;
        Check(RestoreSceneSnapshot(before, context.resources, restored, error), error.c_str());
        Check(Snapshot(restored, context.resources) == before,
              "memory round trip changed serialized bytes");
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
        Check(SaveScene(first, context.resources, path, error), error.c_str());
        Check(SaveScene(second, context.resources, path, error), error.c_str());
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

    void EditorSessionResetPolicy(TestContext&)
    {
        EditorSession session;
        session.selected = Entity{ 7, 3 };
        session.scenePath = L"Assets/Scenes/Keep.scene";
        session.sceneStatus = "keep status";
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
            const auto path = GetSceneDir() / fileName;
            Check(LoadScene(path, context.resources, world, error), error.c_str());
            const std::string first = Snapshot(world, context.resources);
            World restored;
            Check(RestoreSceneSnapshot(first, context.resources, restored, error),
                  error.c_str());
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
        Check(SerializeScene(output, world, context.resources, error), error.c_str());
        Check(output.str().find("1.25") != std::string::npos,
              "serializer inherited a decimal-comma locale");

        std::istringstream input(output.str());
        input.imbue(std::locale(std::locale::classic(), new CommaDecimalPoint));
        World restored;
        Check(DeserializeScene(input, context.resources, restored, error), error.c_str());
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

    struct TestCase
    {
        const char* name;
        void (*run)(TestContext&);
    };
}

int main()
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult))
    {
        std::cerr << "COM initialization failed: 0x" << std::hex
                  << static_cast<unsigned>(comResult) << '\n';
        return 2;
    }

    int result = 0;
    try
    {
        TestContext context;
        const TestCase tests[] = {
            { "functional/file-memory-identical", FileAndMemoryUseIdenticalBytes },
            { "functional/memory-round-trip", MemoryRoundTripIsStable },
            { "functional/invalid-memory-is-transactional", InvalidMemoryKeepsLiveWorld },
            { "functional/atomic-save", AtomicSavePreservesLastGoodFile },
            { "functional/editor-session-reset", EditorSessionResetPolicy },
            { "regression/scene-v1-defaults", Version1Defaults },
            { "regression/scene-v2-skybox", Version2Skybox },
            { "regression/scene-v3-normal-map", Version3NormalMap },
            { "regression/scene-v4-shadow", Version4ShadowControls },
            { "regression/scene-v5-blend", Version5BlendMode },
            { "regression/existing-fixtures", ExistingFixturesStillRoundTrip },
            { "regression/future-version-and-bom", FutureVersionAndBomPolicy },
            { "regression/classic-locale", SerializationForcesClassicLocale },
            { "regression/d3d12-debug-layer-clean", D3D12DebugLayerStaysClean },
        };

        int passed = 0;
        for (const TestCase& test : tests)
        {
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

        std::cout << passed << '/' << std::size(tests) << " tests passed\n";
        result = passed == std::size(tests) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "test setup failed: " << e.what() << '\n';
        result = 2;
    }

    CoUninitialize();
    return result;
}
