#include "Graphics/Renderer.h"

#include "Core/Common.h"
#include "Graphics/Image.h"
#include "Graphics/Mesh.h"
#include "Game/Scene.h"
#include "Game/Camera.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <filesystem>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    constexpr float kClearColor[4] = { 0.02f, 0.04f, 0.08f, 1.0f };
    constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

    ComPtr<ID3DBlob> CompileShader(const std::filesystem::path& path,
                                   const char* entryPoint, const char* target)
    {
        // D3DCompileFromFile only reports a bare HRESULT for a missing file,
        // which is painful to diagnose - check first and name the path.
        if (!std::filesystem::exists(path))
        {
            throw std::runtime_error("Shader file not found:\n" + path.string());
        }

        UINT flags = 0;
#if defined(_DEBUG)
        // Embed debug info and keep the HLSL readable in PIX/RenderDoc.
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr,
                                        D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                        entryPoint, target, flags, 0,
                                        &bytecode, &errors);
        if (FAILED(hr))
        {
            // The compiler writes human-readable errors into the blob.
            if (errors)
            {
                throw std::runtime_error(
                    static_cast<const char*>(errors->GetBufferPointer()));
            }
            ThrowIfFailed(hr, "D3DCompileFromFile");
        }
        return bytecode;
    }
}

Renderer::Renderer(HWND hwnd, UINT width, UINT height)
    : m_device()                                  // debug layer -> device -> queue
    , m_swapChain(m_device, hwnd, width, height)  // needs the queue
    , m_dsvAllocator(m_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                     kDsvHeapCapacity, /*shaderVisible*/ false)
    , m_srvAllocator(m_device.Device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                     kSrvHeapCapacity, /*shaderVisible*/ true)
{
    // Slots are taken once here; the views written into them can change.
    m_depthStencilView = m_dsvAllocator.Allocate();
    m_textureSRV       = m_srvAllocator.Allocate();

    CreateCommandObjects();
    CreateSizeDependentResources();
    CreateConstantBuffers();
    CreateTexture();          // records + submits, so needs the command list
    CreateRootSignature();
    CreatePipelineState();
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

// The depth buffer is ours (unlike the back buffers), so a resize means
// recreating it by hand.
void Renderer::CreateSizeDependentResources()
{
    const UINT width  = m_swapChain.Width();
    const UINT height = m_swapChain.Height();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU-local memory

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = width;
    desc.Height           = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = kDepthFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // Declaring the expected clear value lets the driver pick a faster clear
    // path. It must match what ClearDepthStencilView uses.
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format             = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f; // 1.0 = farthest

    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE,
                      &clearValue, IID_PPV_ARGS(&m_depthStencilBuffer)),
                  "CreateCommittedResource(DepthStencil)");

    m_device.Device()->CreateDepthStencilView(
        m_depthStencilBuffer.Get(), nullptr,
        m_depthStencilView.cpu);

    m_viewport    = { 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f };
    m_scissorRect = { 0, 0, LONG(width), LONG(height) };
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

// A GPU-local (default heap) resource cannot be written by the CPU, so the
// data takes two hops:
//   CPU memory -> upload heap (CPU-writable) -> default heap
// The second hop is a GPU copy command: record, submit, wait.
void Renderer::CreateTexture()
{
    const ImageData image = LoadImageRGBA(GetAssetDir() / L"Crate.png");

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = image.width;
    texDesc.Height           = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1; // no mipmaps yet
    texDesc.Format           = SwapChain::kFormat;
    texDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST,
                      nullptr, IID_PPV_ARGS(&m_texture)),
                  "CreateCommittedResource(Texture)");

    // Textures are NOT tightly packed: each row must start on a 256-byte
    // boundary. GetCopyableFootprints computes the exact layout.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows        = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 uploadSize     = 0;
    m_device.Device()->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                             &footprint, &numRows, &rowSizeInBytes,
                                             &uploadSize);

    ComPtr<ID3D12Resource> uploadBuffer =
        CreateUploadBuffer(m_device.Device(), nullptr, uploadSize,
                           "CreateCommittedResource(TexUpload)");

    // Copy ROW BY ROW: the source is tightly packed, the destination has
    // padding at the end of every row.
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = {};
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
                  "TexUpload Map");
    for (UINT y = 0; y < numRows; ++y)
    {
        memcpy(mapped + footprint.Offset + SIZE_T(y) * footprint.Footprint.RowPitch,
               image.pixels.data() + SIZE_T(y) * image.width * 4,
               static_cast<size_t>(rowSizeInBytes));
    }
    uploadBuffer->Unmap(0, nullptr);

    // Init-time work, before any frame has been submitted - frame 0's
    // allocator is free to borrow.
    ThrowIfFailed(m_frames[0].commandAllocator->Reset(), "Allocator Reset (texture)");
    ThrowIfFailed(m_commandList->Reset(m_frames[0].commandAllocator.Get(), nullptr),
                  "CommandList Reset (texture)");

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = m_texture.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuffer.Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Copy done -> the texture becomes readable by the pixel shader.
    D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
        m_texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &toShaderResource);

    ThrowIfFailed(m_commandList->Close(), "CommandList Close (texture)");
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_device.Queue()->ExecuteCommandLists(1, lists);

    // uploadBuffer dies at the end of this function - the GPU must be
    // finished reading it first.
    m_device.WaitForGpu();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = SwapChain::kFormat;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = 1;
    m_device.Device()->CreateShaderResourceView(
        m_texture.Get(), &srvDesc, m_textureSRV.cpu);
}

