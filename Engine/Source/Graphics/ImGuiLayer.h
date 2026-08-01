// ImGuiLayer.h : Dear ImGui plumbing. Knows nothing about the game.
//
// What goes in a panel is decided elsewhere (Game/DebugUI) - this file only
// owns the backend's lifetime and the two calls that bracket a frame.
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgiformat.h>

class GraphicsDevice;
class DescriptorAllocator;

class ImGuiLayer
{
public:
    ImGuiLayer(HWND hwnd, GraphicsDevice& device, DescriptorAllocator& srvAllocator,
               DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, int framesInFlight);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Call once per frame before any UI is built.
    void NewFrame();

    // Records the UI into the command list. Must come AFTER the scene and
    // BEFORE the back buffer transitions to PRESENT.
    void Render(ID3D12GraphicsCommandList* commandList);

    // True when the cursor or keyboard is over the UI, so the game should
    // not also act on it - otherwise dragging a slider spins the camera.
    bool WantsMouse() const;
    bool WantsKeyboard() const;

    // Forwarded from the window procedure. Returns true when ImGui consumed
    // the message.
    static bool HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};
