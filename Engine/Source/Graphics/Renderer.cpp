#include "Graphics/Renderer.h"

#include "Core/Common.h"
#include "Graphics/Mesh.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    // What the scene is cleared to, inside the viewport.
    constexpr float kSceneClearColor[4] = { 0.02f, 0.04f, 0.08f, 1.0f };
    // What is behind the panels. Deliberately a different, lighter grey: it
    // makes the boundary of the scene viewport obvious at a glance.
    constexpr float kEditorClearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };

    // The scene texture matches the back buffer's format so one PSO serves
    // both - nothing here needs HDR yet.
    constexpr DXGI_FORMAT kSceneColorFormat = SwapChain::kFormat;
    // What a PSO must declare to draw into the scene's depth. DepthTarget
    // owns the resource format; this is the VIEW format, and the two differ
    // once a depth buffer becomes sampleable.
    constexpr DXGI_FORMAT kSceneDepthViewFormat = DXGI_FORMAT_D32_FLOAT;

    // The shadow map's size is FIXED, unlike the scene target which follows
    // the viewport panel. It has nothing to do with how big the window is -
    // it is the resolution the light samples the world at, and tying it to a
    // panel the user drags would make shadow quality wobble as they resize.
    constexpr UINT kShadowMapSize = 2048;

    // The caster-side half of shadow bias. Around depth 0.5, D32_FLOAT turns
    // the constant 1000 into only about 6e-5 normalized depth, versus the
    // receiver bias's 0.001 default. It is a small baseline; the slope-scaled
    // term below is what chiefly counters self-shadowing on angled polygons.
    constexpr INT   kShadowRasterDepthBias       = 1000;
    constexpr float kShadowRasterSlopeScaledBias = 1.0f;

    // Never smaller than this, so a scene holding one tiny object (or nothing
    // but a point) still gets an orthographic volume with real width and a
    // near/far spread rather than a division by zero.
    constexpr float kMinShadowRadius = 1.0f;
}

Renderer::Renderer(HWND hwnd, UINT width, UINT height,
                   const RuntimePaths& runtimePaths,
                   GraphicsDevice::AdapterPolicy adapterPolicy)
    : m_device(adapterPolicy)                     // debug layer -> device -> queue
    , m_swapChain(m_device, hwnd, width, height)  // needs the queue
    , m_rtvAllocator(m_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                     kRtvHeapCapacity, /*shaderVisible*/ false)
    , m_dsvAllocator(m_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                     kDsvHeapCapacity, /*shaderVisible*/ false)
    , m_srvAllocator(m_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                     kSrvHeapCapacity, /*shaderVisible*/ true)
    , m_resources(m_device, m_srvAllocator, runtimePaths)
{
    CreateCommandObjects();
    QueryMsaaSupport();
    // Prefer the quality path this step introduces. Unsupported adapters
    // simply keep these at 1x/quality 0 and expose that fact to the UI.
    m_msaaEnabled = m_4xMsaaSupported;
    const UINT sceneSampleCount   = m_msaaEnabled ? 4u : 1u;
    const UINT sceneSampleQuality = m_msaaEnabled ? m_4xMsaaQuality : 0u;

    // The scene has its own surface now, so it no longer shares the window's
    // size. Starting at the client size just avoids a resize on frame one -
    // the viewport panel takes over as soon as it reports its size.
    //
    // Colour and depth are built separately and used together. Passing no
    // SRV allocator for the depth says "never sampled", which is true for
    // the scene's own depth and false for the shadow map that arrives in
    // 11.4.
    m_sceneColor = std::make_unique<RenderTarget>(
        m_device, m_rtvAllocator, &m_srvAllocator,
        width, height, kSceneColorFormat, kSceneClearColor,
        sceneSampleCount, sceneSampleQuality);
    m_sceneDepth = std::make_unique<DepthTarget>(
        m_device, m_dsvAllocator, /*srvAllocator*/ nullptr, width, height,
        sceneSampleCount, sceneSampleQuality);

    // Swap-chain presentation has a separate lifetime from the editor's
    // docked viewport. The colour source is used only in 4x; at 1x the
    // current back buffer is the scene target itself.
    m_swapChainMsaaColor = std::make_unique<RenderTarget>(
        m_device, m_rtvAllocator, /*srvAllocator*/ nullptr,
        width, height, kSceneColorFormat, kSceneClearColor,
        sceneSampleCount, sceneSampleQuality);
    m_swapChainDepth = std::make_unique<DepthTarget>(
        m_device, m_dsvAllocator, /*srvAllocator*/ nullptr, width, height,
        sceneSampleCount, sceneSampleQuality);

    // The shadow map is the OTHER case DepthTarget was built for: written as
    // depth in one pass, sampled as a texture afterwards. Passing an SRV
    // allocator is what switches its resource to typeless and adds the second
    // view - the whole reason 11.1 made that an option instead of a second
    // class.
    m_shadowMap = std::make_unique<DepthTarget>(
        m_device, m_dsvAllocator, &m_srvAllocator, kShadowMapSize, kShadowMapSize);

    CreateConstantBuffers();
    CreateRootSignature();
    CreatePipelineStates();
}

Renderer::~Renderer()
{
    // Never destroy anything the GPU might still be reading.
    m_device.WaitForGpu();
}

// Allocator = command memory, list = recorder, queue = where lists go.
void Renderer::CreateCommandObjects()
{
    // One allocator per frame in flight. The LIST is shared - it holds no
    // memory of its own, it just records into whichever allocator it was
    // Reset with. Only the allocator's memory is the contended resource.
    for (FrameResource& frame : m_frames)
    {
        ThrowIfFailed(m_device.Device()->CreateCommandAllocator(
                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                          IID_PPV_ARGS(&frame.commandAllocator)),
                      "CreateCommandAllocator");
    }

    ThrowIfFailed(m_device.Device()->CreateCommandList(
                      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                      m_frames[0].commandAllocator.Get(), nullptr,
                      IID_PPV_ARGS(&m_commandList)),
                  "CreateCommandList");

    // Lists are born recording, but every frame starts with Reset(), which
    // requires a CLOSED list.
    ThrowIfFailed(m_commandList->Close(), "Close command list");
}

