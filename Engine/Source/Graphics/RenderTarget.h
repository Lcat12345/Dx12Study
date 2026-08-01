// RenderTarget.h : an offscreen surface the scene is drawn into.
//
// The colour texture is both a render target AND a shader resource: the
// scene writes it, then ImGui samples it to show inside a window. That dual
// role is why it flips between two states every frame.
//
// Phase 11's shadow mapping is the same idea with only the depth half - the
// class is shaped so that split is easy later.
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
                 DescriptorAllocator& dsvAllocator,
                 DescriptorAllocator& srvAllocator,
                 UINT width, UINT height,
                 DXGI_FORMAT colorFormat, DXGI_FORMAT depthFormat,
                 const float clearColor[4]);

    RenderTarget(const RenderTarget&)            = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // The caller MUST have waited for the GPU. The textures are recreated,
    // and anything still referencing the old ones would be reading freed
    // memory. Descriptor SLOTS are reused - only their contents change.
    void Resize(UINT width, UINT height);

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }
    float AspectRatio() const
    {
        return m_height == 0 ? 1.0f : float(m_width) / float(m_height);
    }

    ID3D12Resource* ColorResource() const { return m_color.Get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE RTV() const { return m_rtv.cpu; }
    D3D12_CPU_DESCRIPTOR_HANDLE DSV() const { return m_dsv.cpu; }
    // For ImGui::Image - the shader-visible handle of the colour texture.
    D3D12_GPU_DESCRIPTOR_HANDLE SRV() const { return m_srv.gpu; }

    const float* ClearColor() const { return m_clearColor; }

    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT     ScissorRect() const;

private:
    void CreateResources();

    GraphicsDevice& m_device;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_color;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depth;

    DescriptorHandle m_rtv;
    DescriptorHandle m_dsv;
    DescriptorHandle m_srv;

    UINT        m_width  = 0;
    UINT        m_height = 0;
    DXGI_FORMAT m_colorFormat;
    DXGI_FORMAT m_depthFormat;
    float       m_clearColor[4];
};
