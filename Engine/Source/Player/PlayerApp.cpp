#include "Player/PlayerApp.h"

#include "Game/BuildWorld.h"
#include "Game/Systems.h"

#include <stdexcept>

namespace
{
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine Player";
    constexpr UINT    kClientWidth   = 1280;
    constexpr UINT    kClientHeight  = 720;
}

PlayerApp::PlayerApp(HINSTANCE instance, const RuntimePaths& runtimePaths)
    : Engine(instance, kWindowTitle, kClientWidth, kClientHeight, runtimePaths)
{
}

void PlayerApp::OnInit()
{
    BuildWorld(GetRenderer().Resources(), m_world);
    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player world has no active camera");
    }
    m_play.Begin();
}

void PlayerApp::OnUpdate(float dt)
{
    const FrameContext frame = MakePlayerFrameContext(CaptureHostFrame(dt));
    m_play.BeginFrame(frame);
    RunPlaySystems(m_world, m_play);
    m_play.EndFrame();

    if (!GetActiveCameraView(m_world, m_camera))
    {
        throw std::runtime_error("Player lost its active camera");
    }
    if (frame.input.WasPressed('V'))
    {
        GetRenderer().SetVSync(!GetRenderer().IsVSync());
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