// Two constant buffers, one per update frequency - duplicated per frame.
void Renderer::CreateConstantBuffers()
{
    // Forgetting the pass CB here is an easy mistake: it is written once per
    // frame rather than once per object, but the GPU reads it just as long.
    D3D12_RANGE readRange = {};
    for (FrameResource& frame : m_frames)
    {
        frame.objectCB = CreateUploadBuffer(m_device.Device(), nullptr,
                                            UINT64(kObjectCBSize) * kMaxObjects,
                                            "CreateCommittedResource(ObjectCB)");
        // Map once and keep the pointer: Map/Unmap every frame would be pure
        // overhead for an upload-heap resource.
        ThrowIfFailed(frame.objectCB->Map(
                          0, &readRange,
                          reinterpret_cast<void**>(&frame.objectCBMapped)),
                      "ObjectCB Map");

        frame.passCB = CreateUploadBuffer(m_device.Device(), nullptr, kPassCBSize,
                                          "CreateCommittedResource(PassCB)");
        ThrowIfFailed(frame.passCB->Map(
                          0, &readRange,
                          reinterpret_cast<void**>(&frame.passCBMapped)),
                      "PassCB Map");
    }
}

// The "function signature" of the pipeline:
// b0 object CB, b1 pass CB, t0 diffuse, t1 normal map, t2 shadow map,
// s0 material sampler, s1 shadow comparison sampler.
void Renderer::CreateRootSignature()
{
    // TWO tables of one descriptor each, not one table of two.
    //
    // A descriptor table names a CONTIGUOUS run of the heap, and a material's
    // diffuse and normal map are two independent textures that the allocator
    // handed whatever slots were free when each was loaded. They are almost
    // never neighbours. One table of two would therefore bind the diffuse
    // plus whatever unrelated texture happens to sit after it.
    //
    // The alternative is copying both descriptors into a contiguous scratch
    // region every draw. That is what a bigger renderer does when the count
    // grows; at two textures the extra root parameter is far cheaper.
    D3D12_DESCRIPTOR_RANGE diffuseRange = {};
    diffuseRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    diffuseRange.NumDescriptors     = 1;
    diffuseRange.BaseShaderRegister = 0; // t0

    D3D12_DESCRIPTOR_RANGE normalRange = {};
    normalRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    normalRange.NumDescriptors     = 1;
    normalRange.BaseShaderRegister = 1; // t1

    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors     = 1;
    shadowRange.BaseShaderRegister = 2; // t2

    // Both CBs are visible to ALL stages: the VS needs the matrices, the PS
    // needs the material and the lights.
    D3D12_ROOT_PARAMETER rootParams[5] = {};
    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0; // b0 - per object
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1; // b1 - per frame
    rootParams[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges   = &diffuseRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges   = &normalRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Global for the pass, unlike the two material textures above: every
    // object asks the same light-depth texture whether it is visible.
    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges   = &shadowRange;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // A static sampler lives in the root signature, not in a heap - the
    // common case, since most samplers never change at runtime.
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // floor tiles
    samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister   = 0; // s0
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // A comparison sampler returns visibility directly. Border white means
    // a PCF tap outside the light's texture is lit rather than wrapping to
    // unrelated depth on the opposite edge.
    // The shader places nine taps in a 3x3 kernel; linear comparison also
    // blends the four neighbouring comparisons at each tap, avoiding the
    // visibly quantized edge that point comparison left on the large floor.
    samplers[1].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister   = 1; // s1
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = _countof(rootParams);
    desc.pParameters       = rootParams;
    desc.NumStaticSamplers = _countof(samplers);
    desc.pStaticSamplers   = samplers;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Root signatures are handed to the driver serialized, as a blob.
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &blob, &errors);
    if (FAILED(hr))
    {
        throw std::runtime_error(
            errors ? static_cast<const char*>(errors->GetBufferPointer())
                   : "D3D12SerializeRootSignature failed");
    }
    ThrowIfFailed(m_device.Device()->CreateRootSignature(0, blob->GetBufferPointer(),
                                                         blob->GetBufferSize(),
                                                         IID_PPV_ARGS(&m_sceneRootSignature)),
                  "CreateRootSignature");
}

// The state every shaded scene pass agrees on. A role then changes only the
// few fields that make it that role.
//
// The formats are the part worth centralising: RTVFormats, DSVFormat and
// SampleDesc must match the attachments the pass actually binds, and a
// mismatch is a Debug Layer error or a silently skipped draw. With one
// template that is one place to be right instead of one per pass.
D3D12_GRAPHICS_PIPELINE_STATE_DESC Renderer::SceneShadedPsoTemplate(
    UINT sampleCount, UINT sampleQuality) const
{
    // Offsets must match struct Vertex in Mesh.h exactly. Mismatches produce
    // no error - just a wrong picture.
    static const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_sceneRootSignature.Get();
    desc.SampleMask     = UINT_MAX;

    desc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = FALSE; // clockwise = front
    desc.RasterizerState.DepthClipEnable       = TRUE;

    desc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    desc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // LESS = keep the fragment closer to the camera.
    desc.DepthStencilState.DepthEnable    = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    desc.DepthStencilState.StencilEnable  = FALSE;

    desc.InputLayout           = { kInputLayout, _countof(kInputLayout) };
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // The scene target's shape, not the window's.
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0]    = kSceneColorFormat;
    desc.DSVFormat        = kSceneDepthViewFormat;
    desc.SampleDesc.Count   = sampleCount;
    desc.SampleDesc.Quality = sampleQuality;

    return desc;
}

