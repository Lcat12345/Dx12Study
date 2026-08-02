// Renderer.h : draws a list of DrawItems. Everything D3D12 lives behind this.
#pragma once

#include "Graphics/GraphicsDevice.h"
#include "Graphics/SwapChain.h"
#include "Graphics/FrameResource.h"
#include "Graphics/DescriptorAllocator.h"
#include "Graphics/DepthTarget.h"
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
    uint64_t SceneTextureId() const { return m_sceneColor->SRV().ptr; }

    // The aspect the scene was last drawn with. Picking has to unproject
    // through the same projection the renderer built, so it needs this
    // rather than the window's shape.
    float SceneAspectRatio() const { return m_sceneColor->AspectRatio(); }

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
    // What a pipeline state is FOR. Phase 11 adds passes that draw the same
    // geometry with different state; naming the role keeps "which PSO" from
    // becoming a comment.
    //
    // Skybox, Transparent and ShadowDepth have no shaders yet - each arrives
    // with the step that needs it, and until then its slot stays null.
    enum class PsoRole
    {
        Opaque,
        Skybox,
        Transparent,
        ShadowDepth,
        Count
    };

    void CreateCommandObjects();
    void CreateConstantBuffers();
    void CreateRootSignature();
    void CreatePipelineStates();

    // The settings every scene PSO shares: input layout, root signature,
    // topology, and the scene target's formats. Each role starts from this
    // and changes only what makes it that role - so a format mismatch is one
    // fix rather than four.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC SceneShadedPsoTemplate() const;

    void UpdatePassConstants(FrameResource& frame, const CameraView& camera,
                             const LightingData& lighting);
    void UpdateObjectConstants(FrameResource& frame,
                               const std::vector<DrawItem>& items);

    // --- the passes, in the order they run ---
    // Written out rather than hidden behind a render graph: at this many
    // passes the order IS the documentation, and each one owns its own
    // barriers.
    void DrawOpaquePass(FrameResource& frame, const std::vector<DrawItem>& items);
    // After opaque so it only fills pixels nothing has claimed, and before
    // transparent (11.6) so alpha has a background to blend against.
    void DrawSkyboxPass(FrameResource& frame, CubeTextureHandle skybox);
    void DrawOverlayPass();

    // Shared setup every geometry pass needs, so adding a pass does not mean
    // copying six bind calls.
    void BindScenePass(FrameResource& frame, PsoRole role);
    void DrawItems(FrameResource& frame, const std::vector<DrawItem>& items);

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

    // The scene is drawn here, never straight to the back buffer. Colour and
    // depth are separate objects that this pass happens to combine.
    std::unique_ptr<RenderTarget> m_sceneColor;
    std::unique_ptr<DepthTarget>  m_sceneDepth;
    UINT m_requestedViewportWidth  = 0;
    UINT m_requestedViewportHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    // The per-frame sets, used round-robin. m_currentFrame indexes these and
    // is NOT the same as the swap chain's back buffer index: one is our own
    // ring, the other is chosen by the swap chain.
    FrameResource m_frames[kFramesInFlight];
    UINT          m_currentFrame = 0;

    // Shared by the scene passes for as long as they need the same inputs.
    // Not a rule: a pass whose contract genuinely differs - shadow depth
    // wants no textures at all - may bring its own rather than pad this one.
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_sceneRootSignature;

    // Indexed by PsoRole. Null until the step that introduces that pass.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStates[size_t(PsoRole::Count)];

    // Declared after the allocators it borrows a slot from, so it is
    // destroyed before them.
    ResourceManager m_resources;

    // The box the skybox is drawn on - the same procedural cube any entity
    // could use. Resolved once because every frame with a sky needs it.
    MeshHandle m_skyboxMesh;

    // Declared last: it is torn down before the device and heaps it uses.
    std::unique_ptr<ImGuiLayer> m_overlay;

    bool m_vsync = true;
};
