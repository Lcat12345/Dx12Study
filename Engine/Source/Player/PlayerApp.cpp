#include "Player/PlayerApp.h"

#include "Game/Scene.h"
#include "Game/Systems.h"
#include "Core/TextEncoding.h"

#include <stdexcept>
#include <utility>

namespace
{
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;
}

PlayerApp::PlayerApp(HINSTANCE instance, const RuntimePaths& runtimePaths,
                     std::filesystem::path scenePath, const wchar_t* windowTitle)
    : Engine(instance, windowTitle, kClientWidth, kClientHeight, runtimePaths)
    , m_scenePath(std::move(scenePath))
{
}

void PlayerApp::OnInit()
{
    std::string error;
    if (!LoadScene(m_scenePath, GetRenderer().Resources(), m_world, error))
    {
        throw std::runtime_error("could not load Player Scene '" +
                                 ToUtf8(m_scenePath.wstring()) + "': " + error);
    }
    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player Scene has no ActiveCamera: " +
                                 ToUtf8(m_scenePath.wstring()));
    }
    m_play.Begin();
}

void PlayerApp::OnUpdate(float dt)
{
    const FrameContext frame = MakePlayerFrameContext(CaptureHostFrame(dt));
    m_play.BeginFrame(frame);
    RunPlaySystems(m_world, m_play, &GetRenderer().Resources());
    m_play.EndFrame();

    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player lost its active camera");
    }
    if (frame.input.WasPressed('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
    }
    if (frame.input.WasPressed('M'))
    {
        GetRenderer().SetMsaaEnabled(!GetRenderer().IsMsaaEnabled());
    }
}

FrameContext PlayerApp::CaptureHostFrame(float dt)
{
    FrameContext frame;
    frame.deltaSeconds = dt;
    const UINT height = GetWindow().ClientHeight();
    frame.renderAspect = height == 0
        ? 1.0f
        : float(GetWindow().ClientWidth()) / float(height);

    GetWindow().ConsumeMouseDelta(frame.input.mouseDeltaX, frame.input.mouseDeltaY);
    for (int key = 0; key < InputContext::kKeyCount; ++key)
    {
        frame.input.keyDown[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        frame.input.keyPressed[key] = GetWindow().ConsumeKeyPress(key);
    }
    return frame;
}

void PlayerApp::OnRender()
{
    BuildRenderData(m_world, GetRenderer().Resources(), m_drawItems, m_lighting);
    GetRenderer().RenderFrame(m_camera, m_lighting, m_drawItems,
                              Renderer::SceneOutput::SwapChain);
}