// The world-space box every shadow caster fits inside.
//
// Built from the meshes' LOCAL boxes pushed through each item's world matrix,
// which is why this needs no new DrawItem field: the renderer already holds
// the ResourceManager, so GetMesh(item.mesh).bounds is right there. A local
// box transformed by a matrix is not a box, so all EIGHT corners go through
// and the result is the box around those - loose for a rotated object, but
// never too small, which is the direction that matters for a shadow volume.
//
// Returns false for an empty scene: no casters means no sensible light
// frustum, and the caller skips the pass rather than inventing one.
bool Renderer::ComputeSceneBounds(const std::vector<DrawItem>& items,
                                  const DrawQueue& itemIndices,
                                  XMFLOAT3& outCenter, float& outRadius) const
{
    XMVECTOR minCorner = XMVectorReplicate( FLT_MAX);
    XMVECTOR maxCorner = XMVectorReplicate(-FLT_MAX);
    bool any = false;

    for (const size_t itemIndex : itemIndices)
    {
        const DrawItem& item = items[itemIndex];
        if (!item.mesh.IsValid())
        {
            continue;
        }
        const Aabb& local = m_resources.GetMesh(item.mesh).bounds;
        if (local.IsEmpty())
        {
            continue;
        }

        const XMMATRIX world = XMLoadFloat4x4(&item.world);
        for (int corner = 0; corner < 8; ++corner)
        {
            // Bit 0/1/2 pick min or max on x/y/z - the eight combinations.
            const XMVECTOR localPoint = XMVectorSet(
                (corner & 1) ? local.max.x : local.min.x,
                (corner & 2) ? local.max.y : local.min.y,
                (corner & 4) ? local.max.z : local.min.z,
                1.0f);
            const XMVECTOR worldPoint = XMVector3TransformCoord(localPoint, world);
            minCorner = XMVectorMin(minCorner, worldPoint);
            maxCorner = XMVectorMax(maxCorner, worldPoint);
            any = true;
        }
    }

    if (!any)
    {
        return false;
    }

    const XMVECTOR center = XMVectorScale(XMVectorAdd(minCorner, maxCorner), 0.5f);
    XMStoreFloat3(&outCenter, center);

    // A bounding SPHERE, not the box's extents, on purpose: its size does not
    // depend on which way the light happens to face, so the orthographic
    // volume stays the same as the sun rotates. Sizing to the box in light
    // space would be tighter but would breathe every frame, and a shadow map
    // that changes scale between frames crawls visibly along every edge.
    const float radius = 0.5f * XMVectorGetX(
        XMVector3Length(XMVectorSubtract(maxCorner, minCorner)));
    outRadius = (std::max)(radius, kMinShadowRadius);
    return true;
}

// Where the directional light looks from, and through what volume.
//
// "Directional" means the rays are parallel and only the DIRECTION matters -
// there is no real position to put the camera at, so one is invented far
// enough back that the whole scene is in front of it.
XMMATRIX Renderer::ComputeShadowViewProj(const LightingData& lighting,
                                         const XMFLOAT3& center, float radius) const
{
    XMVECTOR direction = XMLoadFloat3(&lighting.directionalDirection);
    // A zeroed direction would make LookToLH produce NaNs that then poison
    // every vertex the shadow VS touches.
    if (XMVectorGetX(XMVector3LengthSq(direction)) < 1e-12f)
    {
        direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
    }
    direction = XMVector3Normalize(direction);

    // LookToLH needs an up vector that is not parallel to the view direction,
    // and the sun pointing straight down - the single most likely setting -
    // is exactly parallel to world up. Swap axes when they get close rather
    // than when they are exactly equal, because the matrix degrades long
    // before the cross product reaches zero.
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const float    alignment = std::fabs(XMVectorGetX(XMVector3Dot(direction, worldUp)));
    const XMVECTOR up = (alignment > 0.99f)
                      ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
                      : worldUp;

    // Back the eye off along the light's own direction until the entire
    // bounding sphere is in front of it, plus a margin so nothing sits on the
    // near plane.
    const float    backoff = radius + 1.0f;
    const XMVECTOR eye = XMVectorSubtract(XMLoadFloat3(&center),
                                          XMVectorScale(direction, backoff));

    const XMMATRIX view = XMMatrixLookToLH(eye, direction, up);
    // Width and height cover the sphere exactly; near/far span from just in
    // front of the eye to just past the far side of it.
    const XMMATRIX proj = XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f,
                                                 0.1f, backoff + radius + 0.1f);
    return view * proj;
}

// Nearly every pipeline setting baked into ONE immutable object: shaders,
// vertex layout, rasterizer/blend/depth state, output formats. Validated
// once here, cheap to switch at runtime.
//
// Opaque, transparent, skybox and shadow depth all begin from the same
// contract; the point of the role table is that each difference stays a few
// explicit lines rather than becoming a separate copy of the whole PSO.
void Renderer::CreatePipelineStates()
{
    // Load once, then bake the same shader programs into immutable 1x and
    // 4x PSOs. Sample count/quality is pipeline state in D3D12, not a dynamic
    // setting that can be changed at draw time.
    const ShaderBytecode& vs    = m_resources.LoadShader(L"Basic.VS.cso");
    const ShaderBytecode& ps    = m_resources.LoadShader(L"Basic.PS.cso");
    const ShaderBytecode& skyVs = m_resources.LoadShader(L"Skybox.VS.cso");
    const ShaderBytecode& skyPs = m_resources.LoadShader(L"Skybox.PS.cso");

    auto createSceneVariants = [&](size_t variantIndex,
                                   UINT sampleCount, UINT sampleQuality) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC opaque =
            SceneShadedPsoTemplate(sampleCount, sampleQuality);
        opaque.VS = { vs.Data(), vs.Size() };
        opaque.PS = { ps.Data(), ps.Size() };

        ThrowIfFailed(m_device.Device()->CreateGraphicsPipelineState(
                          &opaque,
                          IID_PPV_ARGS(&m_pipelineStates[variantIndex]
                                                       [size_t(PsoRole::Opaque)])),
                      "CreateGraphicsPipelineState(Opaque)");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC transparent = opaque;
        D3D12_RENDER_TARGET_BLEND_DESC& blend = transparent.BlendState.RenderTarget[0];
        blend.BlendEnable    = TRUE;
        blend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp        = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha  = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        transparent.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        ThrowIfFailed(m_device.Device()->CreateGraphicsPipelineState(
                          &transparent,
                          IID_PPV_ARGS(&m_pipelineStates[variantIndex]
                                                       [size_t(PsoRole::Transparent)])),
                      "CreateGraphicsPipelineState(Transparent)");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skybox =
            SceneShadedPsoTemplate(sampleCount, sampleQuality);
        skybox.VS = { skyVs.Data(), skyVs.Size() };
        skybox.PS = { skyPs.Data(), skyPs.Size() };
        // The camera sees the cube's back faces. Its forced far-plane depth
        // must pass the clear value without overwriting scene depth.
        skybox.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        skybox.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        skybox.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        ThrowIfFailed(m_device.Device()->CreateGraphicsPipelineState(
                          &skybox,
                          IID_PPV_ARGS(&m_pipelineStates[variantIndex]
                                                       [size_t(PsoRole::Skybox)])),
                      "CreateGraphicsPipelineState(Skybox)");
    };

    createSceneVariants(/*1x row*/ 0, 1, 0);
    if (m_4xMsaaSupported)
    {
        createSceneVariants(/*4x row*/ 1, 4, m_4xMsaaQuality);
    }

    // --- shadow depth: the template with the COLOUR half removed ---
    const ShaderBytecode& shadowVs = m_resources.LoadShader(L"ShadowDepth.VS.cso");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow = SceneShadedPsoTemplate(1, 0);
    shadow.VS = { shadowVs.Data(), shadowVs.Size() };

    // NO pixel shader. Depth is written by the rasterizer whether or not one
    // runs, so a pass that only wants depth should not pay for one - and this
    // one has nothing to say about colour anyway.
    shadow.PS = {};

    // NumRenderTargets alone is not enough: the template left a real format
    // in RTVFormats[0], and a PSO declaring a format for a target it does not
    // have is a Debug Layer error rather than a harmless leftover.
    shadow.NumRenderTargets = 0;
    shadow.RTVFormats[0]    = DXGI_FORMAT_UNKNOWN;

    // Push caster depth slightly away from the light before it reaches the
    // map. The constant term supplies only the small baseline described
    // above; the slope-scaled term handles angled-polygon self-shadowing.
    // Scene tuning belongs to the larger adjustable receiver bias.
    shadow.RasterizerState.DepthBias            = kShadowRasterDepthBias;
    shadow.RasterizerState.SlopeScaledDepthBias = kShadowRasterSlopeScaledBias;

    ThrowIfFailed(m_device.Device()->CreateGraphicsPipelineState(
                      &shadow,
                      IID_PPV_ARGS(&m_pipelineStates[0]
                                                   [size_t(PsoRole::ShadowDepth)])),
                  "CreateGraphicsPipelineState(ShadowDepth)");

    m_skyboxMesh = m_resources.ResolveMesh(L"#cube");
}

