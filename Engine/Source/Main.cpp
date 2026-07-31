// Main.cpp : Application entry point.
// Phase 2 - D3D12 initialization and clear screen. (see ROADMAP.md)
//
// Everything lives in this one file on purpose: no abstraction until the
// repetition shows up (Phase 9). Initialization functions appear in the
// exact order they are called, so reading top-to-bottom = init order.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>   // Microsoft::WRL::ComPtr - smart pointer for COM objects
#include <stdexcept>
#include <cstdio>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // ---------------------------------------------------------------- settings

    constexpr wchar_t kClassName[]   = L"Dx12EngineWndClass";
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine";
    constexpr int     kClientWidth   = 1280;
    constexpr int     kClientHeight  = 720;

    constexpr UINT  kFrameCount    = 2;                          // double buffering
    constexpr float kClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f }; // RGBA

    // ---------------------------------------------------------------- D3D12 state
    // Globals for now - Phase 9 wraps these into classes. ComPtr calls
    // Release() automatically when the last reference goes away.

    ComPtr<ID3D12Device>              g_device;
    ComPtr<ID3D12CommandQueue>        g_commandQueue;
    ComPtr<IDXGISwapChain3>           g_swapChain;
    ComPtr<ID3D12Resource>            g_renderTargets[kFrameCount];
    ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
    UINT                              g_rtvDescriptorSize = 0;
    ComPtr<ID3D12CommandAllocator>    g_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> g_commandList;

    // Fence: CPU-GPU synchronization (see WaitForGpu below).
    ComPtr<ID3D12Fence> g_fence;
    UINT64              g_fenceValue = 0;
    HANDLE              g_fenceEvent = nullptr;

    UINT g_frameIndex = 0; // which back buffer we are drawing to this frame

    // ---------------------------------------------------------------- helpers

    void ThrowIfFailed(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg), "%s failed (HRESULT 0x%08X)",
                          what, static_cast<unsigned int>(hr));
            throw std::runtime_error(msg);
        }
    }

    // Fills the most common barrier type: "this resource transitions from
    // state A to state B". The GPU may reorder/overlap work; a barrier is
    // our promise about how the resource is used before and after it.
    D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
                                             D3D12_RESOURCE_STATES before,
                                             D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }

    // ---------------------------------------------------------------- init steps
    // Called from wWinMain in this order.

    // [1] Debug layer - MUST be enabled before the device is created.
    //     Validates API usage every call and prints readable errors to the
    //     VS Output window. Debug builds only (it costs performance).
    void EnableDebugLayer()
    {
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
#endif
    }

    // [2] DXGI factory - the entry point to everything display-related:
    //     enumerating GPUs (adapters) and creating swap chains.
    ComPtr<IDXGIFactory6> CreateFactory()
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)),
                      "CreateDXGIFactory2");
        return factory;
    }

    // [3] Device - our connection to one physical GPU. Walk the adapters
    //     (fastest first), skip software rasterizers, take the first one
    //     that speaks D3D12.
    void CreateDevice(IDXGIFactory6* factory)
    {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             factory->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue; // WARP etc. - we want real hardware
            }
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&g_device))))
            {
                return;
            }
        }
        throw std::runtime_error("No D3D12-capable hardware GPU found");
    }

    // [4] Command objects. D3D12 never executes work immediately:
    //     - command LIST      : where the CPU records commands
    //     - command ALLOCATOR : the memory those commands are recorded into
    //     - command QUEUE     : where recorded lists are submitted for the
    //                           GPU to execute (DIRECT = graphics-capable)
    void CreateCommandObjects()
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(g_device->CreateCommandQueue(&queueDesc,
                                                   IID_PPV_ARGS(&g_commandQueue)),
                      "CreateCommandQueue");

        ThrowIfFailed(g_device->CreateCommandAllocator(
                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                          IID_PPV_ARGS(&g_commandAllocator)),
                      "CreateCommandAllocator");

        ThrowIfFailed(g_device->CreateCommandList(
                          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                          g_commandAllocator.Get(), nullptr,
                          IID_PPV_ARGS(&g_commandList)),
                      "CreateCommandList");

        // Lists are born in the "recording" state, but our render loop
        // starts every frame with Reset() which requires a CLOSED list.
        ThrowIfFailed(g_commandList->Close(), "Close command list");
    }

    // [5] Swap chain - the pair of buffers we alternate between: draw into
    //     the back buffer while the front buffer is on screen, then flip.
    //     Note it is created from the QUEUE, not the device: Present() is
    //     itself work that goes through the queue.
    void CreateSwapChain(IDXGIFactory6* factory, HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.BufferCount      = kFrameCount;
        desc.Width            = kClientWidth;
        desc.Height           = kClientHeight;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model
        desc.SampleDesc.Count = 1;                             // no MSAA

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
                          g_commandQueue.Get(), hWnd, &desc,
                          nullptr, nullptr, &swapChain1),
                      "CreateSwapChainForHwnd");

        // Upgrade to IDXGISwapChain3 for GetCurrentBackBufferIndex().
        ThrowIfFailed(swapChain1.As(&g_swapChain), "IDXGISwapChain3 query");

        // DXGI's built-in Alt+Enter fullscreen toggle fights with manual
        // resize handling - disable it, we will own fullscreen later.
        ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER),
                      "MakeWindowAssociation");

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }

    // [6] RTV descriptor heap + one RTV per back buffer.
    //     A descriptor is a small GPU-readable "view" struct describing how
    //     to use a resource; descriptors must live inside descriptor heaps.
    //     An RTV (render target view) says "this resource can be drawn to".
    void CreateRenderTargets()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = kFrameCount;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_rtvHeap)),
                      "CreateDescriptorHeap(RTV)");

        // Descriptor sizes are GPU-specific - always ask, never hardcode.
        g_rtvDescriptorSize =
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i)
        {
            ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])),
                          "SwapChain GetBuffer");
            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                             rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize; // step to the next slot
        }
    }

    // [7] Fence - a monotonically increasing counter the GPU writes and the
    //     CPU reads. Signal(fence, N) enqueues "set fence to N when all work
    //     before this point is done"; the CPU can then wait for value N.
    void CreateSyncObjects()
    {
        ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(&g_fence)),
                      "CreateFence");

        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent)
        {
            throw std::runtime_error("CreateEvent failed");
        }
    }

    // Block the CPU until the GPU has finished ALL submitted work.
    // Simple but wasteful - the CPU sits idle while the GPU draws, and
    // vice versa. Phase 9 (frame resources) overlaps them properly.
    void WaitForGpu()
    {
        const UINT64 valueToWait = ++g_fenceValue;
        ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), valueToWait),
                      "Queue Signal");

        if (g_fence->GetCompletedValue() < valueToWait)
        {
            ThrowIfFailed(g_fence->SetEventOnCompletion(valueToWait, g_fenceEvent),
                          "SetEventOnCompletion");
            WaitForSingleObject(g_fenceEvent, INFINITE);
        }
    }

    // ---------------------------------------------------------------- per frame

    void Render()
    {
        // Reset = reuse the memory from last frame's commands. Only safe
        // because WaitForGpu() below guarantees the GPU is done with it.
        ThrowIfFailed(g_commandAllocator->Reset(), "Allocator Reset");
        ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), nullptr),
                      "CommandList Reset");

        ID3D12Resource* backBuffer = g_renderTargets[g_frameIndex].Get();

        // The buffer we are about to draw into was just being presented.
        // Transition: PRESENT -> RENDER_TARGET.
        D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
            backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_commandList->ResourceBarrier(1, &toRenderTarget);

        // Point at this frame's RTV slot and clear it.
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += SIZE_T(g_frameIndex) * g_rtvDescriptorSize;

        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        g_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);

        // Done drawing - transition back so DXGI may present it.
        D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
            backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        g_commandList->ResourceBarrier(1, &toPresent);

        ThrowIfFailed(g_commandList->Close(), "CommandList Close");

        // Everything so far was only RECORDED. This line hands it to the GPU.
        ID3D12CommandList* lists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, lists);

        // Show the back buffer (1 = wait for vsync), then flush and move on
        // to the buffer that just left the screen.
        ThrowIfFailed(g_swapChain->Present(1, 0), "Present");
        WaitForGpu();
        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }

    // ---------------------------------------------------------------- window

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            // Swap chain resize lands here in Phase 6.
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    HWND CreateMainWindow(HINSTANCE hInstance)
    {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
        {
            return nullptr;
        }

        RECT windowRect = { 0, 0, kClientWidth, kClientHeight };
        const DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRect(&windowRect, style, FALSE);

        return CreateWindowExW(
            0, kClassName, kWindowTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr, hInstance, nullptr);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    HWND hWnd = CreateMainWindow(hInstance);
    if (!hWnd)
    {
        return -1;
    }

    try
    {
        EnableDebugLayer();                        // [1] before device creation!
        ComPtr<IDXGIFactory6> factory = CreateFactory(); // [2]
        CreateDevice(factory.Get());               // [3]
        CreateCommandObjects();                    // [4]
        CreateSwapChain(factory.Get(), hWnd);      // [5]
        CreateRenderTargets();                     // [6]
        CreateSyncObjects();                       // [7]
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "D3D12 initialization failed",
                    MB_OK | MB_ICONERROR);
        return -1;
    }

    ShowWindow(hWnd, nCmdShow);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            Render();
        }
    }

    // Never destroy resources the GPU might still be reading - flush first.
    WaitForGpu();
    CloseHandle(g_fenceEvent);

    return static_cast<int>(msg.wParam);
}
