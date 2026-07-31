#include "Renderer.h"

#include "Common.h"
#include "Image.h"
#include "Mesh.h"
#include "Scene.h"
#include "Camera.h"

#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <stdexcept>
#include <filesystem>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    // ---------------------------------------------------------------- settings

    constexpr UINT  kFrameCount    = 2; // double buffering
    constexpr float kClearColor[4] = { 0.02f, 0.04f, 0.08f, 1.0f };

    constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT kDepthFormat      = DXGI_FORMAT_D32_FLOAT;

    constexpr UINT kMaxObjects = 32; // constant buffer holds one slot each

    // --- constant buffers, split BY UPDATE FREQUENCY ---
    // Lights and camera are identical for every object in a frame, so
    // uploading them once (pass CB) instead of once per object (object CB)
    // is both less work and a clearer separation of concerns.

    struct ObjectConstants // b0 - written once per object per frame
    {
        XMFLOAT4X4 world;
        XMFLOAT4X4 worldInvTranspose;
        XMFLOAT4   diffuseAlbedo;
        XMFLOAT3   specularColor;
        float      shininess;
    };

    // Layout must match the HLSL cbuffer exactly. In HLSL a float3 followed
    // by a float packs into one 16-byte register, which is why every pair
    // below is written together.
    struct PassConstants // b1 - written once per frame
    {
        XMFLOAT4X4 viewProj;
        XMFLOAT3   eyePosW;           float pad0 = 0.0f;
        XMFLOAT3   ambientLight;      float pad1 = 0.0f;
        XMFLOAT3   dirLightDirection; float pad2 = 0.0f;
        XMFLOAT3   dirLightColor;     float pad3 = 0.0f;
        XMFLOAT3   pointLightPos;     float pointLightRange = 0.0f;
        XMFLOAT3   pointLightColor;   float pad4 = 0.0f;
    };

    constexpr UINT kObjectCBSize = Align(sizeof(ObjectConstants), 256);
    constexpr UINT kPassCBSize   = Align(sizeof(PassConstants), 256);

    // ---------------------------------------------------------------- state
    // File-static, so nothing outside this file can touch it.

    UINT g_clientWidth  = 0;
    UINT g_clientHeight = 0;

    ComPtr<ID3D12Device>              g_device;
    ComPtr<ID3D12CommandQueue>        g_commandQueue;
    ComPtr<IDXGISwapChain3>           g_swapChain;
    ComPtr<ID3D12Resource>            g_renderTargets[kFrameCount];
    ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
    UINT                              g_rtvDescriptorSize = 0;
    ComPtr<ID3D12CommandAllocator>    g_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> g_commandList;

    ComPtr<ID3D12RootSignature> g_rootSignature;
    ComPtr<ID3D12PipelineState> g_pipelineState;

    ComPtr<ID3D12Resource>       g_depthStencilBuffer;
    ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

    ComPtr<ID3D12Resource> g_objectCB;
    uint8_t*               g_objectCBMapped = nullptr;
    ComPtr<ID3D12Resource> g_passCB;
    uint8_t*               g_passCBMapped = nullptr;

    ComPtr<ID3D12DescriptorHeap> g_srvHeap;
    ComPtr<ID3D12Resource>       g_texture;

    ComPtr<ID3D12Fence> g_fence;
    UINT64              g_fenceValue = 0;
    HANDLE              g_fenceEvent = nullptr;

    UINT g_frameIndex = 0;

    D3D12_VIEWPORT g_viewport    = {};
    D3D12_RECT     g_scissorRect = {};

    // ---------------------------------------------------------------- helpers

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

    // Block until the GPU has finished ALL submitted work. Simple but
    // wasteful - Phase 9 (frame resources) overlaps CPU and GPU properly.
    void WaitForGpu()
    {
        const UINT64 valueToWait = ++g_fenceValue;
        ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), valueToWait),
                      "Queue Signal");
        if (g_fence->GetCompletedValue() < valueToWait)
        {
            ThrowIfFailed(g_fence->SetEventOnCompletion(valueToWait, g_fenceEvent),
                          "SetEventOnCompletion");
            WaitForSingleObject(g_fenceEvent, INFINITE);
        }
    }

    // ---------------------------------------------------------------- init steps
    // Called from Renderer::Initialize in this order.

    // [1] Debug layer - MUST be enabled before the device is created.
    void EnableDebugLayer()
    {
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
#endif
    }

    // [2] DXGI factory - the entry point to adapters and swap chains.
    ComPtr<IDXGIFactory6> CreateFactory()
    {
        UINT flags = 0;
#if defined(_DEBUG)
        flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)),
                      "CreateDXGIFactory2");
        return factory;
    }

    // [3] Device - first hardware adapter that speaks D3D12, fastest first.
    void CreateDevice(IDXGIFactory6* factory)
    {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             factory->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue; // WARP etc. - we want real hardware
            }
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&g_device))))
            {
                return;
            }
        }
        throw std::runtime_error("No D3D12-capable hardware GPU found");
    }

    // [4] Command objects: allocator = command memory, list = recorder,
    //     queue = where recorded lists are submitted for execution.
    void CreateCommandObjects()
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(g_device->CreateCommandQueue(&queueDesc,
                                                   IID_PPV_ARGS(&g_commandQueue)),
                      "CreateCommandQueue");

        ThrowIfFailed(g_device->CreateCommandAllocator(
                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                          IID_PPV_ARGS(&g_commandAllocator)),
                      "CreateCommandAllocator");

        ThrowIfFailed(g_device->CreateCommandList(
                          0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                          g_commandAllocator.Get(), nullptr,
                          IID_PPV_ARGS(&g_commandList)),
                      "CreateCommandList");

        // Lists are born recording, but every frame starts with Reset(),
        // which requires a CLOSED list.
        ThrowIfFailed(g_commandList->Close(), "Close command list");
    }

    // [5] Swap chain - created from the QUEUE, because Present is itself
    //     work that goes through the queue.
    void CreateSwapChain(IDXGIFactory6* factory, HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.BufferCount      = kFrameCount;
        desc.Width            = g_clientWidth;
        desc.Height           = g_clientHeight;
        desc.Format           = kBackBufferFormat;
        desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD; // modern flip model
        desc.SampleDesc.Count = 1;                             // no MSAA

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
                          g_commandQueue.Get(), hWnd, &desc,
                          nullptr, nullptr, &swapChain1),
                      "CreateSwapChainForHwnd");

        // Upgrade for GetCurrentBackBufferIndex (added in IDXGISwapChain3).
        ThrowIfFailed(swapChain1.As(&g_swapChain), "IDXGISwapChain3 query");

        // DXGI's Alt+Enter fullscreen toggle fights with manual resize
        // handling - disable it.
        ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER),
                      "MakeWindowAssociation");

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }

    // [6] Descriptor heaps that OUTLIVE a resize (only contents change).
    void CreateDescriptorHeaps()
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)),
                      "CreateDescriptorHeap(RTV)");

        // Descriptor sizes are GPU-specific - always ask, never hardcode.
        g_rtvDescriptorSize =
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 1;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)),
                      "CreateDescriptorHeap(DSV)");

        // Shader-visible heap: just the texture SRV. The per-object matrices
        // go through a ROOT DESCRIPTOR instead, which needs no heap slot.
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)),
                      "CreateDescriptorHeap(SRV)");
    }

    // [7] Fence - a counter the GPU writes and the CPU waits on.
    void CreateSyncObjects()
    {
        ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(&g_fence)),
                      "CreateFence");
        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent)
        {
            throw std::runtime_error("CreateEvent failed");
        }
    }

    // [8] Everything whose SIZE depends on the window. Called at startup and
    //     again after every resize.
    void CreateSizeDependentResources()
    {
        // Back buffer RTVs (the buffers belong to the swap chain; we only
        // fetch them and create views).
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i)
        {
            ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])),
                          "SwapChain GetBuffer");
            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize;
        }

        // Depth buffer - this one WE own, so it must be recreated by hand.
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU-local memory

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = g_clientWidth;
        desc.Height           = g_clientHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        // Declaring the expected clear value lets the driver pick a faster
        // clear path. It must match what ClearDepthStencilView uses.
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format             = kDepthFormat;
        clearValue.DepthStencil.Depth = 1.0f; // 1.0 = farthest

        ThrowIfFailed(g_device->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_DEPTH_WRITE,
                          &clearValue, IID_PPV_ARGS(&g_depthStencilBuffer)),
                      "CreateCommittedResource(DepthStencil)");

        g_device->CreateDepthStencilView(
            g_depthStencilBuffer.Get(), nullptr,
            g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        g_viewport    = { 0.0f, 0.0f, float(g_clientWidth), float(g_clientHeight),
                          0.0f, 1.0f };
        g_scissorRect = { 0, 0, LONG(g_clientWidth), LONG(g_clientHeight) };
    }

    // [9] Two constant buffers, one per update frequency.
    void CreateConstantBuffers()
    {
        g_objectCB = CreateUploadBuffer(g_device.Get(), nullptr,
                                        UINT64(kObjectCBSize) * kMaxObjects,
                                        "CreateCommittedResource(ObjectCB)");
        // Map once and keep the pointer: Map/Unmap every frame would be
        // pure overhead for an upload-heap resource.
        D3D12_RANGE readRange = {};
        ThrowIfFailed(g_objectCB->Map(0, &readRange,
                                      reinterpret_cast<void**>(&g_objectCBMapped)),
                      "ObjectCB Map");

        g_passCB = CreateUploadBuffer(g_device.Get(), nullptr, kPassCBSize,
                                      "CreateCommittedResource(PassCB)");
        ThrowIfFailed(g_passCB->Map(0, &readRange,
                                    reinterpret_cast<void**>(&g_passCBMapped)),
                      "PassCB Map");
    }

    // [10] Texture. A GPU-local (default heap) resource cannot be written by
    //      the CPU, so the data takes two hops:
    //        CPU memory -> upload heap (CPU-writable) -> default heap
    //      The second hop is a GPU copy command: record, submit, wait.
    void CreateTexture()
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
        texDesc.Format           = kBackBufferFormat;
        texDesc.SampleDesc.Count = 1;

        ThrowIfFailed(g_device->CreateCommittedResource(
                          &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                          D3D12_RESOURCE_STATE_COPY_DEST,
                          nullptr, IID_PPV_ARGS(&g_texture)),
                      "CreateCommittedResource(Texture)");

        // Textures are NOT tightly packed: each row must start on a 256-byte
        // boundary. GetCopyableFootprints computes the exact layout.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT   numRows        = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 uploadSize     = 0;
        g_device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                        &footprint, &numRows, &rowSizeInBytes,
                                        &uploadSize);

        ComPtr<ID3D12Resource> uploadBuffer =
            CreateUploadBuffer(g_device.Get(), nullptr, uploadSize,
                               "CreateCommittedResource(TexUpload)");

        // Copy ROW BY ROW: the source is tightly packed, the destination has
        // padding at the end of every row.
        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange = {};
        ThrowIfFailed(uploadBuffer->Map(0, &readRange,
                                        reinterpret_cast<void**>(&mapped)),
                      "TexUpload Map");
        for (UINT y = 0; y < numRows; ++y)
        {
            memcpy(mapped + footprint.Offset + SIZE_T(y) * footprint.Footprint.RowPitch,
                   image.pixels.data() + SIZE_T(y) * image.width * 4,
                   static_cast<size_t>(rowSizeInBytes));
        }
        uploadBuffer->Unmap(0, nullptr);

        ThrowIfFailed(g_commandAllocator->Reset(), "Allocator Reset (texture)");
        ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), nullptr),
                      "CommandList Reset (texture)");

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = g_texture.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource       = uploadBuffer.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        g_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Copy done -> the texture becomes readable by the pixel shader.
        D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
            g_texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        g_commandList->ResourceBarrier(1, &toShaderResource);

        ThrowIfFailed(g_commandList->Close(), "CommandList Close (texture)");
        ID3D12CommandList* lists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, lists);

        // uploadBuffer dies at the end of this function - the GPU must be
        // finished reading it first.
        WaitForGpu();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                  = kBackBufferFormat;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels     = 1;
        g_device->CreateShaderResourceView(
            g_texture.Get(), &srvDesc,
            g_srvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // [11] Root signature: the "function signature" of the pipeline.
    //      b0 object CB, b1 pass CB, t0 texture, s0 static sampler.
    void CreateRootSignature()
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0; // t0

        // Both CBs are visible to ALL stages: the VS needs the matrices, the
        // PS needs the material and the lights.
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
        ThrowIfFailed(g_device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                    blob->GetBufferSize(),
                                                    IID_PPV_ARGS(&g_rootSignature)),
                      "CreateRootSignature");
    }

    // [12] PSO - nearly every pipeline setting baked into ONE immutable
    //      object: shaders, vertex layout, rasterizer/blend/depth state,
    //      output formats. Validated once here, cheap to switch at runtime.
    void CreatePipelineState()
    {
        const std::filesystem::path shaderFile = GetShaderDir() / L"Basic.hlsl";
        ComPtr<ID3DBlob> vs = CompileShader(shaderFile, "VSMain", "vs_5_0");
        ComPtr<ID3DBlob> ps = CompileShader(shaderFile, "PSMain", "ps_5_0");

        // Offsets must match struct Vertex in Mesh.h exactly. Mismatches
        // produce no error - just a wrong picture.
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
        psoDesc.pRootSignature  = g_rootSignature.Get();
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
        psoDesc.RTVFormats[0]         = kBackBufferFormat;
        psoDesc.SampleDesc.Count      = 1;

        ThrowIfFailed(g_device->CreateGraphicsPipelineState(
                          &psoDesc, IID_PPV_ARGS(&g_pipelineState)),
                      "CreateGraphicsPipelineState");
    }

    // ---------------------------------------------------------------- per frame

    // Written ONCE per frame: camera and lights are shared by every object.
    void UpdatePassConstants(const Camera& camera, float totalSeconds)
    {
        const XMVECTOR eye     = XMLoadFloat3(&camera.position);
        const XMVECTOR forward = CameraForward(camera);
        const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        // LookTo (not LookAt): we have a direction, not a target point. The
        // view matrix is the INVERSE of the camera's world transform -
        // moving the camera right shifts the whole world left.
        const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);

        // Aspect ratio comes from the CURRENT client size, so resizing keeps
        // the image correctly proportioned.
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            float(g_clientWidth) / float(g_clientHeight),
            0.1f, 200.0f);

        PassConstants constants;
        XMStoreFloat4x4(&constants.viewProj, XMMatrixTranspose(view * proj));
        constants.eyePosW = camera.position;

        // Ambient: a cheap stand-in for global bounced light.
        constants.ambientLight = { 0.18f, 0.19f, 0.22f };

        // Directional light: only a DIRECTION, no position - it models a
        // source so far away (the sun) that all its rays are parallel.
        XMVECTOR dir = XMVector3Normalize(XMVectorSet(0.6f, -0.75f, 0.3f, 0.0f));
        XMStoreFloat3(&constants.dirLightDirection, dir);
        constants.dirLightColor = { 0.85f, 0.82f, 0.75f };

        // Point light: orbits the scene so the falloff is easy to see.
        constants.pointLightPos   = { 14.0f * std::cos(totalSeconds * 0.7f),
                                      4.0f,
                                      14.0f * std::sin(totalSeconds * 0.7f) };
        constants.pointLightRange = 30.0f;
        constants.pointLightColor = { 1.0f, 0.55f, 0.2f }; // warm orange

        memcpy(g_passCBMapped, &constants, sizeof(constants));
    }

    // Written once per OBJECT: its transform and material.
    void UpdateObjectConstants(const Scene& scene, float totalSeconds)
    {
        for (size_t i = 0; i < scene.objects.size() && i < kMaxObjects; ++i)
        {
            const SceneObject& obj = scene.objects[i];

            const XMMATRIX world =
                XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z) *
                XMMatrixRotationY(obj.spinSpeed * totalSeconds) *
                XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);

            // Normals need the INVERSE TRANSPOSE, not the world matrix.
            // Squashing a surface tilts it one way, but squashing its normal
            // the same way tilts it the OTHER way - the two stop being
            // perpendicular. The inverse transpose is exactly the matrix
            // that undoes that. For uniform scale it reduces to the world
            // matrix (up to a factor normalize() removes), which is why the
            // bug stays hidden until something is both non-uniformly scaled
            // AND has faces that are not axis-aligned - hence the squashed
            // pyramid in the scene.
            const XMMATRIX worldInvTranspose =
                XMMatrixTranspose(XMMatrixInverse(nullptr, world));

            ObjectConstants constants;
            // The extra transpose on both is the row-major -> HLSL
            // column-major fix.
            XMStoreFloat4x4(&constants.world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&constants.worldInvTranspose,
                            XMMatrixTranspose(worldInvTranspose));
            constants.diffuseAlbedo = obj.material.diffuseAlbedo;
            constants.specularColor = obj.material.specularColor;
            constants.shininess     = obj.material.shininess;

            memcpy(g_objectCBMapped + i * kObjectCBSize, &constants, sizeof(constants));
        }
    }
}