// The only safe order:
//   1. wait for the GPU  2. release every reference to the old buffers
//   3. ResizeBuffers
// Skipping step 1 or 2 makes ResizeBuffers fail - the swap chain cannot free
// buffers anyone still holds.
//
// The offscreen scene follows the viewport panel. Only the attachments used
// for direct swap-chain output follow the window.
void Renderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    m_device.WaitForGpu();
    m_swapChain.Resize(width, height); // releases its own back buffer refs
    m_swapChainMsaaColor->Resize(width, height);
    m_swapChainDepth->Resize(width, height);
}

// Recorded, not applied: the UI runs before Render(), and tearing down a
// texture the GPU is still reading is exactly the bug this defers around.
// Render() applies it once it can prove the GPU is idle.
void Renderer::SetSceneViewportSize(UINT width, UINT height)
{
    m_requestedViewportWidth  = width;
    m_requestedViewportHeight = height;
}

void Renderer::SetMsaaEnabled(bool enabled)
{
    const bool requested = enabled && m_4xMsaaSupported;
    if (requested == m_msaaEnabled)
    {
        return;
    }

    // Unlike a per-frame choice, changing sample count replaces every scene
    // attachment. It is a rare editor setting, so drain once and keep the
    // lifetime rule obvious.
    m_device.WaitForGpu();
    m_msaaEnabled = requested;

    const UINT sampleCount   = m_msaaEnabled ? 4u : 1u;
    const UINT sampleQuality = m_msaaEnabled ? m_4xMsaaQuality : 0u;
    m_sceneColor->SetSampleDesc(sampleCount, sampleQuality);
    m_sceneDepth->SetSampleDesc(sampleCount, sampleQuality);
    m_swapChainMsaaColor->SetSampleDesc(sampleCount, sampleQuality);
    m_swapChainDepth->SetSampleDesc(sampleCount, sampleQuality);
}

// Written ONCE per frame: camera and lights are shared by every object.
// Every value here now arrives from the caller - the light positions and
// colours used to be hardcoded in this function.
void Renderer::UpdatePassConstants(FrameResource& frame, const CameraView& camera,
                                   const LightingData& lighting,
                                   const std::vector<DrawItem>& items,
                                   const DrawQueue& shadowCasters,
                                   float renderAspect)
{
    // Scene loading and the editor already validate these values, but keep
    // this renderer boundary defensive for programmatic LightingData callers.
    // NaN defeats both std::max and std::clamp, so reject it explicitly.
    const float shadowBias = std::isfinite(lighting.shadowBias)
                           ? (std::max)(lighting.shadowBias, 0.0f)
                           : 0.0f;
    const float shadowStrength = std::isfinite(lighting.shadowStrength)
                               ? (std::clamp)(lighting.shadowStrength, 0.0f, 1.0f)
                               : 0.0f;

    const XMVECTOR eye     = XMLoadFloat3(&camera.position);
    const XMVECTOR forward = XMVector3Normalize(XMLoadFloat3(&camera.forward));
    const XMVECTOR up      = XMLoadFloat3(&camera.up);

    // LookTo (not LookAt): we have a direction, not a target point. The view
    // matrix is the INVERSE of the camera's world transform - moving the
    // camera right shifts the whole world left.
    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);

    // The aspect ratio is deliberately NOT part of CameraView: it belongs to
    // the output selected by the host for this frame.
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(
        camera.fovY, renderAspect, camera.nearZ, camera.farZ);

    // The same view with the camera sitting at the origin. Dropping the
    // translation row is what pins the background to the camera: rotating
    // still changes what you see, walking does not.
    XMMATRIX skyView = view;
    skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    // The light's frustum has to be rebuilt every frame because it is derived
    // from where the casters ARE - a spinning crate changes the box. Computed
    // here, next to the camera matrices, so one function decides everything
    // the frame is rendered from.
    //
    // m_shadowCastersExist doubles as the pass's own "should I run" flag,
    // recorded rather than recomputed so the pass and the constant buffer can
    // never disagree about whether a light frustum was written.
    // Zero, not uninitialized: ComputeSceneBounds only WRITES outCenter when
    // it returns true, and this gets copied into m_shadowSceneCenter and
    // read back by the debug UI unconditionally below. An uninitialized
    // XMFLOAT3 there is stack garbage on screen for an empty scene - not
    // wrong maths, just never-written memory - and garbage bit patterns are
    // one of the few things a float can hold that legitimately prints as NaN.
    XMFLOAT3 sceneCenter = { 0.0f, 0.0f, 0.0f };
    float    sceneRadius = 0.0f;
    m_shadowCastersExist = lighting.shadowsEnabled && shadowStrength > 0.0f &&
                           ComputeSceneBounds(items, shadowCasters,
                                              sceneCenter, sceneRadius);
    const XMMATRIX shadowViewProj =
        m_shadowCastersExist ? ComputeShadowViewProj(lighting, sceneCenter, sceneRadius)
                             : XMMatrixIdentity();
    m_shadowSceneCenter = sceneCenter;
    m_shadowSceneRadius = m_shadowCastersExist ? sceneRadius : 0.0f;

    PassConstants constants;
    XMStoreFloat4x4(&constants.viewProj, XMMatrixTranspose(view * proj));
    XMStoreFloat4x4(&constants.skyViewProj, XMMatrixTranspose(skyView * proj));
    XMStoreFloat4x4(&constants.shadowViewProj, XMMatrixTranspose(shadowViewProj));
    constants.eyePosW = camera.position;

    constants.ambientLight     = lighting.ambient;
    constants.dirLightDirection = lighting.directionalDirection;
    constants.dirLightColor     = lighting.directionalColor;
    constants.pointLightPos     = lighting.pointPosition;
    constants.pointLightRange   = lighting.pointRange;
    constants.pointLightColor   = lighting.pointColor;
    constants.shadowTexelSize = {
        1.0f / float(m_shadowMap->Width()),
        1.0f / float(m_shadowMap->Height())
    };
    constants.shadowBias       = shadowBias;
    constants.shadowStrength   = m_shadowCastersExist ? shadowStrength : 0.0f;

    memcpy(frame.passCBMapped, &constants, sizeof(constants));
}

