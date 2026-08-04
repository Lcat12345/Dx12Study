#include "Graphics/RenderTarget.h"

#include "Graphics/GraphicsDevice.h"
#include "Core/Common.h"

#include <algorithm>

using Microsoft::WRL::ComPtr;

RenderTarget::RenderTarget(GraphicsDevice& device,
                           DescriptorAllocator& rtvAllocator,
                           DescriptorAllocator* srvAllocator,
                           UINT width, UINT height,
                           DXGI_FORMAT colorFormat,
                           const float clearColor[4],
                           UINT sampleCount,
    UINT sampleQuality)
    : m_device(device)
    , m_sampled(srvAllocator != nullptr)
    , m_width(std::max(width, 1u))
    , m_height(std::max(height, 1u))
    , m_sampleCount(std::max(sampleCount, 1u))
    , m_sampleQuality(sampleCount > 1 ? sampleQuality : 0)
    , m_colorFormat(colorFormat)
{
    for (int i = 0; i < 4; ++i)
    {
        m_clearColor[i] = clearColor[i];
    }

    // Taken once for the lifetime of this target. Resizing swaps the texture
    // underneath but keeps the same slots, so anyone holding the SRV handle
    // (ImGui) stays valid.
    m_rtv = rtvAllocator.Allocate();
    if (srvAllocator)
    {
        m_srv = srvAllocator->Allocate();
    }

    CreateResources();
}

void RenderTarget::CreateResources()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU-local

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width            = m_width;
    colorDesc.Height           = m_height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels        = 1;
    colorDesc.Format           = m_colorFormat;
    colorDesc.SampleDesc.Count   = m_sampleCount;
    colorDesc.SampleDesc.Quality = m_sampleQuality;
    colorDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format = m_colorFormat;
    for (int i = 0; i < 4; ++i)
    {
        colorClear.Color[i] = m_clearColor[i];
    }

    // A sampled 1x texture rests shader-readable. A multisample colour source
    // always rests as RESOLVE_SOURCE, ready for its presentation destination.
    const D3D12_RESOURCE_STATES initialColorState = IsMultisampled()
        ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE
        : (m_sampled ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                     : D3D12_RESOURCE_STATE_RENDER_TARGET);
    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
                      initialColorState,
                      &colorClear, IID_PPV_ARGS(&m_color)),
                  "CreateCommittedResource(RenderTarget colour)");

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format        = m_colorFormat;
    rtvDesc.ViewDimension = IsMultisampled() ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                                             : D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device.Device()->CreateRenderTargetView(m_color.Get(), &rtvDesc, m_rtv.cpu);

    if (IsMultisampled() && m_sampled)
    {
        // The resolve destination is deliberately a plain, single-sample
        // texture. It is never an RTV; ResolveSubresource is its only writer.
        D3D12_RESOURCE_DESC resolveDesc = colorDesc;
        resolveDesc.SampleDesc.Count   = 1;
        resolveDesc.SampleDesc.Quality = 0;
        resolveDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;
        ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &resolveDesc,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                          nullptr, IID_PPV_ARGS(&m_resolve)),
                      "CreateCommittedResource(RenderTarget resolve)");
    }

    if (m_sampled)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                  = m_colorFormat;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels     = 1;
        m_device.Device()->CreateShaderResourceView(ShaderResource(), &srvDesc, m_srv.cpu);
    }
}

void RenderTarget::Resize(UINT width, UINT height)
{
    // A docked panel can collapse to zero. Creating a 0-wide texture fails,
    // so clamp rather than letting the app die on a window drag.
    width  = std::max(width, 1u);
    height = std::max(height, 1u);

    if (width == m_width && height == m_height)
    {
        return;
    }

    m_width  = width;
    m_height = height;

    // Dropping the old texture here is only safe because the caller flushed
    // the GPU first.
    m_color.Reset();
    m_resolve.Reset();
    CreateResources();
}

void RenderTarget::SetSampleDesc(UINT sampleCount, UINT sampleQuality)
{
    sampleCount = std::max(sampleCount, 1u);
    sampleQuality = sampleCount > 1 ? sampleQuality : 0;
    if (sampleCount == m_sampleCount && sampleQuality == m_sampleQuality)
    {
        return;
    }

    m_sampleCount   = sampleCount;
    m_sampleQuality = sampleQuality;
    m_color.Reset();
    m_resolve.Reset();
    CreateResources();
}

D3D12_VIEWPORT RenderTarget::Viewport() const
{
    return { 0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f };
}

D3D12_RECT RenderTarget::ScissorRect() const
{
    return { 0, 0, LONG(m_width), LONG(m_height) };
}
