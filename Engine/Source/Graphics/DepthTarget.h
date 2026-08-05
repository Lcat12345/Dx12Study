// DepthTarget.h : a depth buffer, on its own.
//
// Split out of RenderTarget because the two things a depth surface is used
// for pull in opposite directions:
//
//   the scene's depth   - written, tested, never read back. DSV only.
//   a shadow map        - written in one pass, SAMPLED in the next. DSV+SRV.
//
// The second needs a TYPELESS resource, because one format cannot be both a
// depth-stencil view and a shader resource view. Making that an option here
// rather than a second copy of RenderTarget is what stops "depth-only pass"
// from being a special case everywhere else.
#pragma once

#include "Graphics/DescriptorAllocator.h"

#include <d3d12.h>
#include <wrl/client.h>

class GraphicsDevice;

class DepthTarget
{
public:
    // srvAllocator may be null when the depth is never sampled - the scene's
    // own depth buffer. Passing one switches the resource to typeless and
    // creates the extra view.
    DepthTarget(GraphicsDevice& device,
                DescriptorAllocator& dsvAllocator,
                DescriptorAllocator* srvAllocator,
                UINT width, UINT height,
                UINT sampleCount = 1,
                UINT sampleQuality = 0);

    DepthTarget(const DepthTarget&)            = delete;
    DepthTarget& operator=(const DepthTarget&) = delete;

    // The caller MUST have waited for the GPU: the resource is recreated.
    // Descriptor SLOTS are reused - only their contents change - so anything
    // holding the SRV handle stays valid.
    void Resize(UINT width, UINT height);
    // Safe only after the caller has drained the GPU.
    void SetSampleDesc(UINT sampleCount, UINT sampleQuality);

    UINT Width()       const { return m_width; }
    UINT Height()      const { return m_height; }
    UINT SampleCount() const { return m_sampleCount; }
    UINT SampleQuality() const { return m_sampleQuality; }

    ID3D12Resource* Resource() const { return m_resource.Get(); }

    // Resolved on every call rather than cached: growing the SRV heap
    // replaces it, and every GPU address with it.
    D3D12_CPU_DESCRIPTOR_HANDLE DSV() const;
    // Only valid when this target was created with an SRV allocator.
    bool                        HasSrv() const { return m_srv.IsValid(); }
    D3D12_GPU_DESCRIPTOR_HANDLE SRV()    const;

    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT     ScissorRect() const;

private:
    void CreateResources();

    GraphicsDevice& m_device;
    // Kept for the lifetime of the target: an index means nothing without the
    // allocator that issued it.
    DescriptorAllocator& m_dsvAllocator;
    DescriptorAllocator* m_srvAllocator = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;

    DescriptorHandle m_dsv;
    DescriptorHandle m_srv; // invalid unless the depth is sampled

    UINT m_width       = 0;
    UINT m_height      = 0;
    UINT m_sampleCount = 1;
    UINT m_sampleQuality = 0;
    bool m_readable    = false;
};