// Written once per OBJECT: its transform and material.
// The world matrix arrives already built - composing it from a Transform is
// the render system's job, on the other side of the boundary.
void Renderer::UpdateObjectConstants(FrameResource& frame,
                                     const std::vector<DrawItem>& items)
{
    if (ExceedsInitialObjectCapacity(items.size()))
    {
        throw std::runtime_error(
            "object constant buffer exhausted: draw_items=" +
            std::to_string(items.size()) + " capacity=" +
            std::to_string(kMaxObjects));
    }

    for (size_t i = 0; i < items.size(); ++i)
    {
        const DrawItem& item  = items[i];
        const XMMATRIX  world = XMLoadFloat4x4(&item.world);

        // Normals need the INVERSE TRANSPOSE, not the world matrix.
        // Squashing a surface tilts it one way, but squashing its normal the
        // same way tilts it the OTHER way - the two stop being
        // perpendicular. The inverse transpose is exactly the matrix that
        // undoes that. For uniform scale it reduces to the world matrix (up
        // to a factor normalize() removes), which is why the bug stays
        // hidden until something is both non-uniformly scaled AND has faces
        // that are not axis-aligned - hence the squashed pyramid.
        const XMMATRIX worldInvTranspose =
            XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        ObjectConstants constants;
        // The extra transpose on both is the row-major -> HLSL column-major
        // fix.
        XMStoreFloat4x4(&constants.world, XMMatrixTranspose(world));
        XMStoreFloat4x4(&constants.worldInvTranspose,
                        XMMatrixTranspose(worldInvTranspose));
        constants.diffuseAlbedo = item.material.diffuseAlbedo;
        constants.specularColor = item.material.specularColor;
        constants.shininess     = item.material.shininess;
        constants.normalStrength = item.material.normalStrength;

        memcpy(frame.objectCBMapped + i * kObjectCBSize, &constants, sizeof(constants));
    }
}

// State every geometry pass into the scene target needs. Extracted so that
// adding a pass is choosing a role, not copying six bind calls that must
// stay in sync.
void Renderer::BindScenePass(FrameResource& frame, const SceneAttachments& target,
                             PsoRole role)
{
    // Command lists are stateless after Reset: root signature, PSO,
    // viewport/scissor, topology and buffers are set every frame.
    m_commandList->SetGraphicsRootSignature(m_sceneRootSignature.Get());
    const size_t sampleVariant = m_msaaEnabled ? 1u : 0u;
    m_commandList->SetPipelineState(
        m_pipelineStates[sampleVariant][size_t(role)].Get());

    // Bound ONCE for the whole frame - the payoff of splitting the constant
    // buffers by update frequency.
    m_commandList->SetGraphicsRootConstantBufferView(
        1, frame.passCB->GetGPUVirtualAddress());
    ++m_lastFrameStats.rootCbvBinds;
    m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowMap->SRV());

    m_commandList->RSSetViewports(1, &target.viewport);
    m_commandList->RSSetScissorRects(1, &target.scissor);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Renderer::QueryMsaaSupport()
{
    auto qualityLevels = [&](DXGI_FORMAT format) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query = {};
        query.Format      = format;
        query.SampleCount = 4;
        query.Flags       = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        if (FAILED(m_device.Device()->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &query, sizeof(query))))
        {
            return 0u;
        }
        return query.NumQualityLevels;
    };

    // An attachment set is valid only when colour, depth, and the PSO all
    // use the same count AND quality. Take the common quality range.
    const UINT colorLevels = qualityLevels(kSceneColorFormat);
    const UINT depthLevels = qualityLevels(kSceneDepthViewFormat);
    const UINT commonLevels = std::min(colorLevels, depthLevels);
    m_4xMsaaSupported = commonLevels > 0;
    m_4xMsaaQuality   = m_4xMsaaSupported ? commonLevels - 1 : 0;
}

// Build pass membership without moving DrawItems themselves. Their position
// in the source array is also their Object CB slot, so sorting that array or
// renumbering a queue independently would bind the wrong transform.
void Renderer::BuildDrawQueues(const CameraView& camera,
                               const std::vector<DrawItem>& items,
                               DrawQueue& outOpaque,
                               DrawQueue& outTransparent) const
{
    outOpaque.clear();
    outTransparent.clear();
    outOpaque.reserve(items.size());
    outTransparent.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].layer == RenderLayer::Transparent)
        {
            outTransparent.push_back(i);
        }
        else
        {
            outOpaque.push_back(i);
        }
    }

    XMVECTOR forward = XMLoadFloat3(&camera.forward);
    const float forwardLengthSq = XMVectorGetX(XMVector3LengthSq(forward));
    if (!std::isfinite(forwardLengthSq) || forwardLengthSq < 1e-12f)
    {
        forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    else
    {
        forward = XMVector3Normalize(forward);
    }
    const XMVECTOR eye = XMLoadFloat3(&camera.position);

    auto cameraDepth = [&](size_t itemIndex) {
        const XMVECTOR center = XMLoadFloat3(&items[itemIndex].worldBoundsCenter);
        const float depth = XMVectorGetX(
            XMVector3Dot(XMVectorSubtract(center, eye), forward));
        // Invalid transforms should not poison stable_sort's strict ordering.
        return std::isfinite(depth) ? depth : -FLT_MAX;
    };

    // Largest camera-space Z first: straight-alpha blending needs every
    // farther surface already in the target when a nearer surface arrives.
    std::stable_sort(outTransparent.begin(), outTransparent.end(),
                     [&](size_t left, size_t right) {
                         return cameraDepth(left) > cameraDepth(right);
                     });
}