// The "function signature" of the pipeline:
// b0 object CB, b1 pass CB, t0 texture, s0 static sampler.
void Renderer::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 1;
    srvRange.BaseShaderRegister = 0; // t0

    // Both CBs are visible to ALL stages: the VS needs the matrices, the PS
    // needs the material and the lights.
    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0; // b0 - per object
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1; // b1 - per frame
    rootParams[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges   = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // A static sampler lives in the root signature, not in a heap - the
    // common case, since most samplers never change at runtime.
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // floor tiles
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0; // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters     = _countof(rootParams);
    desc.pParameters       = rootParams;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers   = &sampler;
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
                                                         IID_PPV_ARGS(&m_rootSignature)),
                  "CreateRootSignature");
}

// Nearly every pipeline setting baked into ONE immutable object: shaders,
// vertex layout, rasterizer/blend/depth state, output formats. Validated
// once here, cheap to switch at runtime.
void Renderer::CreatePipelineState()
{
    const std::filesystem::path shaderFile = GetShaderDir() / L"Basic.hlsl";
    ComPtr<ID3DBlob> vs = CompileShader(shaderFile, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = CompileShader(shaderFile, "PSMain", "ps_5_0");

    // Offsets must match struct Vertex in Mesh.h exactly. Mismatches produce
    // no error - just a wrong picture.
    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode              = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode              = D3D12_CULL_MODE_BACK; // drop back faces
    rasterizer.FrontCounterClockwise = FALSE;                // clockwise = front
    rasterizer.DepthClipEnable       = TRUE;

    D3D12_BLEND_DESC blend = {};
    blend.RenderTarget[0].BlendEnable           = FALSE; // opaque for now
    blend.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature  = m_rootSignature.Get();
    psoDesc.VS              = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS              = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState      = blend;
    psoDesc.SampleMask      = UINT_MAX;
    psoDesc.RasterizerState = rasterizer;

    // LESS = keep the fragment closer to the camera.
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;
    psoDesc.DSVFormat                        = kDepthFormat;

    psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = SwapChain::kFormat;
    psoDesc.SampleDesc.Count      = 1;

    ThrowIfFailed(m_device.Device()->CreateGraphicsPipelineState(
                      &psoDesc, IID_PPV_ARGS(&m_pipelineState)),
                  "CreateGraphicsPipelineState");
}

// The only safe order:
//   1. wait for the GPU  2. release every reference to the old buffers
//   3. ResizeBuffers     4. recreate views and the depth buffer
// Skipping step 1 or 2 makes ResizeBuffers fail - the swap chain cannot free
// buffers anyone still holds.
void Renderer::Resize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    m_device.WaitForGpu();

    m_depthStencilBuffer.Reset();
    m_swapChain.Resize(width, height); // releases its own back buffer refs

    CreateSizeDependentResources();
}

