// Renderer.h : draws a Scene. Everything D3D12 lives behind this.
#pragma once

#include "Graphics/GraphicsDevice.h"
#include "Graphics/SwapChain.h"
#include "Graphics/FrameResource.h"
#include "Graphics/DescriptorAllocator.h"
#include "Graphics/ResourceManager.h"

#include <d3d12.h>
#include <wrl/client.h>

struct Scene;
struct Camera;

class Renderer
{
public:
    Renderer(HWND hwnd, UINT width, UINT height);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Meshes and textures are created against this device (see BuildScene).
    ID3D12Device* Device() const { return m_device.Device(); }

    // The shader-visible heap other systems draw slots from. ImGui takes one
    // for its font atlas in 9.6; it is deliberately larger than we need.
    DescriptorAllocator& ShaderVisibleDescriptors() { return m_srvAllocator; }

    // Assets are loaded and cached here (see BuildScene).
    ResourceManager& Resources() { return m_resources; }

    void Resize(UINT width, UINT height);
    void Render(const Scene& scene, const Camera& camera, float totalSeconds);

    // Vsync off only actually uncaps the frame rate when the display path
    // supports tearing.
    void SetVSync(bool enabled)  { m_vsync = enabled; }
    bool IsVSync() const         { return m_vsync; }
    bool IsTearingSupported() const { return m_device.IsTearingSupported(); }

private:
    void CreateCommandObjects();
    void CreateSizeDependentResources(); // depth buffer, viewport, scissor
    void CreateConstantBuffers();
    void CreateRootSignature();
    void CreatePipelineState();

    void UpdatePassConstants(FrameResource& frame, const Camera& camera,
                             float totalSeconds);
    void UpdateObjectConstants(FrameResource& frame, const Scene& scene,
                               float totalSeconds);

    // Declaration order IS destruction order, reversed: the device is first
    // so everything created from it dies before it does.
    GraphicsDevice m_device;
    SwapChain      m_swapChain;

    // Depth needs exactly one slot. The shader-visible heap gets room to
    // spare so later systems can Allocate() without resizing anything.
    static constexpr UINT kDsvHeapCapacity = 1;
    static constexpr UINT kSrvHeapCapacity = 16;

    DescriptorAllocator m_dsvAllocator;
    DescriptorAllocator m_srvAllocator;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // The per-frame sets, used round-robin. m_currentFrame indexes these and
    // is NOT the same as the swap chain's back buffer index: one is our own
    // ring, the other is chosen by the swap chain.
    FrameResource m_frames[kFramesInFlight];
    UINT          m_currentFrame = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencilBuffer;
    DescriptorHandle                       m_depthStencilView;

    // Declared after the allocators it borrows a slot from, so it is
    // destroyed before them.
    ResourceManager m_resources;

    D3D12_VIEWPORT m_viewport    = {};
    D3D12_RECT     m_scissorRect = {};

    bool m_vsync = true;
};
