// DemoGame.h : the game side - what is in the world and how it is driven.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Game/AssetBrowser.h"
#include "Game/EditorSession.h"
#include "Game/ExecutionContext.h"
#include "Game/Systems.h"
#include "Graphics/RenderData.h"
#include "Graphics/ImGuiLayer.h"

#include <memory>
#include <vector>

// Everything specific to this demo lives here; Engine and Renderer stay
// unaware of entities.
class DemoGame : public Engine
{
public:
    DemoGame(HINSTANCE instance);
    ~DemoGame() override;

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    FrameContext CaptureHostFrame(float dt);
    void RunAlways(const FrameContext& frame);
    void RunEditorOnly(const FrameContext& frame);
    void RunPlayOnly(const FrameContext& frame);
    void SetRunMode(RunMode mode);

    World m_world;
    EditorSession m_editor;
    PlaySession   m_play;
    EditorCamera  m_editorCamera;

    // Created in OnInit, not the constructor: it needs the renderer's
    // ResourceManager, which does not exist until the base class has
    // initialized.
    std::unique_ptr<AssetBrowser> m_assets;
    // Editor-owned: destroyed before the base Engine and its graphics device.
    std::unique_ptr<ImGuiLayer> m_overlay;

    // Rebuilt every frame by the render system. Kept as a member so the
    // vector's storage is reused instead of reallocated 5000 times a second.
    std::vector<DrawItem> m_drawItems;
    // Selected once in OnUpdate and shared by render + viewport picking.
    CameraView            m_camera;
    LightingData          m_lighting;

    // Set by last frame's Scene panel. The systems run before the UI is
    // built, so this is necessarily one frame old - which is invisible at
    // frame rate, and the alternative (building the UI first) would let a
    // panel edit be overwritten by a system in the same frame.
    bool m_viewportHovered = false;
};
