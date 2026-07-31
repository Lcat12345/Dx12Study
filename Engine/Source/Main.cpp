// Main.cpp : Application entry point.
// Phase 7 - Blinn-Phong lighting: vertex normals, materials, directional +
// point light, constant buffers split by update frequency. (see ROADMAP.md)
//
// Controls: WASD = move, Q/E = down/up, hold RIGHT MOUSE = look around,
//           Shift = move faster, Esc = quit.
//
// Everything lives in this one file on purpose: no abstraction until the
// repetition shows up (Phase 9). Initialization functions appear in the
// exact order they are called, so reading top-to-bottom = init order.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    // ---------------------------------------------------------------- settings

    constexpr wchar_t kClassName[]   = L"Dx12EngineWndClass";
    constexpr wchar_t kWindowTitle[] = L"Dx12Engine";

    constexpr UINT  kFrameCount    = 2;
    constexpr float kClearColor[4] = { 0.02f, 0.04f, 0.08f, 1.0f };

    constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT kDepthFormat      = DXGI_FORMAT_D32_FLOAT;

    constexpr UINT kMaxObjects = 32;

    constexpr float kMoveSpeed      = 8.0f;
    constexpr float kFastMultiplier = 3.0f;
    constexpr float kMouseSpeed     = 0.004f;

    // Phase 7: normals join the vertex. Stride is now 32 bytes.
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
    };

    // How light bounces off a surface. Passed through the per-object CB.
    struct Material
    {
        XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
        XMFLOAT3 specularColor = { 0.3f, 0.3f, 0.3f };
        float    shininess     = 32.0f;
    };

    // --- constant buffers, split BY UPDATE FREQUENCY (the Phase 7 lesson) ---
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

    constexpr UINT Align(UINT size, UINT alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }
    constexpr UINT kObjectCBSize = Align(sizeof(ObjectConstants), 256);
    constexpr UINT kPassCBSize   = Align(sizeof(PassConstants), 256);

    struct Mesh
    {
        ComPtr<ID3D12Resource>   vertexBuffer;
        ComPtr<ID3D12Resource>   indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        D3D12_INDEX_BUFFER_VIEW  ibv = {};
        UINT                     indexCount = 0;
    };

    struct SceneObject
    {
        const Mesh* mesh      = nullptr;
        XMFLOAT3    position  = { 0, 0, 0 };
        XMFLOAT3    scale     = { 1, 1, 1 }; // non-uniform on purpose (see below)
        float       spinSpeed = 0.0f;
        Material    material;
    };

    struct Camera
    {
        XMFLOAT3 position = { 0.0f, 3.5f, -22.0f };
        float    yaw      = 0.0f;
        float    pitch    = 0.0f;
    };

    // ---------------------------------------------------------------- state

    UINT g_clientWidth  = 1280;
    UINT g_clientHeight = 720;
    HWND g_hWnd         = nullptr;

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

    Mesh                     g_cubeMesh;
    Mesh                     g_pyramidMesh;
    Mesh                     g_floorMesh;
    std::vector<SceneObject> g_scene;
    Camera                   g_camera;

    bool  g_lookActive  = false;
    POINT g_lastMouse   = {};
    float g_mouseDeltaX = 0.0f;
    float g_mouseDeltaY = 0.0f;

    bool g_resizePending = false;
    bool g_minimized     = false;

    // ---------------------------------------------------------------- helpers

    void ThrowIfFailed(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg), "%s failed (HRESULT 0x%08X)",
                          what, static_cast<unsigned int>(hr));
            throw std::runtime_error(msg);
        }
    }

    D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
                                             D3D12_RESOURCE_STATES before,
                                             D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter  = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }

    std::filesystem::path GetProjectRoot()
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            if (std::filesystem::exists(dir / L"Dx12Engine.slnx"))
            {
                return dir;
            }
            if (dir.parent_path() == dir)
            {
                break;
            }
            dir = dir.parent_path();
        }
        throw std::runtime_error(
            "Could not find Dx12Engine.slnx above the exe - cannot locate assets");
    }

    std::filesystem::path GetShaderDir() { return GetProjectRoot() / L"Engine" / L"Shaders"; }
    std::filesystem::path GetAssetDir()  { return GetProjectRoot() / L"Engine" / L"Assets"; }

    ComPtr<ID3DBlob> CompileShader(const std::filesystem::path& path,
                                   const char* entryPoint, const char* target)
    {
        if (!std::filesystem::exists(path))
        {
            throw std::runtime_error("Shader file not found:\n" + path.string());
        }

        UINT flags = 0;
#if defined(_DEBUG)
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
            if (errors)
            {
                throw std::runtime_error(
                    static_cast<const char*>(errors->GetBufferPointer()));
            }
            ThrowIfFailed(hr, "D3DCompileFromFile");
        }
        return bytecode;
    }

    ComPtr<ID3D12Resource> CreateUploadBuffer(const void* initData, UINT64 byteSize,
                                              const char* debugWhat)
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = byteSize;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> buffer;
        ThrowIfFailed(g_device->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_GENERIC_READ,
                          nullptr, IID_PPV_ARGS(&buffer)),
                      debugWhat);

        if (initData)
        {
            void* mapped = nullptr;
            D3D12_RANGE readRange = {};
            ThrowIfFailed(buffer->Map(0, &readRange, &mapped), "Map");
            memcpy(mapped, initData, static_cast<size_t>(byteSize));
            buffer->Unmap(0, nullptr);
        }
        return buffer;
    }

    Mesh CreateMesh(const Vertex* vertices, UINT vertexCount,
                    const uint16_t* indices, UINT indexCount)
    {
        Mesh mesh;
        const UINT vertexBytes = vertexCount * sizeof(Vertex);
        const UINT indexBytes  = indexCount * sizeof(uint16_t);

        mesh.vertexBuffer = CreateUploadBuffer(vertices, vertexBytes, "Mesh VB");
        mesh.vbv.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
        mesh.vbv.SizeInBytes    = vertexBytes;
        mesh.vbv.StrideInBytes  = sizeof(Vertex);

        mesh.indexBuffer = CreateUploadBuffer(indices, indexBytes, "Mesh IB");
        mesh.ibv.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
        mesh.ibv.SizeInBytes    = indexBytes;
        mesh.ibv.Format         = DXGI_FORMAT_R16_UINT;

        mesh.indexCount = indexCount;
        return mesh;
    }

    // ---------------------------------------------------------------- image load

    struct ImageData
    {
        std::vector<uint8_t> pixels;
        UINT width  = 0;
        UINT height = 0;
    };

    ImageData LoadImageRGBA(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
        {
            throw std::runtime_error("Texture file not found:\n" + path.string());
        }

        ComPtr<IWICImagingFactory> factory;
        ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
                      "WIC factory");

        ComPtr<IWICBitmapDecoder> decoder;
        ThrowIfFailed(factory->CreateDecoderFromFilename(
                          path.c_str(), nullptr, GENERIC_READ,
                          WICDecodeMetadataCacheOnDemand, &decoder),
                      "WIC CreateDecoderFromFilename");

        ComPtr<IWICBitmapFrameDecode> frame;
        ThrowIfFailed(decoder->GetFrame(0, &frame), "WIC GetFrame");

        ComPtr<IWICFormatConverter> converter;
        ThrowIfFailed(factory->CreateFormatConverter(&converter), "WIC CreateFormatConverter");
        ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                            WICBitmapDitherTypeNone, nullptr, 0.0,
                                            WICBitmapPaletteTypeCustom),
                      "WIC converter Initialize");

        ImageData image;
        ThrowIfFailed(converter->GetSize(&image.width, &image.height), "WIC GetSize");

        const UINT rowPitch = image.width * 4;
        image.pixels.resize(size_t(rowPitch) * image.height);
        ThrowIfFailed(converter->CopyPixels(nullptr, rowPitch,
                                            UINT(image.pixels.size()),
                                            image.pixels.data()),
                      "WIC CopyPixels");
        return image;
    }

    // ---------------------------------------------------------------- init steps

    // [1] Debug layer - MUST come before device creation.
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

    // [2] DXGI factory.
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

    // [3] Device.
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
                continue;
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

    // [4] Command objects.
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
        ThrowIfFailed(g_commandList->Close(), "Close command list");
    }

    // [5] Swap chain.
    void CreateSwapChain(IDXGIFactory6* factory, HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.BufferCount      = kFrameCount;
        desc.Width            = g_clientWidth;
        desc.Height           = g_clientHeight;
        desc.Format           = kBackBufferFormat;
        desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
                          g_commandQueue.Get(), hWnd, &desc,
                          nullptr, nullptr, &swapChain1),
                      "CreateSwapChainForHwnd");
        ThrowIfFailed(swapChain1.As(&g_swapChain), "IDXGISwapChain3 query");
        ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER),
                      "MakeWindowAssociation");

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }

    // [6] Descriptor heaps that OUTLIVE a resize.
    void CreateDescriptorHeaps()
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)),
                      "CreateDescriptorHeap(RTV)");
        g_rtvDescriptorSize =
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 1;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&g_dsvHeap)),
                      "CreateDescriptorHeap(DSV)");

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)),
                      "CreateDescriptorHeap(SRV)");
    }

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

    // [7] Everything whose SIZE depends on the window.
    void CreateSizeDependentResources()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i)
        {
            ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])),
                          "SwapChain GetBuffer");
            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize;
        }

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = g_clientWidth;
        desc.Height           = g_clientHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format             = kDepthFormat;
        clearValue.DepthStencil.Depth = 1.0f;

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

    // Resize: wait -> release every reference -> ResizeBuffers -> recreate.
    void OnResize()
    {
        if (!g_device || g_minimized || g_clientWidth == 0 || g_clientHeight == 0)
        {
            return;
        }

        WaitForGpu();

        for (UINT i = 0; i < kFrameCount; ++i)
        {
            g_renderTargets[i].Reset();
        }
        g_depthStencilBuffer.Reset();

        ThrowIfFailed(g_swapChain->ResizeBuffers(kFrameCount, g_clientWidth,
                                                 g_clientHeight, kBackBufferFormat, 0),
                      "ResizeBuffers");

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
        CreateSizeDependentResources();
    }

    // [8] Fence.
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

    // [9] TWO constant buffers now - one per update frequency.
    void CreateConstantBuffers()
    {
        g_objectCB = CreateUploadBuffer(nullptr, UINT64(kObjectCBSize) * kMaxObjects,
                                        "CreateCommittedResource(ObjectCB)");
        D3D12_RANGE readRange = {};
        ThrowIfFailed(g_objectCB->Map(0, &readRange,
                                      reinterpret_cast<void**>(&g_objectCBMapped)),
                      "ObjectCB Map");

        g_passCB = CreateUploadBuffer(nullptr, kPassCBSize,
                                      "CreateCommittedResource(PassCB)");
        ThrowIfFailed(g_passCB->Map(0, &readRange,
                                    reinterpret_cast<void**>(&g_passCBMapped)),
                      "PassCB Map");
    }

    // [10] Texture: CPU -> upload heap -> default heap (see Phase 5).
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
        texDesc.MipLevels        = 1;
        texDesc.Format           = kBackBufferFormat;
        texDesc.SampleDesc.Count = 1;

        ThrowIfFailed(g_device->CreateCommittedResource(
                          &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                          D3D12_RESOURCE_STATE_COPY_DEST,
                          nullptr, IID_PPV_ARGS(&g_texture)),
                      "CreateCommittedResource(Texture)");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT   numRows        = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 uploadSize     = 0;
        g_device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                        &footprint, &numRows, &rowSizeInBytes,
                                        &uploadSize);

        ComPtr<ID3D12Resource> uploadBuffer =
            CreateUploadBuffer(nullptr, uploadSize, "CreateCommittedResource(TexUpload)");

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

        D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
            g_texture.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        g_commandList->ResourceBarrier(1, &toShaderResource);

        ThrowIfFailed(g_commandList->Close(), "CommandList Close (texture)");
        ID3D12CommandList* lists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, lists);
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

    // [11] Root signature: b0 object CB, b1 pass CB, t0 texture, s0 sampler.
    //      Both CBs are visible to ALL stages now - the VS needs the
    //      matrices, the PS needs the material and the lights.
    void CreateRootSignature()
    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0; // t0

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

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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

    // [12] PSO - input layout gains NORMAL between POSITION and TEXCOORD.
    void CreatePipelineState()
    {
        const std::filesystem::path shaderFile = GetShaderDir() / L"Basic.hlsl";
        ComPtr<ID3DBlob> vs = CompileShader(shaderFile, "VSMain", "vs_5_0");
        ComPtr<ID3DBlob> ps = CompileShader(shaderFile, "PSMain", "ps_5_0");

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
        rasterizer.CullMode              = D3D12_CULL_MODE_BACK;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthClipEnable       = TRUE;

        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable           = FALSE;
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

    // [13] Meshes and scene. Each cube face carries its own flat normal -
    //      possible only because the 24-vertex layout already gives every
    //      face its own copy of each corner.
    void CreateScene()
    {
        const XMFLOAT3 nFront = {  0,  0, -1 };
        const XMFLOAT3 nBack  = {  0,  0, +1 };
        const XMFLOAT3 nLeft  = { -1,  0,  0 };
        const XMFLOAT3 nRight = { +1,  0,  0 };
        const XMFLOAT3 nUp    = {  0, +1,  0 };
        const XMFLOAT3 nDown  = {  0, -1,  0 };

        const Vertex cubeVertices[] = {
            // front (-Z)
            { { -1, +1, -1 }, nFront, { 0, 0 } }, { { +1, +1, -1 }, nFront, { 1, 0 } },
            { { +1, -1, -1 }, nFront, { 1, 1 } }, { { -1, -1, -1 }, nFront, { 0, 1 } },
            // back (+Z)
            { { +1, +1, +1 }, nBack,  { 0, 0 } }, { { -1, +1, +1 }, nBack,  { 1, 0 } },
            { { -1, -1, +1 }, nBack,  { 1, 1 } }, { { +1, -1, +1 }, nBack,  { 0, 1 } },
            // left (-X)
            { { -1, +1, +1 }, nLeft,  { 0, 0 } }, { { -1, +1, -1 }, nLeft,  { 1, 0 } },
            { { -1, -1, -1 }, nLeft,  { 1, 1 } }, { { -1, -1, +1 }, nLeft,  { 0, 1 } },
            // right (+X)
            { { +1, +1, -1 }, nRight, { 0, 0 } }, { { +1, +1, +1 }, nRight, { 1, 0 } },
            { { +1, -1, +1 }, nRight, { 1, 1 } }, { { +1, -1, -1 }, nRight, { 0, 1 } },
            // top (+Y)
            { { -1, +1, +1 }, nUp,    { 0, 0 } }, { { +1, +1, +1 }, nUp,    { 1, 0 } },
            { { +1, +1, -1 }, nUp,    { 1, 1 } }, { { -1, +1, -1 }, nUp,    { 0, 1 } },
            // bottom (-Y)
            { { -1, -1, -1 }, nDown,  { 0, 0 } }, { { +1, -1, -1 }, nDown,  { 1, 0 } },
            { { +1, -1, +1 }, nDown,  { 1, 1 } }, { { -1, -1, +1 }, nDown,  { 0, 1 } },
        };

        std::vector<uint16_t> cubeIndices;
        cubeIndices.reserve(36);
        for (uint16_t face = 0; face < 6; ++face)
        {
            const uint16_t base = face * 4;
            cubeIndices.insert(cubeIndices.end(),
                               { uint16_t(base + 0), uint16_t(base + 1), uint16_t(base + 2),
                                 uint16_t(base + 0), uint16_t(base + 2), uint16_t(base + 3) });
        }
        g_cubeMesh = CreateMesh(cubeVertices, _countof(cubeVertices),
                                cubeIndices.data(), UINT(cubeIndices.size()));

        // --- pyramid: SLANTED faces, which is the whole point.
        //     A cube's normals all point straight down an axis, so an
        //     axis-aligned squash leaves them pointing the same way once
        //     renormalized - the inverse transpose changes nothing there.
        //     A diagonal normal is different: squash it with the world
        //     matrix and it stops being perpendicular to its own surface.
        //     This is the shape that makes the inverse transpose visible.
        const float kSlope = 0.4472f; // normalize(1, 2) -> y component
        const float kSide  = 0.8944f; // ...and the sideways component
        const XMFLOAT3 pFront = {  0,      kSlope, -kSide };
        const XMFLOAT3 pRight = {  kSide,  kSlope,  0     };
        const XMFLOAT3 pBack  = {  0,      kSlope,  kSide };
        const XMFLOAT3 pLeft  = { -kSide,  kSlope,  0     };

        const Vertex pyramidVertices[] = {
            // each side face: apex, then the two base corners (clockwise
            // seen from outside), with its own flat normal
            { {  0, +1,  0 }, pFront, { 0.5f, 0 } },
            { { +1, -1, -1 }, pFront, { 1,    1 } },
            { { -1, -1, -1 }, pFront, { 0,    1 } },

            { {  0, +1,  0 }, pRight, { 0.5f, 0 } },
            { { +1, -1, +1 }, pRight, { 1,    1 } },
            { { +1, -1, -1 }, pRight, { 0,    1 } },

            { {  0, +1,  0 }, pBack,  { 0.5f, 0 } },
            { { -1, -1, +1 }, pBack,  { 1,    1 } },
            { { +1, -1, +1 }, pBack,  { 0,    1 } },

            { {  0, +1,  0 }, pLeft,  { 0.5f, 0 } },
            { { -1, -1, -1 }, pLeft,  { 1,    1 } },
            { { -1, -1, +1 }, pLeft,  { 0,    1 } },

            // base, facing down
            { { -1, -1, -1 }, nDown,  { 0, 0 } }, { { +1, -1, -1 }, nDown, { 1, 0 } },
            { { +1, -1, +1 }, nDown,  { 1, 1 } }, { { -1, -1, +1 }, nDown, { 0, 1 } },
        };
        const uint16_t pyramidIndices[] = {
            0, 1, 2,   3, 4, 5,   6, 7, 8,   9, 10, 11, // sides
            12, 13, 14,  12, 14, 15,                    // base
        };
        g_pyramidMesh = CreateMesh(pyramidVertices, _countof(pyramidVertices),
                                   pyramidIndices, _countof(pyramidIndices));

        constexpr float kHalf = 40.0f;
        constexpr float kTile = 20.0f;
        const Vertex floorVertices[] = {
            { { -kHalf, 0, +kHalf }, nUp, { 0,     0     } },
            { { +kHalf, 0, +kHalf }, nUp, { kTile, 0     } },
            { { +kHalf, 0, -kHalf }, nUp, { kTile, kTile } },
            { { -kHalf, 0, -kHalf }, nUp, { 0,     kTile } },
        };
        const uint16_t floorIndices[] = { 0, 1, 2, 0, 2, 3 };
        g_floorMesh = CreateMesh(floorVertices, _countof(floorVertices),
                                 floorIndices, _countof(floorIndices));

        // --- scene ---
        // Floor: rough and wide, barely any highlight.
        SceneObject floor;
        floor.mesh                    = &g_floorMesh;
        floor.material.specularColor  = { 0.05f, 0.05f, 0.05f };
        floor.material.shininess      = 8.0f;
        g_scene.push_back(floor);

        // Cubes with deliberately different materials, so the same lights
        // produce visibly different surfaces.
        int index = 0;
        for (int z = -1; z <= 1; ++z)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && z == 0)
                {
                    continue;
                }
                SceneObject cube;
                cube.mesh      = &g_cubeMesh;
                cube.position  = { x * 8.0f, 1.0f, z * 8.0f };
                cube.spinSpeed = 0.3f + 0.15f * float(x + z * 3);

                // Shininess ramps 4 -> 256 across the grid: the low end is a
                // broad dull sheen, the high end a small sharp highlight.
                cube.material.shininess     = 4.0f * std::pow(2.0f, float(index) * 0.75f);
                cube.material.specularColor = { 0.6f, 0.6f, 0.6f };
                g_scene.push_back(cube);
                ++index;
            }
        }

        // A wide flat billboard, just to have a big surface catching light.
        SceneObject wall;
        wall.mesh                   = &g_cubeMesh;
        wall.position               = { 0.0f, 3.0f, 20.0f };
        wall.scale                  = { 6.0f, 3.0f, 0.5f };
        wall.material.diffuseAlbedo = { 1.0f, 0.85f, 0.7f, 1.0f };
        wall.material.specularColor = { 0.4f, 0.4f, 0.4f };
        wall.material.shininess     = 64.0f;
        g_scene.push_back(wall);

        // Two pyramids side by side: one untouched, one squashed flat on Y.
        // The squashed one only shades correctly because its normals go
        // through the inverse transpose. Replace gWorldInvTranspose with
        // gWorld in Basic.hlsl and re-run (shaders compile at startup, so
        // no rebuild needed) - the squashed pyramid changes, the plain one
        // and every cube stay exactly the same.
        SceneObject pyramid;
        pyramid.mesh                   = &g_pyramidMesh;
        pyramid.position               = { -6.0f, 2.0f, -13.0f };
        pyramid.scale                  = { 2.0f, 2.0f, 2.0f }; // uniform
        pyramid.material.specularColor = { 0.5f, 0.5f, 0.5f };
        pyramid.material.shininess     = 48.0f;
        g_scene.push_back(pyramid);

        // Squashed flat: its slanted faces are now nearly horizontal, so
        // their normals must swing to nearly straight up. Using gWorld
        // instead swings them the opposite way (nearly horizontal), and the
        // pyramid shades as if it were still steep.
        SceneObject squashed = pyramid;
        squashed.position = { 6.0f, 0.9f, -13.0f };
        squashed.scale    = { 4.0f, 0.8f, 4.0f };
        g_scene.push_back(squashed);
    }

    // ---------------------------------------------------------------- per frame

    XMVECTOR CameraForward()
    {
        const float cosPitch = std::cos(g_camera.pitch);
        return XMVector3Normalize(XMVectorSet(cosPitch * std::sin(g_camera.yaw),
                                              std::sin(g_camera.pitch),
                                              cosPitch * std::cos(g_camera.yaw),
                                              0.0f));
    }

    void UpdateCamera(float dt)
    {
        g_camera.yaw   += g_mouseDeltaX * kMouseSpeed;
        g_camera.pitch -= g_mouseDeltaY * kMouseSpeed;
        g_mouseDeltaX = 0.0f;
        g_mouseDeltaY = 0.0f;

        constexpr float kPitchLimit = XM_PIDIV2 - 0.01f;
        g_camera.pitch = std::clamp(g_camera.pitch, -kPitchLimit, kPitchLimit);

        const XMVECTOR forward = CameraForward();
        const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR right   = XMVector3Normalize(XMVector3Cross(worldUp, forward));

        auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

        XMVECTOR move = XMVectorZero();
        if (down('W')) move = XMVectorAdd(move, forward);
        if (down('S')) move = XMVectorSubtract(move, forward);
        if (down('D')) move = XMVectorAdd(move, right);
        if (down('A')) move = XMVectorSubtract(move, right);
        if (down('E')) move = XMVectorAdd(move, worldUp);
        if (down('Q')) move = XMVectorSubtract(move, worldUp);

        if (XMVectorGetX(XMVector3LengthSq(move)) > 0.0f)
        {
            float speed = kMoveSpeed;
            if (down(VK_SHIFT))
            {
                speed *= kFastMultiplier;
            }
            move = XMVectorScale(XMVector3Normalize(move), speed * dt);

            XMVECTOR pos = XMLoadFloat3(&g_camera.position);
            XMStoreFloat3(&g_camera.position, XMVectorAdd(pos, move));
        }
    }

    // Written ONCE per frame: camera and lights are shared by every object.
    void UpdatePassConstants(float totalSeconds)
    {
        const XMVECTOR eye     = XMLoadFloat3(&g_camera.position);
        const XMVECTOR forward = CameraForward();
        const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            float(g_clientWidth) / float(g_clientHeight),
            0.1f, 200.0f);

        PassConstants constants;
        XMStoreFloat4x4(&constants.viewProj, XMMatrixTranspose(view * proj));
        constants.eyePosW = g_camera.position;

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
    void UpdateObjectConstants(float totalSeconds)
    {
        for (size_t i = 0; i < g_scene.size() && i < kMaxObjects; ++i)
        {
            const SceneObject& obj = g_scene[i];

            const XMMATRIX world =
                XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z) *
                XMMatrixRotationY(obj.spinSpeed * totalSeconds) *
                XMMatrixTranslation(obj.position.x, obj.position.y, obj.position.z);

            // Normals need the INVERSE TRANSPOSE, not the world matrix.
            // Why: squashing a surface tilts it one way, but squashing its
            // normal the same way tilts it the OTHER way - the two stop
            // being perpendicular. The inverse transpose is exactly the
            // matrix that undoes that. For uniform scale it reduces to the
            // world matrix (up to a scale factor that normalize() removes),
            // which is why the bug stays hidden until something is both
            // non-uniformly scaled AND has faces that are not axis-aligned
            // - hence the squashed pyramid in the scene.
            const XMMATRIX worldInvTranspose =
                XMMatrixTranspose(XMMatrixInverse(nullptr, world));

            ObjectConstants constants;
            // The extra transpose on both is the usual row-major -> HLSL
            // column-major fix from Phase 4.
            XMStoreFloat4x4(&constants.world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&constants.worldInvTranspose,
                            XMMatrixTranspose(worldInvTranspose));
            constants.diffuseAlbedo = obj.material.diffuseAlbedo;
            constants.specularColor = obj.material.specularColor;
            constants.shininess     = obj.material.shininess;

            memcpy(g_objectCBMapped + i * kObjectCBSize, &constants, sizeof(constants));
        }
    }

    void Render()
    {
        ThrowIfFailed(g_commandAllocator->Reset(), "Allocator Reset");
        ThrowIfFailed(g_commandList->Reset(g_commandAllocator.Get(), nullptr),
                      "CommandList Reset");

        ID3D12Resource* backBuffer = g_renderTargets[g_frameIndex].Get();

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
        g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                             1.0f, 0, 0, nullptr);

        g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
        g_commandList->SetPipelineState(g_pipelineState.Get());

        ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        g_commandList->SetGraphicsRootDescriptorTable(
            2, g_srvHeap->GetGPUDescriptorHandleForHeapStart());

        // Bound ONCE for the whole frame - that is the payoff of splitting
        // the constant buffers by update frequency.
        g_commandList->SetGraphicsRootConstantBufferView(
            1, g_passCB->GetGPUVirtualAddress());

        g_commandList->RSSetViewports(1, &g_viewport);
        g_commandList->RSSetScissorRects(1, &g_scissorRect);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const D3D12_GPU_VIRTUAL_ADDRESS objectCBBase =
            g_objectCB->GetGPUVirtualAddress();

        for (size_t i = 0; i < g_scene.size() && i < kMaxObjects; ++i)
        {
            const Mesh& mesh = *g_scene[i].mesh;

            // Only this changes per object.
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

        ID3D12CommandList* lists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(1, lists);

        ThrowIfFailed(g_swapChain->Present(1, 0), "Present");
        WaitForGpu();
        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    }

    // ---------------------------------------------------------------- window

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
        {
            g_minimized = (wParam == SIZE_MINIMIZED);
            const UINT width  = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (!g_minimized && width > 0 && height > 0)
            {
                g_clientWidth  = width;
                g_clientHeight = height;
                g_resizePending = true;
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
            g_lookActive = true;
            GetCursorPos(&g_lastMouse);
            SetCapture(hWnd);
            return 0;

        case WM_RBUTTONUP:
            g_lookActive = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE:
            if (g_lookActive)
            {
                POINT current;
                GetCursorPos(&current);
                g_mouseDeltaX += float(current.x - g_lastMouse.x);
                g_mouseDeltaY += float(current.y - g_lastMouse.y);
                g_lastMouse = current;
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    HWND CreateMainWindow(HINSTANCE hInstance)
    {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc))
        {
            return nullptr;
        }

        RECT windowRect = { 0, 0, LONG(g_clientWidth), LONG(g_clientHeight) };
        const DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRect(&windowRect, style, FALSE);

        return CreateWindowExW(
            0, kClassName, kWindowTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr, hInstance, nullptr);
    }

    class Timer
    {
    public:
        Timer()
        {
            QueryPerformanceFrequency(&m_frequency);
            QueryPerformanceCounter(&m_start);
            m_previous = m_start;
        }

        float Tick()
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            const float dt = float(double(now.QuadPart - m_previous.QuadPart) /
                                   double(m_frequency.QuadPart));
            m_previous = now;
            return dt;
        }

        float TotalSeconds() const
        {
            return float(double(m_previous.QuadPart - m_start.QuadPart) /
                         double(m_frequency.QuadPart));
        }

    private:
        LARGE_INTEGER m_frequency = {};
        LARGE_INTEGER m_start     = {};
        LARGE_INTEGER m_previous  = {};
    };

    void UpdateTitleFps(float dt)
    {
        static float accumulated = 0.0f;
        static int   frames      = 0;

        accumulated += dt;
        ++frames;
        if (accumulated >= 1.0f)
        {
            wchar_t title[128];
            std::swprintf(title, _countof(title),
                          L"%ls   %d fps   %.2f ms   %ux%u",
                          kWindowTitle, frames, 1000.0f * accumulated / float(frames),
                          g_clientWidth, g_clientHeight);
            SetWindowTextW(g_hWnd, title);
            accumulated = 0.0f;
            frames      = 0;
        }
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");

    g_hWnd = CreateMainWindow(hInstance);
    if (!g_hWnd)
    {
        return -1;
    }

    try
    {
        EnableDebugLayer();                              // [1]
        ComPtr<IDXGIFactory6> factory = CreateFactory(); // [2]
        CreateDevice(factory.Get());                     // [3]
        CreateCommandObjects();                          // [4]
        CreateSwapChain(factory.Get(), g_hWnd);          // [5]
        CreateDescriptorHeaps();                         // [6]
        CreateSyncObjects();                             // [8] (needed by [7])
        CreateSizeDependentResources();                  // [7]
        CreateConstantBuffers();                         // [9]
        CreateTexture();                                 // [10]
        CreateRootSignature();                           // [11]
        CreatePipelineState();                           // [12]
        CreateScene();                                   // [13]
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "D3D12 initialization failed",
                    MB_OK | MB_ICONERROR);
        return -1;
    }

    ShowWindow(g_hWnd, nCmdShow);

    Timer timer;

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        if (g_minimized)
        {
            Sleep(16);
            continue;
        }

        if (g_resizePending)
        {
            g_resizePending = false;
            OnResize();
        }

        const float dt = timer.Tick();
        UpdateCamera(dt);
        UpdatePassConstants(timer.TotalSeconds());
        UpdateObjectConstants(timer.TotalSeconds());
        Render();
        UpdateTitleFps(dt);
    }

    WaitForGpu();
    CloseHandle(g_fenceEvent);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
