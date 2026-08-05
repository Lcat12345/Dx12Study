// DebugUI.h : the debug panels. This is the file that knows about entities.
//
// Kept on the game side deliberately: ImGuiLayer handles the backend and
// stays ignorant of the World, exactly like the renderer does.
#pragma once

#include "Core/World.h"
#include "Game/ExecutionContext.h"
#include "Graphics/RenderData.h"

#include <cstdint>

class AssetBrowser;
class EditorSession;
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
    bool  msaaEnabled      = false;
    bool  msaa4xSupported  = false;
    // The scene, already rendered, as an ImGui texture id. Plain integer so
    // no D3D type reaches this file.
    std::uint64_t sceneTexture = 0;
    // The directional light's depth map, same deal. Shown in its own panel so
    // a pass with no on-screen output is still inspectable.
    std::uint64_t shadowTexture = 0;
    unsigned      shadowMapSize = 0;
    // The sphere the light's orthographic volume was sized to. Shown because
    // the image alone cannot say whether the volume is a sane size.
    float         shadowCenter[3] = { 0.0f, 0.0f, 0.0f };
    float         shadowRadius    = 0.0f;
    // Debug layer messages so far. hasDebugLayer says whether the count means
    // anything - 0 with no layer is "unknown", not "clean".
    std::uint64_t debugMessages   = 0;
    bool          hasDebugLayer   = false;
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
    // Picking uses the exact camera selected for this frame's render.
    const CameraView* viewportCamera = nullptr;
    RunMode           runMode        = RunMode::Edit;
    float             playElapsed    = 0.0f;
    // Apply the built-in panel arrangement this frame, overwriting whatever
    // is docked now. True on a first run (no imgui.ini to respect) and for the
    // one frame after View > Reset layout. The caller owns the flag because
    // only it knows whether a saved layout existed - see
    // ImGuiLayer::HadSavedLayout.
    bool              applyDefaultLayout = false;

    // --- out ---
    bool vsyncToggled = false;
    bool msaaToggled  = false;
    // The size the Scene panel wants its texture to be. Zero while the panel
    // is collapsed, which the renderer reads as "leave it alone".
    unsigned viewportWidth  = 0;
    unsigned viewportHeight = 0;
    // Camera input is allowed only while the cursor is over the scene, so
    // dragging a slider no longer spins the view.
    bool viewportHovered = false;
    bool    runModeChangeRequested = false;
    RunMode requestedRunMode       = RunMode::Edit;
    // The user picked View > Reset layout. The caller feeds it back as
    // applyDefaultLayout next frame rather than this one, because the panels
    // for this frame have already been positioned by the time the menu runs.
    bool    resetLayoutRequested   = false;
};

// Builds this frame's UI. Call between ImGuiLayer::NewFrame and Render.
// The browser is passed in rather than owned here: it holds scan results
// across frames, and the inspector assigns whatever it has selected.
//
// 'world' may be REPLACED wholesale when a scene is loaded, which is why it
// is a mutable reference rather than something this file only reads.
void DrawDebugUI(World& world, ResourceManager& resources, AssetBrowser& assets,
                 EditorSession& session, DebugUIContext& ui);
