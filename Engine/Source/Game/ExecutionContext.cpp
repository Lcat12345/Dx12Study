#include "Game/ExecutionContext.h"

#include <algorithm>

namespace
{
    bool IsValidKey(int virtualKey)
    {
        return virtualKey >= 0 && virtualKey < InputContext::kKeyCount;
    }
}

bool InputContext::IsDown(int virtualKey) const
{
    return IsValidKey(virtualKey) && keyDown[virtualKey];
}

bool InputContext::WasPressed(int virtualKey) const
{
    return IsValidKey(virtualKey) && keyPressed[virtualKey];
}

void InputContext::ClearKeyboard()
{
    keyDown.fill(false);
    keyPressed.fill(false);
}

FrameContext MakeEditorFrameContext(const FrameContext& hostFrame,
                                    bool viewportHovered,
                                    bool uiCapturesKeyboard)
{
    FrameContext frame = hostFrame;
    if (!viewportHovered)
    {
        frame.input.mouseDeltaX = 0.0f;
        frame.input.mouseDeltaY = 0.0f;
    }
    if (uiCapturesKeyboard)
    {
        frame.input.ClearKeyboard();
    }
    return frame;
}

FrameContext MakePlayerFrameContext(const FrameContext& hostFrame)
{
    return hostFrame;
}

void PlaySession::Begin()
{
    m_active         = true;
    m_frameOpen      = false;
    m_elapsedSeconds = 0.0f;
    m_deltaSeconds   = 0.0f;
    m_input          = InputContext{};
}

void PlaySession::BeginFrame(const FrameContext& frame)
{
    if (!m_active)
    {
        return;
    }

    m_frameOpen    = true;
    m_deltaSeconds = (std::max)(0.0f, frame.deltaSeconds);
    m_input        = frame.input;
}

void PlaySession::EndFrame()
{
    if (!m_active || !m_frameOpen)
    {
        return;
    }

    m_elapsedSeconds += m_deltaSeconds;
    m_frameOpen = false;
}

void PlaySession::End()
{
    m_active         = false;
    m_frameOpen      = false;
    m_elapsedSeconds = 0.0f;
    m_deltaSeconds   = 0.0f;
    m_input          = InputContext{};
}
