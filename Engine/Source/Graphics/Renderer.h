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
#include "Core/RuntimePaths.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

class Renderer
{
public:
    struct FrameStats
    {
        uint64_t drawCalls        = 0;
        uint64_t rootCbvBinds     = 0;
        uint64_t mainVisible      = 0;
        uint64_t shadowVisible    = 0;
        uint64_t submittedItems   = 0;
        UINT     objectCapacity   = 0;
        UINT     srvUsed           = 0;
        UINT     srvCapacity       = 0;
        UINT     renderWidth       = 0;
        UINT     renderHeight      = 0;
    };

    enum class SceneOutput
    {
        OffscreenTexture,
        SwapChain
    };

    using OverlayRecorder = std::function<void(ID3D12GraphicsCommandList*)>;

    Renderer(HWND hwnd, UINT width, UINT height, const RuntimePaths& runtimePaths,
             GraphicsDevice::AdapterPolicy adapterPolicy =
                 GraphicsDevice::AdapterPolicy::HardwareOnly);
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Meshes and textures are created against this device (see BuildWorld).
    ID3D12Device* Device() const { return m_device.Device(); }

    // Generic graphics services used by optional host-owned rendering layers.
    ID3D12CommandQueue* CommandQueue() const { return m_device.Queue(); }
    DescriptorAllocator& ShaderVisibleDescriptors() { return m_srvAllocator; }
    static constexpr DXGI_FORMAT OutputFormat() { return SwapChain::kFormat; }
    static constexpr int FramesInFlight() { return int(kFramesInFlight); }

    // Assets are loaded and cached here (see BuildWorld).
    ResourceManager& Resources() { return m_resources; }

    // A host-owned GPU layer must drain queued work before destroying its
    // resources. Normal frame rendering remains asynchronous.
    void WaitForGpu() { m_device.WaitForGpu(); }

    // The window changed size. Only the swap chain cares - the scene now
    // lives in its own target, sized by the viewport panel instead.
    void Resize(UINT width, UINT height);

    // --- the scene viewport ---
    // The editor asks for a size; the renderer applies it at the top of the
    // next frame, once the GPU is known to be finished with the old texture.
    void SetSceneViewportSize(UINT width, UINT height);

    // The offscreen output texture as a plain descriptor-handle integer.
    uint64_t SceneTextureId() const { return m_sceneColor->SRV().ptr; }

    // The aspect the scene was last drawn with. Picking has to unproject
    // through the same projection the renderer built, so it needs this
    // rather than the window's shape.
    float SceneAspectRatio() const { return m_sceneColor->AspectRatio(); }

    // The shadow map, for the editor's debug view. A depth texture read as
    // R32_FLOAT, so it displays as near-white: most of the map is the far
    // plane (1.0) and casters only pull it down by their share of the light's
    // depth range. Faint is CORRECT here, not a bug.
    uint64_t ShadowTextureId() const { return m_shadowMap->SRV().ptr; }
    UINT     ShadowMapSize()   const { return m_shadowMap->Width(); }

    // The sphere the light's orthographic volume was sized to this frame.
    // Worth showing next to the map: a radius that has jumped is the usual
    // reason a shadow suddenly goes blocky, and it is not visible from the
    // image alone.
    DirectX::XMFLOAT3 ShadowSceneCenter() const { return m_shadowSceneCenter; }
    float             ShadowSceneRadius() const { return m_shadowSceneRadius; }

    // How many DrawItems Render() will accept before throwing. The editor
    // shows it next to the live count so the ceiling is visible before it
    // is hit.
    UINT MaxDrawItems() const { return kMaxObjects; }
    static constexpr UINT InitialObjectCapacity() { return kMaxObjects; }
    static constexpr bool ExceedsInitialObjectCapacity(size_t drawItemCount)
    {
        return drawItemCount > kMaxObjects;
    }

