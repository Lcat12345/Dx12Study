#include "Game/EditorSession.h"

#include "Game/Scene.h"

#include <utility>

void EditorSession::OnWorldReplaced()
{
    selected = Entity{};
    commands.clear();
    openSaveAs = false;

    saveAsName.fill('\0');
    constexpr char initial[] = "MyScene";
    for (std::size_t i = 0; i < sizeof(initial); ++i)
    {
        saveAsName[i] = initial[i];
    }

    runStatus.clear();
}

bool EditorSession::EnterPlay(World& world, const ResourceManager& resources,
                              PlaySession& play)
{
    if (runMode != RunMode::Edit)
    {
        runStatus = "Play is already running";
        return false;
    }

    // Capture locally so a failed serialization cannot publish a partial
    // transaction or disturb the current World/mode/session.
    std::string snapshot;
    std::string error;
    if (!CaptureSceneSnapshot(world, resources, snapshot, error))
    {
        runStatus = "Play failed: " +
                    (error.empty() ? std::string("could not capture the scene") : error);
        return false;
    }

    m_playSnapshot = std::move(snapshot);
    play.Begin();
    runMode   = RunMode::Play;
    runStatus = "Play: scene edits will be discarded on Stop";
    return true;
}

bool EditorSession::StopPlay(World& world, ResourceManager& resources,
                             PlaySession& play)
{
    if (runMode != RunMode::Play)
    {
        runStatus = "Play is not running";
        return false;
    }

    // RestoreSceneSnapshot is itself transactional, and using an explicit
    // temporary here keeps the host transition just as clear: no state below
    // this point changes until every entity/component/asset has restored.
    World restored;
    std::string error;
    if (!RestoreSceneSnapshot(m_playSnapshot, resources, restored, error))
    {
        runStatus = "Stop failed; Play kept running: " +
                    (error.empty() ? std::string("could not restore the scene") : error);
        return false;
    }

    world = std::move(restored);
    play.End();
    runMode  = RunMode::Edit;
    m_playSnapshot.clear();

    // Entity handles and queued edits belonged to the discarded Play World.
    // File identity intentionally survives this replacement boundary.
    OnWorldReplaced();
    runStatus = "Stopped: Play changes discarded";
    return true;
}
