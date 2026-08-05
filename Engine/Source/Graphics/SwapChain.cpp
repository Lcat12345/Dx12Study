#include "Graphics/SwapChain.h"

#include "Graphics/GraphicsDevice.h"
#include "Core/Common.h"

using Microsoft::WRL::ComPtr;

SwapChain::SwapChain(GraphicsDevice& device, HWND hwnd, UINT width, UINT height)
    : m_device(device)
    , m_rtvAllocator(device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                     kBufferCount, /*shaderVisible*/ false)
    , m_width(width)
    , m_height(height)
{
    if (m_device.IsTearingSupported())
    {
        m_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount      = kBufferCount;
    desc.Width            = width;
    desc.Height           = height;
    desc.Format           = kFormat;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model
    desc.SampleDesc.Count = 1;                             // no MSAA
    desc.Flags            = m_flags;

    // Created from the QUEUE, not the device: Present is itself work that
    // goes through the queue.
    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(m_device.Factory()->CreateSwapChainForHwnd(
                      m_device.Queue(), hwnd, &desc, nullptr, nullptr, &swapChain1),
                  "CreateSwapChainForHwnd");

    // Upgrade for GetCurrentBackBufferIndex (added in IDXGISwapChain3).
    ThrowIfFailed(swapChain1.As(&m_swapChain), "IDXGISwapChain3 query");

    // DXGI's Alt+Enter fullscreen toggle fights with manual resize handling.
    ThrowIfFailed(m_device.Factory()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
                  "MakeWindowAssociation");

    // One slot per back buffer, taken once for the lifetime of the object.
    for (DescriptorHandle& rtv : m_backBufferRTVs)
    {
        rtv = m_rtvAllocator.Allocate();
    }

    CreateRenderTargetViews();
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void SwapChain::CreateRenderTargetViews()
{
    // The buffers belong to the swap chain; we only fetch them and describe
    // them. The descriptor slots survive a resize - creating a view into an
    // already-used slot simply overwrites it.
    for (UINT i = 0; i < kBufferCount; ++i)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])),
                      "SwapChain GetBuffer");
        m_device.Device()->CreateRenderTargetView(
            m_backBuffers[i].Get(), nullptr,
            m_rtvAllocator.CpuHandle(m_backBufferRTVs[i]));
    }
}

void SwapChain::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    // Every reference to the old buffers has to go before ResizeBuffers.
    for (UINT i = 0; i < kBufferCount; ++i)
    {
        m_backBuffers[i].Reset();
    }

    // Same flags as at creation - mismatching them fails the call.
    ThrowIfFailed(m_swapChain->ResizeBuffers(kBufferCount, width, height, kFormat, m_flags),
                  "ResizeBuffers");

    m_width  = width;
    m_height = height;

    CreateRenderTargetViews();
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void SwapChain::Present(bool vsync)
{
    // All three have to agree for an uncapped present: the swap chain flag
    // (set at creation), SyncInterval 0, and this per-Present flag.
    const UINT syncInterval = vsync ? 1u : 0u;
    const UINT presentFlags = (!vsync && m_device.IsTearingSupported())
                                  ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    ThrowIfFailed(m_swapChain->Present(syncInterval, presentFlags), "Present");

    // Present is what flips the buffers, so ask again afterwards.
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}
