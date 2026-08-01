// Renderer.h : draws a list of DrawItems. Everything D3D12 lives behind this.
#pragma once

#include "Graphics/GraphicsDevice.h"
#include "Graphics/SwapChain.h"
#include "Graphics/FrameResource.h"
#include "Graphics/DescriptorAllocator.h"
#include "Graphics/RenderTarget.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/RenderData.h"
#include "Graphics/ImGuiLayer.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

class Renderer
{
public:
    Renderer(HWND hwnd, UINT width, UINT height);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Meshes and textures are created against this device (see BuildWorld).
    ID3D12Device* Device() const { return m_device.Device(); }

    // The shader-visible heap other systems draw slots from. ImGui takes one
    // for its font atlas; it is deliberately larger than we need.
    DescriptorAllocator& ShaderVisibleDescriptors() { return m_srvAllocator; }

    // Assets are loaded and cached here (see BuildWorld).
    ResourceManager& Resources() { return m_resources; }

    // The debug overlay, created separately because it needs the window
    // handle and must be torn down before the device.
    void        InitializeOverlay(HWND hwnd);
    ImGuiLayer* Overlay() { return m_overlay.get(); }

    // The window changed size. Only the swap chain cares - the scene now
    // lives in its own target, sized by the viewport panel instead.
    void Resize(UINT width, UINT height);

    // --- the scene viewport ---
    // The editor asks for a size; the renderer applies it at the top of the
    // next frame, once the GPU is known to be finished with the old texture.
    void SetSceneViewportSize(UINT width, UINT height);

    // The scene texture, as ImGui wants it. Returned as a plain integer so
    // no D3D type leaks into the UI code.
    uint64_t SceneTextureId() const { return m_sceneTarget->SRV().ptr; }

    // How many DrawItems Render() will accept before throwing. The editor
    // shows it next to the live count so the ceiling is visible before it
    // is hit.
    UINT MaxDrawItems() const { return kMaxObjects; }

    // Takes flattened data, not a scene graph - the renderer has no idea
    // entities exist. See RenderData.h.
    void Render(const CameraView& camera, const LightingData& lighting,
                const std::vector<DrawItem>& items);

    // Vsync off only actually uncaps the frame rate when the display path
    // supports tearing.
    void SetVSync(bool enabled)  { m_vsync = enabled; }
    bool IsVSync() const         { return m_vsync; }
    bool IsTearingSupported() const { return m_device.IsTearingSupported(); }

private:
    void CreateCommandObjects();
    void CreateConstantBuffers();
    void CreateRootSignature();
    void CreatePipelineState();

    void UpdatePassConstants(FrameResource& frame, const CameraView& camera,
                             const LightingData& lighting);
    void UpdateObjectConstants(FrameResource& frame,
                               const std::vector<DrawItem>& items);

    void DrawScene(FrameResource& frame, const std::vector<DrawItem>& items);
    void DrawOverlay();

    // Declaration order IS destruction order, reversed: the device is first
    // so everything created from it dies before it does.
    GraphicsDevice m_device;
    SwapChain      m_swapChain;

    // Room to spare: more render targets arrive with shadow mapping.
    static constexpr UINT kRtvHeapCapacity = 8;
    static constexpr UINT kDsvHeapCapacity = 8;
    // ImGui's font atlas, the scene texture, and every loaded texture share
    // this heap.
    static constexpr UINT kSrvHeapCapacity = 64;

    DescriptorAllocator m_rtvAllocator;
    DescriptorAllocator m_dsvAllocator;
    DescriptorAllocator m_srvAllocator;

    // The scene is drawn here, never straight to the back buffer.
    std::unique_ptr<RenderTarget> m_sceneTarget;
    UINT m_requestedViewportWidth  = 0;
    UINT m_requestedViewportHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // The per-frame sets, used round-robin. m_currentFrame indexes these and
    // is NOT the same as the swap chain's back buffer index: one is our own
    // ring, the other is chosen by the swap chain.
    FrameResource m_frames[kFramesInFlight];
    UINT          m_currentFrame = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;

    // Declared after the allocators it borrows a slot from, so it is
    // destroyed before them.
    ResourceManager m_resources;

    // Declared last: it is torn down before the device and heaps it uses.
    std::unique_ptr<ImGuiLayer> m_overlay;

    bool m_vsync = true;
};
