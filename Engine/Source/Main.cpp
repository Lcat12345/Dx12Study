// Main.cpp : Application entry point.
// Phase 4 - index buffer, constant buffer, depth buffer, rotating cube.
// (see ROADMAP.md; earlier phases' comments trimmed - git history keeps them)
//
// Everything lives in this one file on purpose: no abstraction until the
// repetition shows up (Phase 9). Initialization functions appear in the
// exact order they are called, so reading top-to-bottom = init order.

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <cstdio>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

    constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

    constexpr D3D12_VIEWPORT kViewport = {
        0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 1.0f };
    constexpr D3D12_RECT kScissorRect = { 0, 0, kClientWidth, kClientHeight };

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT4 color;
    };

    // Mirrors the cbuffer in Basic.hlsl. Constant buffers must be a multiple
    // of 256 bytes, so this is padded up - see CreateConstantBuffer.
    struct ObjectConstants
    {
        XMFLOAT4X4 worldViewProj;
    };

    // Round a size up to the next multiple of 256 (hardware requirement for
    // constant buffer views).
    constexpr UINT Align256(UINT size)
    {
        return (size + 255) & ~255u;
    }
    constexpr UINT kObjectCBSize = Align256(sizeof(ObjectConstants));

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

    // Phase 4: depth buffer and its descriptor heap.
    ComPtr<ID3D12Resource>       g_depthStencilBuffer;
    ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

    // Phase 4: constant buffer. Kept permanently mapped - Map/Unmap every
    // frame would be pure overhead for an upload-heap resource.
    ComPtr<ID3D12Resource>       g_constantBuffer;
    ComPtr<ID3D12DescriptorHeap> g_cbvHeap;
    uint8_t*                     g_constantBufferMapped = nullptr;

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

    // Assets live in the source tree, but where the exe ends up depends on
    // how it was built. Walk UP from the exe until we find the solution
    // file - that directory is the repo root.
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

    std::filesystem::path GetShaderDir()
    {
        return GetProjectRoot() / L"Engine" / L"Shaders";
    }

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

    // Creates an upload-heap buffer and copies initData into it. Upload heap
    // = CPU-writable, GPU-readable. Fine for small/static data; Phase 5
    // introduces the upload -> default heap copy for real GPU-local memory.
    ComPtr<ID3D12Resource> CreateUploadBuffer(const void* initData, UINT byteSize,
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
            D3D12_RANGE readRange = {}; // we never read back
            ThrowIfFailed(buffer->Map(0, &readRange, &mapped), "Map");
            memcpy(mapped, initData, byteSize);
            buffer->Unmap(0, nullptr);
        }
        return buffer;
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

    // [3] Device - first hardware adapter that speaks D3D12.
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

    // [7] Depth buffer. Unlike the back buffers (owned by the swap chain),
    //     WE create this one: a DEFAULT-heap texture the GPU reads/writes
    //     every pixel, plus a DSV describing it.
    void CreateDepthStencilBuffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        heapDesc.NumDescriptors = 1;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_dsvHeap)),
                      "CreateDescriptorHeap(DSV)");

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU-local memory

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = kClientWidth;
        desc.Height           = kClientHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = kDepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        // Telling the driver the expected clear value up front lets it pick
        // a faster clear path. It must match what ClearDepthStencilView uses.
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format               = kDepthFormat;
        clearValue.DepthStencil.Depth   = 1.0f; // 1.0 = farthest
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

    // [9] Constant buffer + CBV. This heap is SHADER_VISIBLE (unlike the RTV
    //     and DSV heaps): the GPU reads descriptors from it while drawing,
    //     so the list must bind it with SetDescriptorHeaps.
    void CreateConstantBuffer()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 1;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(g_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(&g_cbvHeap)),
                      "CreateDescriptorHeap(CBV)");

        g_constantBuffer = CreateUploadBuffer(nullptr, kObjectCBSize,
                                              "CreateCommittedResource(CB)");

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes    = kObjectCBSize; // must be 256-byte aligned
        g_device->CreateConstantBufferView(
            &cbvDesc, g_cbvHeap->GetCPUDescriptorHandleForHeapStart());

        // Map once and keep the pointer for the lifetime of the app.
        D3D12_RANGE readRange = {};
        ThrowIfFailed(g_constantBuffer->Map(
                          0, &readRange, reinterpret_cast<void**>(&g_constantBufferMapped)),
                      "CB Map");
    }

    // [10] Root signature - no longer empty: slot 0 is a descriptor table
    //      pointing at one CBV (register b0 in the shader).
    void CreateRootSignature()
    {
        D3D12_DESCRIPTOR_RANGE cbvRange = {};
        cbvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        cbvRange.NumDescriptors     = 1;
        cbvRange.BaseShaderRegister = 0; // b0

        D3D12_ROOT_PARAMETER rootParam = {};
        rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParam.DescriptorTable.NumDescriptorRanges = 1;
        rootParam.DescriptorTable.pDescriptorRanges   = &cbvRange;
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VS only

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = 1;
        desc.pParameters   = &rootParam;
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

    // [11] PSO - now with depth testing enabled and a depth format declared.
    void CreatePipelineState()
    {
        const std::filesystem::path shaderFile = GetShaderDir() / L"Basic.hlsl";
        ComPtr<ID3DBlob> vs = CompileShader(shaderFile, "VSMain", "vs_5_0");
        ComPtr<ID3DBlob> ps = CompileShader(shaderFile, "PSMain", "ps_5_0");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,
              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
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

        // Phase 4: depth test on. LESS = keep the fragment closer to the
        // camera; without this the cube's back faces paint over the front.
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

    // [12] Cube geometry: 8 vertices reused by 36 indices (12 triangles).
    //      Without indices we would need 36 vertices - that is the point of
    //      an index buffer: share vertices between triangles.
    void CreateCubeBuffers()
    {
        const Vertex vertices[] = {
            { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } }, // 0 black
            { { -1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // 1 red
            { { +1.0f, +1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // 2 green
            { { +1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }, // 3 blue
            { { -1.0f, -1.0f, +1.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } }, // 4 yellow
            { { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f, 1.0f, 1.0f } }, // 5 cyan
            { { +1.0f, +1.0f, +1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f } }, // 6 magenta
            { { +1.0f, -1.0f, +1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }, // 7 white
        };

        // Every triangle is wound clockwise when seen from OUTSIDE, matching
        // the rasterizer's "clockwise = front face" setting.
        const uint16_t indices[] = {
            0, 1, 2,  0, 2, 3,   // front
            4, 6, 5,  4, 7, 6,   // back
            4, 5, 1,  4, 1, 0,   // left
            3, 2, 6,  3, 6, 7,   // right
            1, 5, 6,  1, 6, 2,   // top
            4, 0, 3,  4, 3, 7,   // bottom
        };

        g_vertexBuffer = CreateUploadBuffer(vertices, sizeof(vertices),
                                            "CreateCommittedResource(VB)");
        g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
        g_vertexBufferView.SizeInBytes    = sizeof(vertices);
        g_vertexBufferView.StrideInBytes  = sizeof(Vertex);

        g_indexBuffer = CreateUploadBuffer(indices, sizeof(indices),
                                           "CreateCommittedResource(IB)");
        g_indexBufferView.BufferLocation = g_indexBuffer->GetGPUVirtualAddress();
        g_indexBufferView.SizeInBytes    = sizeof(indices);
        g_indexBufferView.Format         = DXGI_FORMAT_R16_UINT; // uint16_t
        g_indexCount = _countof(indices);
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

    // ---------------------------------------------------------------- per frame

    // Build World * View * Projection and push it to the constant buffer.
    //
    // Writing straight into the mapped upload buffer is only safe because
    // Render() flushes the GPU every frame. With frame pipelining (Phase 9)
    // this would overwrite data the GPU is still reading - which is why
    // frame resources keep one constant buffer PER frame in flight.
    void UpdateConstantBuffer(float totalSeconds)
    {
        // Spin around Y, tilt around X, so all faces come into view.
        XMMATRIX world = XMMatrixRotationY(totalSeconds) *
                         XMMatrixRotationX(totalSeconds * 0.5f);

        // Camera at (0, 2, -6) looking at the origin, Y up.
        XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 2.0f, -6.0f, 1.0f),
                                         XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
                                         XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

        // 45-degree vertical FOV. The aspect ratio is what fixes the
        // "stretched" look Phase 3 had.
        XMMATRIX proj = XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            float(kClientWidth) / float(kClientHeight),
            0.1f, 100.0f);

        // DirectXMath is row-major, HLSL reads matrices column-major by
        // default -> transpose on the way to the GPU.
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

        // Bind BOTH the color target and the depth target now.
        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        g_commandList->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
        // Depth must be cleared to 1.0 (far) every frame, or last frame's
        // depths would reject this frame's pixels.
        g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                             1.0f, 0, 0, nullptr);

        g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
        g_commandList->SetPipelineState(g_pipelineState.Get());

        // Shader-visible heaps must be bound before the table that uses them.
        ID3D12DescriptorHeap* heaps[] = { g_cbvHeap.Get() };
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        g_commandList->SetGraphicsRootDescriptorTable(
            0, g_cbvHeap->GetGPUDescriptorHandleForHeapStart());

        g_commandList->RSSetViewports(1, &kViewport);
        g_commandList->RSSetScissorRects(1, &kScissorRect);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
        g_commandList->IASetIndexBuffer(&g_indexBufferView);
        // Indexed draw: walk the index buffer, not the vertex buffer.
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
        CreateRootSignature();                           // [10]
        CreatePipelineState();                           // [11]
        CreateCubeBuffers();                             // [12]
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

    return static_cast<int>(msg.wParam);
}
