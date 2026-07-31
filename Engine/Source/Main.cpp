// Main.cpp : Application entry point.
// Phase 5 - texture mapping: image loading, upload heap -> default heap,
// SRV + static sampler. (see ROADMAP.md)
//
// Everything lives in this one file on purpose: no abstraction until the
// repetition shows up (Phase 9). Initialization functions appear in the
// exact order they are called, so reading top-to-bottom = init order.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wincodec.h>   // WIC - the image decoder built into Windows
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
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
    constexpr int     kClientWidth   = 1280;
    constexpr int     kClientHeight  = 720;

    constexpr UINT  kFrameCount    = 2;
    constexpr float kClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };

    constexpr DXGI_FORMAT kDepthFormat   = DXGI_FORMAT_D32_FLOAT;
    constexpr DXGI_FORMAT kTextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Slots inside the one shader-visible descriptor heap.
    constexpr UINT kCbvHeapIndex = 0;
    constexpr UINT kSrvHeapIndex = 1;
    constexpr UINT kSrvCbvHeapSize = 2;

    constexpr D3D12_VIEWPORT kViewport = {
        0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 1.0f };
    constexpr D3D12_RECT kScissorRect = { 0, 0, kClientWidth, kClientHeight };

    // Phase 5: color is gone, UV takes its place.
    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT2 uv;
    };

    struct ObjectConstants
    {
        XMFLOAT4X4 worldViewProj;
    };

    constexpr UINT Align(UINT size, UINT alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }
    constexpr UINT kObjectCBSize = Align(sizeof(ObjectConstants), 256);

    // ---------------------------------------------------------------- D3D12 state

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

    ComPtr<ID3D12Resource>   g_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
    ComPtr<ID3D12Resource>   g_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW  g_indexBufferView = {};
    UINT                     g_indexCount = 0;

    ComPtr<ID3D12Resource>       g_depthStencilBuffer;
    ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

    // One shader-visible heap now holds BOTH the CBV and the texture SRV.
    ComPtr<ID3D12Resource>       g_constantBuffer;
    ComPtr<ID3D12DescriptorHeap> g_srvCbvHeap;
    UINT                         g_srvCbvDescriptorSize = 0;
    uint8_t*                     g_constantBufferMapped = nullptr;

    ComPtr<ID3D12Resource> g_texture;

    ComPtr<ID3D12Fence> g_fence;
    UINT64              g_fenceValue = 0;
    HANDLE              g_fenceEvent = nullptr;

    UINT g_frameIndex = 0;

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

    D3D12_CPU_DESCRIPTOR_HANDLE SrvCbvCpuHandle(UINT index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h =
            g_srvCbvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(index) * g_srvCbvDescriptorSize;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE SrvCbvGpuHandle(UINT index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h =
            g_srvCbvHeap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += UINT64(index) * g_srvCbvDescriptorSize;
        return h;
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

    // ---------------------------------------------------------------- image load

    struct ImageData
    {
        std::vector<uint8_t> pixels; // tightly packed RGBA8
        UINT width  = 0;
        UINT height = 0;
    };

    // Decoded with WIC, the imaging component that ships with Windows - no
    // third-party library needed, and it handles PNG/JPG/BMP/TIFF alike.
    // (Swapping in stb_image later means replacing only this function.)
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

        // Whatever the file's native format is, force it to plain RGBA8 so
        // it matches DXGI_FORMAT_R8G8B8A8_UNORM on the GPU side.
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
        desc.Width            = kClientWidth;
        desc.Height           = kClientHeight;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    // [6] RTV heap + one RTV per back buffer.
    void CreateRenderTargets()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = kFrameCount;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_rtvHeap)),
                      "CreateDescriptorHeap(RTV)");

        g_rtvDescriptorSize =
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kFrameCount; ++i)
        {
            ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])),
                          "SwapChain GetBuffer");
            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                             rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize;
        }
    }

    // [7] Depth buffer (we own this one, unlike the back buffers).
    void CreateDepthStencilBuffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDesc.NumDescriptors = 1;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_dsvHeap)),
                      "CreateDescriptorHeap(DSV)");

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = kClientWidth;
        desc.Height           = kClientHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format               = kDepthFormat;
        clearValue.DepthStencil.Depth   = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        ThrowIfFailed(g_device->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_DEPTH_WRITE,
                          &clearValue, IID_PPV_ARGS(&g_depthStencilBuffer)),
                      "CreateCommittedResource(DepthStencil)");

        g_device->CreateDepthStencilView(
            g_depthStencilBuffer.Get(), nullptr,
            g_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // [8] Fence for CPU-GPU sync.
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

    // [9] Shader-visible heap (CBV at slot 0, SRV at slot 1) + constant buffer.
    void CreateConstantBuffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kSrvCbvHeapSize;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_srvCbvHeap)),
                      "CreateDescriptorHeap(SRV/CBV)");

        g_srvCbvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        g_constantBuffer = CreateUploadBuffer(nullptr, kObjectCBSize,
                                              "CreateCommittedResource(CB)");

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes    = kObjectCBSize;
        g_device->CreateConstantBufferView(&cbvDesc, SrvCbvCpuHandle(kCbvHeapIndex));

        D3D12_RANGE readRange = {};
        ThrowIfFailed(g_constantBuffer->Map(
                          0, &readRange, reinterpret_cast<void**>(&g_constantBufferMapped)),
                      "CB Map");
    }

    // [10] Texture. THE Phase 5 lesson: a GPU-local (default heap) resource
    //      cannot be written by the CPU, so the data takes two hops:
    //        CPU memory -> upload heap (CPU-writable) -> default heap (GPU-local)
    //      The second hop is a GPU copy command, so it must be recorded,
    //      submitted, and waited on before the texture can be sampled.
    void CreateTexture()
    {
        const ImageData image = LoadImageRGBA(GetAssetDir() / L"Crate.png");

        // --- the destination: GPU-local, starts in COPY_DEST state ---
        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = image.width;
        texDesc.Height           = image.height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1; // no mipmaps yet
        texDesc.Format           = kTextureFormat;
        texDesc.SampleDesc.Count = 1;

        ThrowIfFailed(g_device->CreateCommittedResource(
                          &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                          D3D12_RESOURCE_STATE_COPY_DEST,
                          nullptr, IID_PPV_ARGS(&g_texture)),
                      "CreateCommittedResource(Texture)");

        // --- how the GPU expects the pixels laid out in the upload buffer ---
        // Textures are NOT tightly packed: each row must start on a
        // 256-byte boundary. GetCopyableFootprints computes the exact
        // layout instead of us guessing.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT   numRows        = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 uploadSize     = 0;
        g_device->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                        &footprint, &numRows, &rowSizeInBytes,
                                        &uploadSize);

        ComPtr<ID3D12Resource> uploadBuffer =
            CreateUploadBuffer(nullptr, uploadSize, "CreateCommittedResource(TexUpload)");

        // Copy ROW BY ROW: the source is tightly packed (width*4), the
        // destination has padding at the end of every row.
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

        // --- record and run the GPU copy ---
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

        // uploadBuffer is a local ComPtr - it dies at the end of this
        // function, so we MUST wait for the GPU to finish reading it first.
        WaitForGpu();

        // --- describe the texture to the shader ---
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                  = kTextureFormat;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels     = 1;
        g_device->CreateShaderResourceView(g_texture.Get(), &srvDesc,
                                           SrvCbvCpuHandle(kSrvHeapIndex));
    }

    // [11] Root signature: table 0 -> CBV (b0, VS), table 1 -> SRV (t0, PS),
    //      plus a STATIC sampler (s0) baked into the signature itself.
    void CreateRootSignature()
    {
        D3D12_DESCRIPTOR_RANGE cbvRange = {};
        cbvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        cbvRange.NumDescriptors     = 1;
        cbvRange.BaseShaderRegister = 0; // b0

        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = 1;
        srvRange.BaseShaderRegister = 0; // t0

        D3D12_ROOT_PARAMETER rootParams[2] = {};
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[0].DescriptorTable.pDescriptorRanges   = &cbvRange;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // A static sampler lives in the root signature, not in a heap - the
        // common case, since most samplers never change at runtime.
        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // smooth
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // tile outside [0,1]
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

    // [12] PSO - input layout now carries UV instead of color.
    void CreatePipelineState()
    {
        const std::filesystem::path shaderFile = GetShaderDir() / L"Basic.hlsl";
        ComPtr<ID3DBlob> vs = CompileShader(shaderFile, "VSMain", "vs_5_0");
        ComPtr<ID3DBlob> ps = CompileShader(shaderFile, "PSMain", "ps_5_0");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12,
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
        psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count      = 1;

        ThrowIfFailed(g_device->CreateGraphicsPipelineState(
                          &psoDesc, IID_PPV_ARGS(&g_pipelineState)),
                      "CreateGraphicsPipelineState");
    }

    // [13] Cube geometry - now 24 vertices, not 8.
    //      A corner is shared by three faces, but each face needs its OWN uv
    //      at that corner, and a vertex can only carry one uv. So the corner
    //      must be duplicated per face. (Same problem OBJ files hit in
    //      Phase 8: separate position/uv/normal indices have to be merged.)
    void CreateCubeBuffers()
    {
        // Each face lists its corners as top-left, top-right, bottom-right,
        // bottom-left AS SEEN FROM OUTSIDE, which makes the winding
        // clockwise - matching the rasterizer's "clockwise = front".
        const Vertex vertices[] = {
            // front (-Z)
            { { -1, +1, -1 }, { 0, 0 } }, { { +1, +1, -1 }, { 1, 0 } },
            { { +1, -1, -1 }, { 1, 1 } }, { { -1, -1, -1 }, { 0, 1 } },
            // back (+Z)
            { { +1, +1, +1 }, { 0, 0 } }, { { -1, +1, +1 }, { 1, 0 } },
            { { -1, -1, +1 }, { 1, 1 } }, { { +1, -1, +1 }, { 0, 1 } },
            // left (-X)
            { { -1, +1, +1 }, { 0, 0 } }, { { -1, +1, -1 }, { 1, 0 } },
            { { -1, -1, -1 }, { 1, 1 } }, { { -1, -1, +1 }, { 0, 1 } },
            // right (+X)
            { { +1, +1, -1 }, { 0, 0 } }, { { +1, +1, +1 }, { 1, 0 } },
            { { +1, -1, +1 }, { 1, 1 } }, { { +1, -1, -1 }, { 0, 1 } },
            // top (+Y)
            { { -1, +1, +1 }, { 0, 0 } }, { { +1, +1, +1 }, { 1, 0 } },
            { { +1, +1, -1 }, { 1, 1 } }, { { -1, +1, -1 }, { 0, 1 } },
            // bottom (-Y)
            { { -1, -1, -1 }, { 0, 0 } }, { { +1, -1, -1 }, { 1, 0 } },
            { { +1, -1, +1 }, { 1, 1 } }, { { -1, -1, +1 }, { 0, 1 } },
        };

        // Every face is the same two triangles over its 4 vertices.
        std::vector<uint16_t> indices;
        indices.reserve(36);
        for (uint16_t face = 0; face < 6; ++face)
        {
            const uint16_t base = face * 4;
            indices.insert(indices.end(),
                           { uint16_t(base + 0), uint16_t(base + 1), uint16_t(base + 2),
                             uint16_t(base + 0), uint16_t(base + 2), uint16_t(base + 3) });
        }

        g_vertexBuffer = CreateUploadBuffer(vertices, sizeof(vertices),
                                            "CreateCommittedResource(VB)");
        g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
        g_vertexBufferView.SizeInBytes    = sizeof(vertices);
        g_vertexBufferView.StrideInBytes  = sizeof(Vertex);

        const UINT indexBytes = UINT(indices.size() * sizeof(uint16_t));
        g_indexBuffer = CreateUploadBuffer(indices.data(), indexBytes,
                                           "CreateCommittedResource(IB)");
        g_indexBufferView.BufferLocation = g_indexBuffer->GetGPUVirtualAddress();
        g_indexBufferView.SizeInBytes    = indexBytes;
        g_indexBufferView.Format         = DXGI_FORMAT_R16_UINT;
        g_indexCount = UINT(indices.size());
    }

    // ---------------------------------------------------------------- per frame

    void UpdateConstantBuffer(float totalSeconds)
    {
        XMMATRIX world = XMMatrixRotationY(totalSeconds) *
                         XMMatrixRotationX(totalSeconds * 0.5f);

        XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 2.0f, -6.0f, 1.0f),
                                         XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
                                         XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

        XMMATRIX proj = XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            float(kClientWidth) / float(kClientHeight),
            0.1f, 100.0f);

        // DirectXMath is row-major, HLSL reads column-major -> transpose.
        ObjectConstants constants;
        XMStoreFloat4x4(&constants.worldViewProj,
                        XMMatrixTranspose(world * view * proj));

        memcpy(g_constantBufferMapped, &constants, sizeof(constants));
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

        ID3D12DescriptorHeap* heaps[] = { g_srvCbvHeap.Get() };
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        // Two tables now: the matrix for the VS, the texture for the PS.
        g_commandList->SetGraphicsRootDescriptorTable(0, SrvCbvGpuHandle(kCbvHeapIndex));
        g_commandList->SetGraphicsRootDescriptorTable(1, SrvCbvGpuHandle(kSrvHeapIndex));

        g_commandList->RSSetViewports(1, &kViewport);
        g_commandList->RSSetScissorRects(1, &kScissorRect);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
        g_commandList->IASetIndexBuffer(&g_indexBufferView);
        g_commandList->DrawIndexedInstanced(g_indexCount, 1, 0, 0, 0);

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
            // Swap chain + depth buffer resize lands here in Phase 6.
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

        RECT windowRect = { 0, 0, kClientWidth, kClientHeight };
        const DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRect(&windowRect, style, FALSE);

        return CreateWindowExW(
            0, kClassName, kWindowTitle, style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr, hInstance, nullptr);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // WIC is a COM library, so COM must be running before we use it.
    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");

    HWND hWnd = CreateMainWindow(hInstance);
    if (!hWnd)
    {
        return -1;
    }

    try
    {
        EnableDebugLayer();                              // [1]
        ComPtr<IDXGIFactory6> factory = CreateFactory(); // [2]
        CreateDevice(factory.Get());                     // [3]
        CreateCommandObjects();                          // [4]
        CreateSwapChain(factory.Get(), hWnd);            // [5]
        CreateRenderTargets();                           // [6]
        CreateDepthStencilBuffer();                      // [7]
        CreateSyncObjects();                             // [8]
        CreateConstantBuffer();                          // [9]
        CreateTexture();                                 // [10] needs [4] and [8]
        CreateRootSignature();                           // [11]
        CreatePipelineState();                           // [12]
        CreateCubeBuffers();                             // [13]
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "D3D12 initialization failed",
                    MB_OK | MB_ICONERROR);
        return -1;
    }

    ShowWindow(hWnd, nCmdShow);

    // Phase 6 replaces this with a proper high-resolution timer.
    const ULONGLONG startTime = GetTickCount64();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            UpdateConstantBuffer((GetTickCount64() - startTime) / 1000.0f);
            Render();
        }
    }

    WaitForGpu();
    CloseHandle(g_fenceEvent);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
