// EditorSession.h : transient editor state that follows a World's lifetime.
#pragma once

#include "Core/World.h"
#include "Game/ExecutionContext.h"
#include "Graphics/Handles.h"

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

class ResourceManager;

enum class EditorComponent
{
    Name,
    Transform,
    MeshRenderer,
    Camera,
    Light,
    Spin,
    ActiveCamera,
    Environment,
    Count
};

struct EditorCommand
{
    enum class Kind
    {
        Create,
        Destroy,
        Duplicate,
        AddComponent,
        RemoveComponent,
        Place,
        NewScene,
        LoadScene
    };

    Kind            kind = Kind::Create;
    Entity          target;
    EditorComponent component = EditorComponent::Count;

    // Place commands carry the placement preview's resolved values until the
    // panels finish iterating and the structural edit can be applied safely.
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    MeshHandle        mesh;
    TextureHandle     texture;
    std::string       name;
};

class EditorSession
{
public:
    // Execution mode belongs to the editor host and is never serialized.
    RunMode runMode = RunMode::Edit;

    // Invalidated whenever the World is replaced.
    Entity                     selected;
    std::vector<EditorCommand> commands;

    // File identity belongs to the editor session, not the scene data. The
    // replacement boundary deliberately preserves these two fields; New/Open
    // then applies its own path/status policy after a successful replacement.
    std::filesystem::path scenePath;
    std::string           sceneStatus;

    // Feedback for the Play transaction is separate from file status: a
    // Play/Stop cycle must not erase which scene was opened or last saved.
    std::string runStatus;

    // Popup input is transient and must not leak across a World epoch.
    bool                 openSaveAs = false;
    std::array<char, 64> saveAsName = [] {
        std::array<char, 64> value{};
        constexpr char initial[] = "MyScene";
        for (std::size_t i = 0; i < sizeof(initial); ++i)
        {
            value[i] = initial[i];
        }
        return value;
    }();

    // Build > Package Project keeps its choices across popup opens. The
    // selected Scene is a real file in the editor's runtime Assets copy;
    // PackageBuilder translates it into the Player's package-relative path.
    bool                          openPackageProject = false;
    std::filesystem::path         packageScenePath;
    std::array<char, 512>         packageOutputDir{};
    bool                          saveSceneBeforePackage = true;

    // The single boundary for state that can contain Entity handles or
    // deferred edits aimed at the old World. Scene path/status intentionally
    // survive; callers decide whether a New/Open operation changes them.
    void OnWorldReplaced();

    // Play mutates the live World in place. Enter captures its exact scene
    // bytes; Stop first restores those bytes into a temporary World and only
    // commits the replacement after the complete parse succeeds.
    bool EnterPlay(World& world, const ResourceManager& resources,
                   PlaySession& play);
    bool StopPlay(World& world, ResourceManager& resources, PlaySession& play);

    bool HasPlaySnapshot() const { return !m_playSnapshot.empty(); }

private:
    // Editor-only, memory-only rollback state. It is never a scene component,
    // never autosaved, and disappears naturally when the editor closes.
    std::string m_playSnapshot;
};
