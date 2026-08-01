// SwapChain.h : the back buffers and their render target views.
#pragma once

#include "Graphics/DescriptorAllocator.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

class GraphicsDevice;

// Owns the swap chain, the back buffers it hands out, and the RTV heap that
// describes them. The depth buffer deliberately lives in Renderer instead -
// it is a choice about how we draw, not part of what gets presented.
class SwapChain
{
public:
    static constexpr UINT       kBufferCount = 2; // double buffering
    static constexpr DXGI_FORMAT kFormat     = DXGI_FORMAT_R8G8B8A8_UNORM;

    SwapChain(GraphicsDevice& device, HWND hwnd, UINT width, UINT height);

    SwapChain(const SwapChain&)            = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    // The caller MUST have waited for the GPU first - the swap chain cannot
    // free buffers anyone still holds a reference to.
    void Resize(UINT width, UINT height);

    void Present(bool vsync);

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

    UINT CurrentBackBufferIndex() const { return m_backBufferIndex; }
    ID3D12Resource* CurrentBackBuffer() const
    {
        return m_backBuffers[m_backBufferIndex].Get();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferRTV() const
    {
        return m_backBufferRTVs[m_backBufferIndex].cpu;
    }

private:
    void CreateRenderTargetViews();

    GraphicsDevice& m_device;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_backBuffers[kBufferCount];

    // The slots are allocated once and REUSED across resizes - a resize
    // replaces the buffers, not where their views live.
    DescriptorAllocator m_rtvAllocator;
    DescriptorHandle    m_backBufferRTVs[kBufferCount];

    UINT m_backBufferIndex = 0;
    UINT m_width           = 0;
    UINT m_height          = 0;

    // Must be identical at creation and at every ResizeBuffers, or the
    // resize fails. Carries ALLOW_TEARING when the device reports support.
    UINT m_flags = 0;
};