// One draw per item, with its per-object constants and texture.
//
// A queue changes draw ORDER only. Each queue value remains the source item
// index and therefore the matching Object CB slot.
void Renderer::DrawItems(FrameResource& frame, const std::vector<DrawItem>& items,
                         const DrawQueue& drawQueue)
{
    const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase = frame.objectCB->GetGPUVirtualAddress();

    for (const size_t itemIndex : drawQueue)
    {
        const DrawItem& item = items[itemIndex];
        const Mesh&     mesh = m_resources.GetMesh(item.mesh);

        // Point the root CBV at this object's slot - no descriptor heap
        // juggling, just an address.
        m_commandList->SetGraphicsRootConstantBufferView(
            0, objectCBBase + UINT64(itemIndex) * kObjectCBSize);
        ++m_lastFrameStats.rootCbvBinds;

        // Each material names its own texture; the handle resolves to a
        // slot in the heap bound above. A material placed in the editor may
        // not have chosen one yet - the white default keeps the shader's
        // sample valid instead of leaving the table pointing at nothing.
        const TextureHandle texture = item.material.texture.IsValid()
                                    ? item.material.texture
                                    : m_resources.DefaultTexture();
        m_commandList->SetGraphicsRootDescriptorTable(
            2, m_resources.TextureSRV(texture).gpu);

        // Same idea for the normal map, and the same reason it is never left
        // unbound: a descriptor table the shader reads but nobody filled is
        // undefined behaviour, not a zero. The flat default decodes to
        // "straight out of the surface", so a material without a normal map
        // comes out byte-identical to how it looked before this step.
        const TextureHandle normalMap = item.material.normalTexture.IsValid()
                                      ? item.material.normalTexture
                                      : m_resources.DefaultNormalTexture();
        m_commandList->SetGraphicsRootDescriptorTable(
            3, m_resources.TextureSRV(normalMap).gpu);

        m_commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
        m_commandList->IASetIndexBuffer(&mesh.ibv);
        // StartIndexLocation is what splits one shared index buffer into
        // per-material draws. indexCount 0 means "the whole mesh", which is
        // what a caller that knows nothing about submeshes would pass.
        m_commandList->DrawIndexedInstanced(
            item.indexCount != 0 ? item.indexCount : mesh.indexCount,
            1, item.indexOffset, 0, 0);
        ++m_lastFrameStats.drawCalls;
    }
}

// Pass: the same geometry, from the light. Writes depth and nothing else.
//
// Runs FIRST, before anything touches the scene target, because the lighting
// pass reads this map and has to find it already finished.
void Renderer::DrawShadowDepthPass(FrameResource& frame,
                                   const std::vector<DrawItem>& items,
                                   const DrawQueue& opaqueItems)
{
    // Back to writable. Skipped on the very first frame because DepthTarget
    // creates its resource already in DEPTH_WRITE - transitioning FROM a
    // state it was never in is a Debug Layer error, not a no-op.
    if (m_shadowMapIsShaderResource)
    {
        D3D12_RESOURCE_BARRIER toDepthWrite = TransitionBarrier(
            m_shadowMap->Resource(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_commandList->ResourceBarrier(1, &toDepthWrite);
    }

    // NO render target - the whole point of a depth-only pass. Passing 0 and
    // null is what tells the output merger there is no colour to write.
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_shadowMap->DSV();
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                         1.0f, 0, 0, nullptr);

    // No casters means no meaningful light frustum - UpdatePassConstants
    // already wrote identity and said so, and there is nothing to draw. But
    // the clear and the state transition below still have to happen:
    //
    //   - on the FIRST frame ever, with no return here the resource is
    //     still DEPTH_WRITE when the debug panel's image widget samples it
    //     through the SRV a few lines later in Render() - reading a resource
    //     through an SRV while it is in DEPTH_WRITE is a Debug Layer error
    //     the panel would trip on the very first empty scene.
    //   - on any LATER frame that loses its casters (File > New over a scene
    //     that had some), returning early leaves the map holding the LAST
    //     frame's depths - a stale picture with no caster behind it, shown
    //     as if it were current.
    //
    // Clearing to 1.0 (far) is the honest picture for "nothing to cast a
    // shadow": every texel reads as the far plane, same as a real pass over
    // an empty frustum would produce.
    if (!m_shadowCastersExist || opaqueItems.empty())
    {
        D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
            m_shadowMap->Resource(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_commandList->ResourceBarrier(1, &toShaderResource);
        m_shadowMapIsShaderResource = true;
        return;
    }

    // Deliberately NOT BindScenePass: that binds the scene viewport and the
    // material tables, and both would be wrong here. The shadow map has its
    // own square resolution, and there are no textures to bind because there
    // is no pixel shader to sample them.
    m_commandList->SetGraphicsRootSignature(m_sceneRootSignature.Get());
    m_commandList->SetPipelineState(
        m_pipelineStates[0][size_t(PsoRole::ShadowDepth)].Get());
    m_commandList->SetGraphicsRootConstantBufferView(
        1, frame.passCB->GetGPUVirtualAddress());
    ++m_lastFrameStats.rootCbvBinds;

    const D3D12_VIEWPORT viewport = m_shadowMap->Viewport();
    const D3D12_RECT     scissor  = m_shadowMap->ScissorRect();
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Every opaque item is a caster. Alpha-blended geometry is deliberately
    // absent: the depth-only shader cannot represent partial coverage and
    // would otherwise stamp a solid silhouette into the map.
    const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase = frame.objectCB->GetGPUVirtualAddress();
    for (const size_t itemIndex : opaqueItems)
    {
        const DrawItem& item = items[itemIndex];
        const Mesh&     mesh = m_resources.GetMesh(item.mesh);

        // The SAME object constant buffer the main pass uses, at the same
        // slot - the shadow pass needs the world matrix and nothing else, so
        // there is no second upload and no chance of the two passes drawing
        // the object in two different places.
        m_commandList->SetGraphicsRootConstantBufferView(
            0, objectCBBase + UINT64(itemIndex) * kObjectCBSize);
        ++m_lastFrameStats.rootCbvBinds;

        m_commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
        m_commandList->IASetIndexBuffer(&mesh.ibv);
        m_commandList->DrawIndexedInstanced(
            item.indexCount != 0 ? item.indexCount : mesh.indexCount,
            1, item.indexOffset, 0, 0);
        ++m_lastFrameStats.drawCalls;
    }

    // Hand it to both readers: the scene lighting pass and the debug image.
    D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
        m_shadowMap->Resource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &toShaderResource);
    m_shadowMapIsShaderResource = true;
}

// Pass: the world, into the attachment set selected by the host.
void Renderer::DrawOpaquePass(FrameResource& frame,
                              const SceneAttachments& target,
                              const std::vector<DrawItem>& items,
                              const DrawQueue& opaqueItems)
{
    m_commandList->OMSetRenderTargets(1, &target.rtv, FALSE, &target.dsv);
    m_commandList->ClearRenderTargetView(target.rtv, kSceneClearColor,
                                         0, nullptr);
    // Depth must be cleared to 1.0 (far) every frame, or last frame's depths
    // would reject this frame's pixels.
    m_commandList->ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         1.0f, 0, 0, nullptr);

    BindScenePass(frame, target, PsoRole::Opaque);
    DrawItems(frame, items, opaqueItems);
}