// Written ONCE per frame: camera and lights are shared by every object.
void Renderer::UpdatePassConstants(FrameResource& frame, const Camera& camera,
                                   float totalSeconds)
{
    const XMVECTOR eye     = XMLoadFloat3(&camera.position);
    const XMVECTOR forward = CameraForward(camera);
    const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // LookTo (not LookAt): we have a direction, not a target point. The view
    // matrix is the INVERSE of the camera's world transform - moving the
    // camera right shifts the whole world left.
    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);

    // Aspect ratio comes from the CURRENT client size, so resizing keeps the
    // image correctly proportioned.
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        float(m_swapChain.Width()) / float(m_swapChain.Height()),
        0.1f, 200.0f);

    PassConstants constants;
    XMStoreFloat4x4(&constants.viewProj, XMMatrixTranspose(view * proj));
    constants.eyePosW = camera.position;

    // Ambient: a cheap stand-in for global bounced light.
    constants.ambientLight = { 0.18f, 0.19f, 0.22f };

    // Directional light: only a DIRECTION, no position - it models a source
    // so far away (the sun) that all its rays are parallel.
    XMVECTOR dir = XMVector3Normalize(XMVectorSet(0.6f, -0.75f, 0.3f, 0.0f));
    XMStoreFloat3(&constants.dirLightDirection, dir);
    constants.dirLightColor = { 0.85f, 0.82f, 0.75f };

    // Point light: orbits the scene so the falloff is easy to see.
    constants.pointLightPos   = { 14.0f * std::cos(totalSeconds * 0.7f),
                                  4.0f,
                                  14.0f * std::sin(totalSeconds * 0.7f) };
    constants.pointLightRange = 30.0f;
    constants.pointLightColor = { 1.0f, 0.55f, 0.2f }; // warm orange

    memcpy(frame.passCBMapped, &constants, sizeof(constants));
}

// Written once per OBJECT: its transform and material.
void Renderer::UpdateObjectConstants(FrameResource& frame, const Scene& scene,
                                     float totalSeconds)
{
    for (size_t i = 0; i < scene.objects.size() && i < kMaxObjects; ++i)
    {
        const SceneObject& obj = scene.objects[i];

        const XMMATRIX world =
            XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z) *
            XMMatrixRotationY(obj.spinSpeed * totalSeconds) *
            XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);

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
        constants.diffuseAlbedo = obj.material.diffuseAlbedo;
        constants.specularColor = obj.material.specularColor;
        constants.shininess     = obj.material.shininess;

        memcpy(frame.objectCBMapped + i * kObjectCBSize, &constants, sizeof(constants));
    }
}

void Renderer::Render(const Scene& scene, const Camera& camera, float totalSeconds)
{
    FrameResource& frame = m_frames[m_currentFrame];

    // Wait only until the GPU is done with THIS set of resources - which,
    // with kFramesInFlight sets, is usually already true and costs nothing.
    // The CPU keeps going while the GPU draws.
    m_device.WaitForFenceValue(frame.fenceValue);

    // Only now is it safe to overwrite this frame's constant buffers and
    // recycle its command memory.
    UpdatePassConstants(frame, camera, totalSeconds);
    UpdateObjectConstants(frame, scene, totalSeconds);

    ThrowIfFailed(frame.commandAllocator->Reset(), "Allocator Reset");
    ThrowIfFailed(m_commandList->Reset(frame.commandAllocator.Get(), nullptr),
                  "CommandList Reset");

    ID3D12Resource* backBuffer = m_swapChain.CurrentBackBuffer();

    // The buffer we are about to draw into was just being presented.
    D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_swapChain.CurrentBackBufferRTV();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_depthStencilView.cpu;

    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    m_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
    // Depth must be cleared to 1.0 (far) every frame, or last frame's depths
    // would reject this frame's pixels.
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                         1.0f, 0, 0, nullptr);

    // Command lists are stateless after Reset: root signature, PSO,
    // viewport/scissor, topology and buffers are set every frame.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipelineState.Get());

    ID3D12DescriptorHeap* heaps[] = { m_srvAllocator.Heap() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    m_commandList->SetGraphicsRootDescriptorTable(
        2, m_textureSRV.gpu);

    // Bound ONCE for the whole frame - the payoff of splitting the constant
    // buffers by update frequency.
    m_commandList->SetGraphicsRootConstantBufferView(
        1, frame.passCB->GetGPUVirtualAddress());

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase = frame.objectCB->GetGPUVirtualAddress();

    for (size_t i = 0; i < scene.objects.size() && i < kMaxObjects; ++i)
    {
        const Mesh& mesh = scene.meshes[scene.objects[i].meshIndex];

        // Point the root CBV at this object's slot - no descriptor heap
        // juggling, just an address.
        m_commandList->SetGraphicsRootConstantBufferView(
            0, objectCBBase + UINT64(i) * kObjectCBSize);

        m_commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
        m_commandList->IASetIndexBuffer(&mesh.ibv);
        m_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
    }

    D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &toPresent);

    ThrowIfFailed(m_commandList->Close(), "CommandList Close");

    // Everything so far was only RECORDED. This hands it to the GPU.
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_device.Queue()->ExecuteCommandLists(1, lists);

    m_swapChain.Present(m_vsync);

    // Mark when this frame's work will be done, but do NOT wait for it. The
    // next Render() that lands back on this FrameResource checks this value.
    frame.fenceValue = m_device.Signal();

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}
