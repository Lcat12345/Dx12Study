// ExecutionContext.h : the explicit boundary between host frames, editor
// frames, and a running game session.
#pragma once

#include <array>

// RunMode is editor state, not scene data. EditorSession owns the snapshot
// transaction attached to these transitions.
enum class RunMode
{
    Edit,
    Play
};

// Flattened host input. Systems consume this value object instead of polling
// Win32 or knowing whether ImGui captured a device.
struct InputContext
{
    static constexpr int kKeyCount = 256;

    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    std::array<bool, kKeyCount> keyDown{};
    std::array<bool, kKeyCount> keyPressed{};

    bool IsDown(int virtualKey) const;
    bool WasPressed(int virtualKey) const;
    void ClearKeyboard();
};

// Common information for one update. renderAspect follows the surface the
// host will present, so both camera selection and picking can share it.
struct FrameContext
{
    float        deltaSeconds = 0.0f;
    float        renderAspect = 1.0f;
    InputContext input;
};

// An editor-hosted game only receives devices that the scene viewport owns.
// These pure transforms are also the contract for the Player host, which has
// no viewport-hover or text-input concepts to pass in.
//
// Both devices are gated on HOVER, matching what the manual promises: the
// editor camera is driven "over the Scene panel". Keyboard used to be gated on
// ImGui's WantCaptureKeyboard instead, which is a different question - with
// keyboard navigation enabled that is true whenever ANY ImGui window has
// focus. Once the panels were docked, clicking the Scene focused an ImGui
// window and WASD went dead until the user clicked some bare, non-ImGui pixel
// to blur it.
//
// uiWantsTextInput is the narrow case that genuinely conflicts: a live text
// field, where W and S are letters rather than movement. ImGui's keyboard
// navigation uses arrows, Tab, Enter and Space, none of which the camera
// binds, so nav focus alone is not a reason to swallow input.
FrameContext MakeEditorFrameContext(const FrameContext& hostFrame,
                                    bool viewportHovered,
                                    bool uiWantsTextInput);
FrameContext MakePlayerFrameContext(const FrameContext& hostFrame);

// Time and input whose lifetime begins at Play. Edges are copied per frame
// and cleared at every Begin/End boundary, so stale editor input cannot leak
// into a new game session.
class PlaySession
{
public:
    void Begin();
    void BeginFrame(const FrameContext& frame);
    void EndFrame();
    void End();

    bool IsActive() const { return m_active; }
    float ElapsedSeconds() const { return m_elapsedSeconds; }
    float DeltaSeconds() const { return m_deltaSeconds; }
    const InputContext& Input() const { return m_input; }

private:
    bool         m_active         = false;
    bool         m_frameOpen      = false;
    float        m_elapsedSeconds = 0.0f;
    float        m_deltaSeconds   = 0.0f;
    InputContext m_input;
};
