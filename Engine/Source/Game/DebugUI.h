// DebugUI.h : the debug panels. This is the file that knows about entities.
//
// Kept on the game side deliberately: ImGuiLayer handles the backend and
// stays ignorant of the World, exactly like the renderer does.
#pragma once

#include "Core/World.h"

#include <cstdint>

class AssetBrowser;
class ResourceManager;

// What the panels read from the engine, and what they hand back. A struct
// rather than a parameter list: the viewport panel alone would have added
// four, and every new panel adds more.
struct DebugUIContext
{
    // --- in ---
    float dt               = 0.0f;
    int   fps              = 0;
    bool  vsync            = true;
    bool  tearingSupported = false;
    // The scene, already rendered, as an ImGui texture id. Plain integer so
    // no D3D type reaches this file.
    std::uint64_t sceneTexture = 0;
    // How many draw items the object constant buffer holds. Shown next to
    // the live count, because exceeding it throws rather than degrades.
    unsigned maxDrawItems = 0;
    // What the renderer actually received last frame - NOT "entities with a
    // MeshRenderer". One with no mesh assigned yet is skipped, and a counter
    // that disagreed with the picture would be worse than none.
    unsigned drawItemCount = 0;
    // The aspect the scene was drawn with. Unprojecting a click needs the
    // render target's shape, not the window's.
    float sceneAspect = 1.0f;

    // --- out ---
    bool vsyncToggled = false;
    // The size the Scene panel wants its texture to be. Zero while the panel
    // is collapsed, which the renderer reads as "leave it alone".
    unsigned viewportWidth  = 0;
    unsigned viewportHeight = 0;
    // Camera input is allowed only while the cursor is over the scene, so
    // dragging a slider no longer spins the view.
    bool viewportHovered = false;
};

// Builds this frame's UI. Call between ImGuiLayer::NewFrame and Render.
// The browser is passed in rather than owned here: it holds scan results
// across frames, and the inspector assigns whatever it has selected.
//
// 'world' may be REPLACED wholesale when a scene is loaded, which is why it
// is a mutable reference rather than something this file only reads.
void DrawDebugUI(World& world, ResourceManager& resources, AssetBrowser& assets,
                 DebugUIContext& ui);
