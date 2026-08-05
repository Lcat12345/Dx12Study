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

    // Point the backend at a replaced SRV heap.
    //
    // The backend caches the heap POINTER at init and binds it itself in
    // RenderDrawData, and each texture it owns caches a GPU handle into that
    // heap. Growing the heap invalidates both. Upstream has no API for this,
    // so ImGui_ImplDX12_RebindDescriptorHeap is a small local patch - see the
    // comment on its declaration.
    //
    // Shutting the backend down and re-initialising it was tried first and
    // does not work: Shutdown marks the font texture Destroyed and the next
    // ImGui::NewFrame walks into it. Rebinding leaves textures, the font
    // atlas, and every status untouched.
    //
    // Call at a frame boundary, with the GPU drained and the new heap already
    // holding the same slot indices as the old one.
    void RebindDescriptorHeap();

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
    bool InitBackend();

    // Everything ImGui_ImplDX12_Init needs, kept so the backend can be built
    // a second time without the editor having to hand it all over again.
    ID3D12Device*        m_device       = nullptr;
    ID3D12CommandQueue*  m_commandQueue = nullptr;
    DescriptorAllocator* m_srvAllocator = nullptr;
    DXGI_FORMAT          m_rtvFormat    = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT          m_dsvFormat    = DXGI_FORMAT_UNKNOWN;
    int                  m_framesInFlight = 1;

    bool m_hadSavedLayout = false;
};
