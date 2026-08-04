#include "Game/EditorSession.h"

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
}
