// DebugUI.h : the debug panels. This is the file that knows about entities.
//
// Kept on the game side deliberately: ImGuiLayer handles the backend and
// stays ignorant of the World, exactly like the renderer does.
#pragma once

#include "Core/World.h"

// Builds this frame's UI. Call between ImGuiLayer::NewFrame and Render.
// dt and fps are passed in rather than recomputed so the numbers match the
// ones the engine is already tracking.
void DrawDebugUI(World& world, float dt, int fps, bool vsync, bool tearingSupported,
                 bool& outVSyncToggled);