// ---------------------------------------------------------------- public API

namespace Renderer
{
    void Initialize(HWND hwnd, UINT width, UINT height)
    {
        g_clientWidth  = width;
        g_clientHeight = height;

        EnableDebugLayer();                              // [1] before the device!
        ComPtr<IDXGIFactory6> factory = CreateFactory(); // [2]
        CreateDevice(factory.Get());                     // [3]
        CreateCommandObjects();                          // [4]
        CreateSwapChain(factory.Get(), hwnd);            // [5]
        CreateDescriptorHeaps();                         // [6]
        CreateSyncObjects();                             // [7] needed by [8]
        CreateSizeDependentResources();                  // [8]
        CreateConstantBuffers();                         // [9]
        CreateTexture();                                 // [10]
        CreateRootSignature();                           // [11]
        CreatePipelineState();                           // [12]
    }

    void Shutdown()
    {
        // Never destroy resources the GPU might still be reading.
        if (g_commandQueue && g_fence)
        {
            WaitForGpu();
        }
        if (g_fenceEvent)
        {
            CloseHandle(g_fenceEvent);
            g_fenceEvent = nullptr;
        }
    }

    ID3D12Device* GetDevice()
    {
        return g_device.Get();
    }

    // The only safe order:
    //   1. wait for the GPU  2. release every reference to the old buffers
    //   3. ResizeBuffers     4. recreate views and the depth buffer
    // Skipping step 1 or 2 makes ResizeBuffers fail - the swap chain cannot
    // free buffers anyone still holds.
    void Resize(UINT width, UINT height)
    {
        if (!g_device || width == 0 || height == 0)
        {
            return;
        }

        WaitForGpu();

        for (UINT i = 0; i < kFrameCount; ++i)
        {
            g_renderTargets[i].Reset();
        }
        g_depthStencilBuffer.Reset();

        g_clientWidth  = width;
        g_clientHeight = height;

        ThrowIfFailed(g_swapChain->ResizeBuffers(kFrameCount, g_clientWidth,
                                                 g_clientHeight, kBackBufferFormat, 0),
                      "ResizeBuffers");

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
        CreateSizeDependentResources();
    }