    // Takes flattened data, not a scene graph. Scene passes are shared by
    // both outputs; only the final presentation path differs. The optional
    // recorder is a host extension point and may be empty.
    void RenderFrame(const CameraView& camera, const LightingData& lighting,
                     const std::vector<DrawItem>& items, SceneOutput output,
                     const OverlayRecorder& overlayRecorder = {});

    // Vsync off only actually uncaps the frame rate when the display path
    // supports tearing.
    void SetVSync(bool enabled)  { m_vsync = enabled; }
    bool IsVSync() const         { return m_vsync; }
    bool IsTearingSupported() const { return m_device.IsTearingSupported(); }

    // 4x is enabled by default when both scene colour and depth formats
    // support it. A request on unsupported hardware remains safely at 1x.
    void SetMsaaEnabled(bool enabled);
    bool IsMsaaEnabled() const    { return m_msaaEnabled; }
    bool Is4xMsaaSupported() const { return m_4xMsaaSupported; }
    UINT MsaaSampleCount() const { return m_msaaEnabled ? 4u : 1u; }
    const std::wstring& AdapterName() const { return m_device.AdapterName(); }
    const FrameStats& LastFrameStats() const { return m_lastFrameStats; }

    // Debug layer message count, for the stats panel. See GraphicsDevice.
    uint64_t DebugMessageCount() const { return m_device.DebugMessageCount(); }
    bool     HasDebugLayer()     const { return m_device.HasDebugLayer(); }

private:
    struct SceneAttachments
    {
        ID3D12Resource* color = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
        D3D12_VIEWPORT viewport = {};
        D3D12_RECT scissor = {};
        UINT width = 1;
        UINT height = 1;
        D3D12_RESOURCE_STATES restingState = D3D12_RESOURCE_STATE_COMMON;
    };

    // What a pipeline state is FOR. Phase 11 adds passes that draw the same
    // geometry with different state; naming the role keeps "which PSO" from
    // becoming a comment.
    //
    // Each role is an immutable bundle of the state its pass needs.
    enum class PsoRole
    {
        Opaque,
        Skybox,
        Transparent,
        ShadowDepth,
        Count
    };

    using DrawQueue = std::vector<size_t>;

    void CreateCommandObjects();
    void CreateConstantBuffers();
    void CreateRootSignature();
    void CreatePipelineStates();
    void QueryMsaaSupport();

