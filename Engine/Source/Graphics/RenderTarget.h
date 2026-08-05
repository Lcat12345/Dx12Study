// RenderTarget.h : an offscreen COLOUR surface the scene is drawn into.
//
// The colour texture is both a render target AND a shader resource: the
// scene writes it, then ImGui samples it to show inside a window. That dual
// role is why it flips between two states every frame.
//
// Depth is deliberately NOT here any more. A colour attachment and a depth
// attachment have different lifetimes and different view requirements - the
// shadow map needs depth with no colour at all - so they are separate types
// that a pass combines. See DepthTarget.
#pragma once

#include "Graphics/DescriptorAllocator.h"

#include <d3d12.h>
#include <wrl/client.h>

class GraphicsDevice;

class RenderTarget
{
public:
    RenderTarget(GraphicsDevice& device,
                 DescriptorAllocator& rtvAllocator,
                 DescriptorAllocator* srvAllocator,
                 UINT width, UINT height,
                 DXGI_FORMAT colorFormat,
                 const float clearColor[4],
                 UINT sampleCount = 1,
                 UINT sampleQuality = 0);

    RenderTarget(const RenderTarget&)            = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // The caller MUST have waited for the GPU. The texture is recreated, and
    // anything still referencing the old one would be reading freed memory.
    // Descriptor SLOTS are reused - only their contents change.
    void Resize(UINT width, UINT height);
    // The caller must have waited for the GPU, just like Resize(). The RTV
    // and SRV descriptor slots stay put while the resources behind them are
    // replaced.
    void SetSampleDesc(UINT sampleCount, UINT sampleQuality);

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }
    UINT SampleCount()   const { return m_sampleCount; }
    UINT SampleQuality() const { return m_sampleQuality; }
    bool IsMultisampled() const { return m_sampleCount > 1; }
    float AspectRatio() const
    {
        return m_height == 0 ? 1.0f : float(m_width) / float(m_height);
    }

    ID3D12Resource* ColorResource() const { return m_color.Get(); }
    // In 1x this is the colour resource itself. In MSAA mode it is the
    // single-sample texture ResolveSubresource writes and ImGui samples.
    ID3D12Resource* ShaderResource() const
    {
        return m_resolve ? m_resolve.Get() : m_color.Get();
    }
    bool HasShaderResource() const { return m_srv.IsValid(); }
    DXGI_FORMAT     ColorFormat()   const { return m_colorFormat; }

    // Resolved on every call rather than cached. The SRV heap is replaced
    // when it grows, which moves every GPU address - a stored handle would
    // survive as a number and stop meaning anything.
    D3D12_CPU_DESCRIPTOR_HANDLE RTV() const;
    // For ImGui::Image - the shader-visible handle of the colour texture.
    D3D12_GPU_DESCRIPTOR_HANDLE SRV() const;
    // The SRV's slot INDEX. What an overlay stores when it has to survive the
    // heap being replaced - see DescriptorAllocator.
    UINT SrvIndex() const { return m_srv.index; }

    const float* ClearColor() const { return m_clearColor; }

    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT     ScissorRect() const;

private:
    void CreateResources();

    GraphicsDevice& m_device;
    // Kept for the lifetime of the target: an index means nothing without the
    // allocator that issued it.
    DescriptorAllocator& m_rtvAllocator;
    DescriptorAllocator* m_srvAllocator = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_color;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resolve;

    DescriptorHandle m_rtv;
    DescriptorHandle m_srv;
    bool m_sampled = false;

    UINT        m_width  = 0;
    UINT        m_height = 0;
    UINT        m_sampleCount   = 1;
    UINT        m_sampleQuality = 0;
    DXGI_FORMAT m_colorFormat;
    float       m_clearColor[4];
};
