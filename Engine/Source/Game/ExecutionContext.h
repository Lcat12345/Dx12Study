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
// These pure transforms are also the contract for the future Player host:
// Player has no viewport-hover or ImGui-capture concepts to pass in.
FrameContext MakeEditorFrameContext(const FrameContext& hostFrame,
                                    bool viewportHovered,
                                    bool uiCapturesKeyboard);
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