    void Render(const Scene& scene, const Camera& camera, float totalSeconds)
    {
        UpdatePassConstants(camera, totalSeconds);
        UpdateObjectConstants(scene, totalSeconds);

        // Reset = reuse last frame's command memory. Safe only because
        // WaitForGpu() below guarantees the GPU is done with it.
        ThrowIfFailed(g_commandAllocator->Reset(), "Allocator Reset");
        ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), nullptr),
                      "CommandList Reset");

        ID3D12Resource* backBuffer = g_renderTargets[g_frameIndex].Get();

        // The buffer we are about to draw into was just being presented.
        D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
            backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_commandList->ResourceBarrier(1, &toRenderTarget);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += SIZE_T(g_frameIndex) * g_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
            g_dsvHeap->GetCPUDescriptorHandleForHeapStart();

        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        g_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
        // Depth must be cleared to 1.0 (far) every frame, or last frame's
        // depths would reject this frame's pixels.
        g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                             1.0f, 0, 0, nullptr);

        // Command lists are stateless after Reset: root signature, PSO,
        // viewport/scissor, topology and buffers are set every frame.
        g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
        g_commandList->SetPipelineState(g_pipelineState.Get());

        ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        g_commandList->SetGraphicsRootDescriptorTable(
            2, g_srvHeap->GetGPUDescriptorHandleForHeapStart());

        // Bound ONCE for the whole frame - the payoff of splitting the
        // constant buffers by update frequency.
        g_commandList->SetGraphicsRootConstantBufferView(
            1, g_passCB->GetGPUVirtualAddress());

        g_commandList->RSSetViewports(1, &g_viewport);
        g_commandList->RSSetScissorRects(1, &g_scissorRect);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase =
            g_objectCB->GetGPUVirtualAddress();

        for (size_t i = 0; i < scene.objects.size() && i < kMaxObjects; ++i)
        {
            const Mesh& mesh = scene.meshes[scene.objects[i].meshIndex];

            // Point the root CBV at this object's slot - no descriptor heap
            // juggling, just an address.
            g_commandList->SetGraphicsRootConstantBufferView(
                0, objectCBBase + UINT64(i) * kObjectCBSize);

            g_commandList->IASetVertexBuffers(0, 1, &mesh.vbv);
            g_commandList->IASetIndexBuffer(&mesh.ibv);
            g_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        }

        D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
            backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        g_commandList->ResourceBarrier(1, &toPresent);

        ThrowIfFailed(g_commandList->Close(), "CommandList Close");

        // Everything so far was only RECORDED. This hands it to the GPU.
        ID3D12CommandList* lists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, lists);

        // Show the back buffer (1 = wait for vsync), then flush and move to
        // the buffer that just left the screen.
        ThrowIfFailed(g_swapChain->Present(1, 0), "Present");
        WaitForGpu();
        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }
}
