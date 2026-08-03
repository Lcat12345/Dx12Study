#include "Graphics/DepthTarget.h"

#include "Core/Common.h"
#include "Graphics/GraphicsDevice.h"

#include <algorithm>

namespace
{
    // One depth buffer, three format names.
    //
    // A resource that is only ever a depth buffer can just BE D32_FLOAT. One
    // that also gets sampled cannot: a shader resource view of a
    // depth-stencil format is invalid. The way out is to create the resource
    // as TYPELESS - bits with no interpretation - and let each view say how
    // to read them.
    constexpr DXGI_FORMAT kDepthOnlyResource = DXGI_FORMAT_D32_FLOAT;
    constexpr DXGI_FORMAT kReadableResource  = DXGI_FORMAT_R32_TYPELESS;
    constexpr DXGI_FORMAT kDepthView         = DXGI_FORMAT_D32_FLOAT;
    constexpr DXGI_FORMAT kShaderView        = DXGI_FORMAT_R32_FLOAT;
}

DepthTarget::DepthTarget(GraphicsDevice& device,
                         DescriptorAllocator& dsvAllocator,
                         DescriptorAllocator* srvAllocator,
                         UINT width, UINT height,
                         UINT sampleCount,
                         UINT sampleQuality)
    : m_device(device)
    , m_width(std::max(width, 1u))
    , m_height(std::max(height, 1u))
    , m_sampleCount(std::max(sampleCount, 1u))
    , m_sampleQuality(sampleCount > 1 ? sampleQuality : 0)
    , m_readable(srvAllocator != nullptr)
{
    // Taken once for the lifetime of the target. Resizing swaps the resource
    // underneath but keeps the slots, so a handed-out handle stays valid.
    m_dsv = dsvAllocator.Allocate();
    if (srvAllocator)
    {
        m_srv = srvAllocator->Allocate();
    }

    CreateResources();
}

void DepthTarget::CreateResources()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU-local

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = m_width;
    desc.Height           = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = m_readable ? kReadableResource : kDepthOnlyResource;
    desc.SampleDesc.Count   = m_sampleCount;
    desc.SampleDesc.Quality = m_sampleQuality;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (!m_readable)
    {
        // Says "nothing will ever sample this", which lets the driver pick a
        // compressed layout it would otherwise have to avoid.
        desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    // Must match what ClearDepthStencilView passes, or the fast clear path
    // is lost. The clear value carries the VIEW format, not the resource's.
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format             = kDepthView;
    clearValue.DepthStencil.Depth = 1.0f; // 1.0 = farthest

    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE,
                      &clearValue, IID_PPV_ARGS(&m_resource)),
                  "CreateCommittedResource(DepthTarget)");

    // The view spells out the format the typeless bits should be read as.
    // Passing nullptr here would work for the non-typeless case and fail for
    // the other, so it is always explicit.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = kDepthView;
    dsvDesc.ViewDimension = m_sampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                              : D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device.Device()->CreateDepthStencilView(m_resource.Get(), &dsvDesc, m_dsv.cpu);

    if (m_readable)
    {
        // The SAME bits, read as a single red float channel. Sampling gives
        // the stored depth in x.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        // NOT the default mapping. R32_FLOAT has only one real channel, and
        // the DEFAULT mapping is identity - G and B read as 0, A as 1 - so a
        // naive RGBA sample comes back (depth, 0, 0, 1): pure red, whatever
        // the depth actually is. A depth-sampling shader that reads only .r
        // (11.5) would never notice; the editor's debug Image, which samples
        // all four channels to show a picture, showed exactly this - solid
        // red instead of a greyscale depth map.
        //
        // Replicating component 0 into R, G AND B (and forcing A to 1) makes
        // .r unaffected - still the first component read - while turning a
        // plain RGBA sample into the grey a depth map should look like.
        srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
            D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
            D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
        srvDesc.Format                  = kShaderView;
        srvDesc.ViewDimension           = m_sampleCount > 1
                                        ? D3D12_SRV_DIMENSION_TEXTURE2DMS
                                        : D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels     = 1;
        m_device.Device()->CreateShaderResourceView(m_resource.Get(), &srvDesc, m_srv.cpu);
    }
}

void DepthTarget::Resize(UINT width, UINT height)
{
    // A docked panel can collapse to nothing; a zero-sized texture fails to
    // create.
    width  = std::max(width, 1u);
    height = std::max(height, 1u);

    if (width == m_width && height == m_height)
    {
        return;
    }

    m_width  = width;
    m_height = height;

    // Safe only because the caller flushed the GPU first.
    m_resource.Reset();
    CreateResources();
}

void DepthTarget::SetSampleDesc(UINT sampleCount, UINT sampleQuality)
{
    sampleCount = std::max(sampleCount, 1u);
    sampleQuality = sampleCount > 1 ? sampleQuality : 0;
    if (sampleCount == m_sampleCount && sampleQuality == m_sampleQuality)
    {
        return;
    }

    m_sampleCount   = sampleCount;
    m_sampleQuality = sampleQuality;
    m_resource.Reset();
    CreateResources();
}

D3D12_VIEWPORT DepthTarget::Viewport() const
{
    return { 0.0f, 0.0f, float(m_width), float(m_height), 0.0f, 1.0f };
}

D3D12_RECT DepthTarget::ScissorRect() const
{
    return { 0, 0, LONG(m_width), LONG(m_height) };
}