// Fills whatever the opaque pass left untouched. Runs AFTER it rather than
// before so the sky is only shaded where it is actually visible - drawing it
// first would shade every pixel and then paint over most of them.
void Renderer::DrawSkyboxPass(FrameResource& frame,
                              const SceneAttachments& target,
                              CubeTextureHandle skybox)
{
    if (!skybox.IsValid() || !m_skyboxMesh.IsValid())
    {
        return; // no sky in this scene - the clear colour stands
    }

    BindScenePass(frame, target, PsoRole::Skybox);

    // b0 is unused by the sky shaders but the root signature declares it, so
    // point it at a real address rather than leaving it dangling.
    m_commandList->SetGraphicsRootConstantBufferView(
        0, frame.objectCB->GetGPUVirtualAddress());
    ++m_lastFrameStats.rootCbvBinds;
    m_commandList->SetGraphicsRootDescriptorTable(
        2, m_resources.CubeTextureSRV(skybox).gpu);

    const Mesh& mesh = m_resources.GetMesh(m_skyboxMesh);
    m_commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
    m_commandList->IASetIndexBuffer(&mesh.ibv);
    m_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
    ++m_lastFrameStats.drawCalls;
}

// Straight-alpha compositing after the skybox supplies the background. The
// queue is already back-to-front; depth testing still lets opaque geometry
// reject transparent fragments behind it, while the PSO keeps depth writes
// off so transparent surfaces do not reject one another.
void Renderer::DrawTransparentPass(FrameResource& frame,
                                   const SceneAttachments& target,
                                   const std::vector<DrawItem>& items,
                                   const DrawQueue& transparentItems)
{
    if (transparentItems.empty())
    {
        return;
    }

    m_commandList->OMSetRenderTargets(1, &target.rtv, FALSE, &target.dsv);

    BindScenePass(frame, target, PsoRole::Transparent);
    DrawItems(frame, items, transparentItems);
}

Renderer::SceneAttachments Renderer::GetSceneAttachments(SceneOutput output) const
{
    SceneAttachments target;
    if (output == SceneOutput::OffscreenTexture)
    {
        target.color = m_sceneColor->ColorResource();
        target.rtv = m_sceneColor->RTV();
        target.dsv = m_sceneDepth->DSV();
        target.viewport = m_sceneColor->Viewport();
        target.scissor = m_sceneColor->ScissorRect();
        target.width = m_sceneColor->Width();
        target.height = m_sceneColor->Height();
        target.restingState = m_sceneColor->IsMultisampled()
            ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE
            : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return target;
    }

    target.dsv = m_swapChainDepth->DSV();
    target.width = m_swapChain.Width();
    target.height = m_swapChain.Height();
    target.viewport = {
        0.0f, 0.0f, float(target.width), float(target.height), 0.0f, 1.0f
    };
    target.scissor = { 0, 0, LONG(target.width), LONG(target.height) };
    if (m_msaaEnabled)
    {
        target.color = m_swapChainMsaaColor->ColorResource();
        target.rtv = m_swapChainMsaaColor->RTV();
        target.restingState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    }
    else
    {
        target.color = m_swapChain.CurrentBackBuffer();
        target.rtv = m_swapChain.CurrentBackBufferRTV();
        target.restingState = D3D12_RESOURCE_STATE_PRESENT;
    }
    return target;
}

// Every output uses this exact scene sequence. The target supplies attachment
// handles and dimensions; it does not select or duplicate any scene pass.
void Renderer::RecordScenePasses(FrameResource& frame,
                                 const SceneAttachments& target,
                                 const LightingData& lighting,
                                 const std::vector<DrawItem>& items,
                                 const DrawQueue& opaqueItems,
                                 const DrawQueue& transparentItems)
{
    DrawShadowDepthPass(frame, items, opaqueItems);

    D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
        target.color, target.restingState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    DrawOpaquePass(frame, target, items, opaqueItems);
    DrawSkyboxPass(frame, target, lighting.skybox);
    DrawTransparentPass(frame, target, items, transparentItems);
}

void Renderer::RecordBackBufferOverlay(const OverlayRecorder& overlayRecorder,
                                       bool clearBackBuffer)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_swapChain.CurrentBackBufferRTV();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    if (clearBackBuffer)
    {
        m_commandList->ClearRenderTargetView(rtv, kEditorClearColor, 0, nullptr);
    }

    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, float(m_swapChain.Width()), float(m_swapChain.Height()), 0.0f, 1.0f
    };
    const D3D12_RECT scissor = {
        0, 0, LONG(m_swapChain.Width()), LONG(m_swapChain.Height())
    };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    if (overlayRecorder)
    {
        overlayRecorder(m_commandList.Get());
    }
}