    // The settings every scene PSO shares: input layout, root signature,
    // topology, and the scene target's formats. Each role starts from this
    // and changes only what makes it that role - so a format mismatch is one
    // fix rather than four.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC SceneShadedPsoTemplate(
        UINT sampleCount, UINT sampleQuality) const;

    void UpdatePassConstants(FrameResource& frame, const CameraView& camera,
                             const LightingData& lighting,
                             const std::vector<DrawItem>& items,
                             const DrawQueue& shadowCasters,
                             float renderAspect);
    void UpdateObjectConstants(FrameResource& frame,
                               const std::vector<DrawItem>& items);

    // The world-space extent of everything that casts a shadow, as a sphere.
    // False when there is nothing to bound. Uses the ResourceManager the
    // renderer already holds. The DrawItem centre added in 11.6 is the
    // transparent sort key, not a replacement for all eight corners here.
    bool ComputeSceneBounds(const std::vector<DrawItem>& items,
                            const DrawQueue& itemIndices,
                            DirectX::XMFLOAT3& outCenter, float& outRadius) const;
    DirectX::XMMATRIX ComputeShadowViewProj(const LightingData& lighting,
                                            const DirectX::XMFLOAT3& center,
                                            float radius) const;

    // --- the passes, in the order they run ---
    // Written out rather than hidden behind a render graph: at this many
    // passes the order IS the documentation, and each one owns its own
    // barriers.
    // First: the light's own depth-only view of the casters, into a target
    // that has nothing to do with the scene viewport.
    void BuildDrawQueues(const CameraView& camera,
                         const std::vector<DrawItem>& items,
                         DrawQueue& outOpaque, DrawQueue& outTransparent) const;
    void DrawShadowDepthPass(FrameResource& frame,
                             const std::vector<DrawItem>& items,
                             const DrawQueue& opaqueItems);
    void DrawOpaquePass(FrameResource& frame, const SceneAttachments& target,
                        const std::vector<DrawItem>& items,
                        const DrawQueue& opaqueItems);
    // After opaque so it only fills pixels nothing has claimed, and before
    // transparent (11.6) so alpha has a background to blend against.
    void DrawSkyboxPass(FrameResource& frame, const SceneAttachments& target,
                        CubeTextureHandle skybox);
    void DrawTransparentPass(FrameResource& frame, const SceneAttachments& target,
                             const std::vector<DrawItem>& items,
                             const DrawQueue& transparentItems);
    SceneAttachments GetSceneAttachments(SceneOutput output) const;
    void RecordScenePasses(FrameResource& frame, const SceneAttachments& target,
                           const LightingData& lighting,
                           const std::vector<DrawItem>& items,
                           const DrawQueue& opaqueItems,
                           const DrawQueue& transparentItems);
    void PresentOffscreen(const OverlayRecorder& overlayRecorder);
    void PresentSwapChain(const SceneAttachments& target,
                          const OverlayRecorder& overlayRecorder);
    void RecordBackBufferOverlay(const OverlayRecorder& overlayRecorder,
                                 bool clearBackBuffer);

    // Shared setup every geometry pass needs, so adding a pass does not mean
    // copying six bind calls.
    void BindScenePass(FrameResource& frame, const SceneAttachments& target,
                       PsoRole role);
    void DrawItems(FrameResource& frame, const std::vector<DrawItem>& items,
                   const DrawQueue& drawQueue);

    // Declaration order IS destruction order, reversed: the device is first
    // so everything created from it dies before it does.
    GraphicsDevice m_device;
    SwapChain      m_swapChain;

    // Room to spare for the offscreen and Player presentation targets.
    static constexpr UINT kRtvHeapCapacity = 8;
    static constexpr UINT kDsvHeapCapacity = 8;
    // Host layers, scene textures, and loaded textures share this heap.
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

    // Swap-chain output owns window-sized depth in both modes. In 4x it also
    // owns the multisample colour source resolved into the current back buffer;
    // in 1x the back buffer itself is the scene colour target.
    std::unique_ptr<RenderTarget> m_swapChainMsaaColor;
    std::unique_ptr<DepthTarget>  m_swapChainDepth;

    // Fixed size, unlike the scene target: this is the resolution the LIGHT
    // samples the world at, and has nothing to do with the window.
    std::unique_ptr<DepthTarget> m_shadowMap;
    // Which state the shadow map was left in. Needed because it is created in
    // DEPTH_WRITE but every frame after the first leaves it readable, and a
    // barrier naming a state the resource is not in is an error.
    bool m_shadowMapIsShaderResource = false;
    // Whether this frame's pass constants hold a real light frustum. Decided
    // while writing them, used by the pass, so the two cannot disagree.
    bool m_shadowCastersExist = false;
    DirectX::XMFLOAT3 m_shadowSceneCenter = { 0.0f, 0.0f, 0.0f };
    float             m_shadowSceneRadius = 0.0f;

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

    // Scene roles have immutable 1x and 4x variants because SampleDesc is
    // baked into a PSO. ShadowDepth lives only in the 1x row: the shadow map
    // deliberately remains single-sample.
    static constexpr size_t kSceneSampleVariantCount = 2;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>
        m_pipelineStates[kSceneSampleVariantCount][size_t(PsoRole::Count)];

    bool m_4xMsaaSupported = false;
    bool m_msaaEnabled      = false;
    UINT m_4xMsaaQuality    = 0;

    // Declared after the allocators it borrows a slot from, so it is
    // destroyed before them.
    ResourceManager m_resources;

    // The box the skybox is drawn on - the same procedural cube any entity
    // could use. Resolved once because every frame with a sky needs it.
    MeshHandle m_skyboxMesh;

    bool m_vsync = true;
    FrameStats m_lastFrameStats;
};
