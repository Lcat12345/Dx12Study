// EditorApp.h : editor host, UI ownership, and Edit/Play orchestration.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Editor/AssetBrowser.h"
#include "Editor/EditorSession.h"
#include "Game/ExecutionContext.h"
#include "Game/Systems.h"
#include "Editor/ImGuiLayer.h"
#include "Graphics/RenderData.h"

#include <memory>
#include <vector>

class EditorApp final : public Engine
{
public:
    EditorApp(HINSTANCE instance, const RuntimePaths& runtimePaths);
    ~EditorApp() override;

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

    World         m_world;
    EditorSession m_editor;
    PlaySession   m_play;
    EditorCamera  m_editorCamera;

    std::unique_ptr<AssetBrowser> m_assets;
    std::unique_ptr<ImGuiLayer>   m_overlay;

    std::vector<DrawItem> m_drawItems;
    CameraView            m_camera;
    LightingData          m_lighting;
    bool                  m_viewportHovered = false;
};