// Offscreen output becomes shader-readable, then a host-owned layer records
// the actual back-buffer presentation. An empty recorder still clears and
// presents a valid frame.
void Renderer::PresentOffscreen(const OverlayRecorder& overlayRecorder)
{
    if (!m_sceneColor->IsMultisampled())
    {
        D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
            m_sceneColor->ColorResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_commandList->ResourceBarrier(1, &toShaderResource);
    }
    else
    {
        D3D12_RESOURCE_BARRIER barriers[] = {
            TransitionBarrier(m_sceneColor->ColorResource(),
                              D3D12_RESOURCE_STATE_RENDER_TARGET,
                              D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
            TransitionBarrier(m_sceneColor->ShaderResource(),
                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                              D3D12_RESOURCE_STATE_RESOLVE_DEST),
        };
        m_commandList->ResourceBarrier(_countof(barriers), barriers);
        m_commandList->ResolveSubresource(
            m_sceneColor->ShaderResource(), 0, m_sceneColor->ColorResource(), 0,
            m_sceneColor->ColorFormat());
        D3D12_RESOURCE_BARRIER resolveToShader = TransitionBarrier(
            m_sceneColor->ShaderResource(), D3D12_RESOURCE_STATE_RESOLVE_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_commandList->ResourceBarrier(1, &resolveToShader);
    }

    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();
    D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRenderTarget);
    RecordBackBufferOverlay(overlayRecorder, true);
    D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &toPresent);
}

void Renderer::PresentSwapChain(const SceneAttachments& target,
                                const OverlayRecorder& overlayRecorder)
{
    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();
    if (!m_msaaEnabled)
    {
        // The 1x scene is already in the back buffer and it remains bound as
        // an RTV, so an optional layer can append without another transition.
        RecordBackBufferOverlay(overlayRecorder, false);
        D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
            backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &toPresent);
        return;
    }

    D3D12_RESOURCE_BARRIER resolveBarriers[] = {
        TransitionBarrier(target.color, D3D12_RESOURCE_STATE_RENDER_TARGET,
                          D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
        TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT,
                          D3D12_RESOURCE_STATE_RESOLVE_DEST),
    };
    m_commandList->ResourceBarrier(_countof(resolveBarriers), resolveBarriers);
    m_commandList->ResolveSubresource(backBuffer, 0, target.color, 0,
                                      SwapChain::kFormat);

    if (overlayRecorder)
    {
        D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
            backBuffer, D3D12_RESOURCE_STATE_RESOLVE_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &toRenderTarget);
        RecordBackBufferOverlay(overlayRecorder, false);
        D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
            backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &toPresent);
    }
    else
    {
        D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
            backBuffer, D3D12_RESOURCE_STATE_RESOLVE_DEST,
            D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &toPresent);
    }
}

void Renderer::RenderFrame(const CameraView& camera, const LightingData& lighting,
                           const std::vector<DrawItem>& items, SceneOutput output,
                           const OverlayRecorder& overlayRecorder)
{
    m_lastFrameStats = {};
    m_lastFrameStats.submittedItems = items.size();
    m_lastFrameStats.objectCapacity = kMaxObjects;
    m_lastFrameStats.srvUsed = m_srvAllocator.UsedCount();
    m_lastFrameStats.srvCapacity = m_srvAllocator.Capacity();

    FrameResource& frame = m_frames[m_currentFrame];

    // Wait only until the GPU is done with THIS set of resources - which,
    // with kFramesInFlight sets, is usually already true and costs nothing.
    // The CPU keeps going while the GPU draws.
    m_device.WaitForFenceValue(frame.fenceValue);

    // A pending viewport resize is applied HERE, before anything reads the
    // target's size. The fence wait above is not enough: it only proves this
    // frame's set is free, while the OTHER frame in flight may still be
    // sampling the old texture. Recreating a texture needs the whole GPU
    // idle, so this costs a full flush - but only on frames where the panel
    // actually changed size, i.e. while the user is dragging a splitter.
    if (m_requestedViewportWidth  != 0 &&
        (m_requestedViewportWidth  != m_sceneColor->Width() ||
         m_requestedViewportHeight != m_sceneColor->Height()))
    {
        m_device.WaitForGpu();
        // Both halves of the attachment, together - a colour and depth pair
        // of different sizes is an invalid render target.
        m_sceneColor->Resize(m_requestedViewportWidth, m_requestedViewportHeight);
        m_sceneDepth->Resize(m_requestedViewportWidth, m_requestedViewportHeight);
    }

    // Queues contain source indices, so transparent sorting never changes
    // the item-to-Object-CB mapping established below.
    DrawQueue opaqueItems;
    DrawQueue transparentItems;
    BuildDrawQueues(camera, items, opaqueItems, transparentItems);

    // Only now is it safe to overwrite this frame's constant buffers and
    // recycle its command memory.
    const SceneAttachments target = GetSceneAttachments(output);
    const float renderAspect = target.height == 0
        ? 1.0f : float(target.width) / float(target.height);
    UpdatePassConstants(frame, camera, lighting, items, opaqueItems, renderAspect);
    UpdateObjectConstants(frame, items);

    ThrowIfFailed(frame.commandAllocator->Reset(), "Allocator Reset");
    ThrowIfFailed(m_commandList->Reset(frame.commandAllocator.Get(), nullptr),
                  "CommandList Reset");

    // Bound once for scene resources and any host-owned recorder resources.
    ID3D12DescriptorHeap* heaps[] = { m_srvAllocator.Heap() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    RecordScenePasses(frame, target, lighting, items,
                      opaqueItems, transparentItems);
    if (output == SceneOutput::OffscreenTexture)
    {
        PresentOffscreen(overlayRecorder);
    }
    else
    {
        PresentSwapChain(target, overlayRecorder);
    }

    ThrowIfFailed(m_commandList->Close(), "CommandList Close");

    // Everything so far was only RECORDED. This hands it to the GPU.
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_device.Queue()->ExecuteCommandLists(1, lists);

    m_swapChain.Present(m_vsync);

    // Mark when this frame's work will be done, but do NOT wait for it. The
    // next Render() that lands back on this FrameResource checks this value.
    frame.fenceValue = m_device.Signal();

    m_lastFrameStats.mainVisible = opaqueItems.size() + transparentItems.size();
    m_lastFrameStats.shadowVisible = m_shadowCastersExist
        ? opaqueItems.size() : 0;
    m_lastFrameStats.renderWidth = target.width;
    m_lastFrameStats.renderHeight = target.height;

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}
