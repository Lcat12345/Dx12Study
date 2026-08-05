// ImGuiLayer.h : Dear ImGui plumbing. Knows nothing about the game.
//
// What goes in a panel is decided elsewhere (Game/DebugUI) - this file only
// owns the backend's lifetime and the two calls that bracket a frame.
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>

class DescriptorAllocator;

class ImGuiLayer
{
public:
    ImGuiLayer(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue,
               DescriptorAllocator& srvAllocator,
               DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, int framesInFlight);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Call once per frame before any UI is built.
    void NewFrame();

    // Records the UI into the command list. Must come AFTER the scene and
    // BEFORE the back buffer transitions to PRESENT.
    void Render(ID3D12GraphicsCommandList* commandList);

    // True when the cursor is over the UI, so the game should not also act on
    // it - otherwise dragging a slider spins the camera.
    bool WantsMouse() const;

    // True only while a text field is taking input.
    //
    // Deliberately NOT WantCaptureKeyboard: with keyboard navigation enabled
    // that is true whenever any ImGui window merely has focus, which with
    // docked panels means "always", and it silently swallowed the editor's
    // WASD. This asks the narrower question the camera actually cares about.
    bool WantsTextInput() const;

    // Whether imgui.ini already existed when this layer started, i.e. whether
    // a previous run saved a panel arrangement.
    //
    // The editor needs this to decide between "apply my default layout" and
    // "leave the user's alone". It has to be captured at construction: once
    // ImGui has loaded the file, a restored layout and a freshly built one
    // look the same.
    bool HadSavedLayout() const { return m_hadSavedLayout; }

    // Forwarded from the window procedure. Returns true when ImGui consumed
    // the message.
    static bool HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool m_hadSavedLayout = false;
};
