#include "Graphics/GraphicsDevice.h"

#include "Core/Common.h"

#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

GraphicsDevice::GraphicsDevice(AdapterPolicy adapterPolicy)
    : m_adapterPolicy(adapterPolicy)
{
    // Order matters and is the reason this is a constructor rather than
    // four loose calls: the debug layer is only honoured if it is turned on
    // BEFORE the device exists.
    EnableDebugLayer();
    CreateFactory();
    CreateDevice();
    CreateQueueAndFence();
}

GraphicsDevice::~GraphicsDevice()
{
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void GraphicsDevice::EnableDebugLayer()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
#endif
}

void GraphicsDevice::CreateFactory()
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    ThrowIfFailed(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)),
                  "CreateDXGIFactory2");

    // Tearing must be QUERIED, not assumed - older drivers, remote desktop
    // sessions and some virtualized GPUs report no support.
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                 &allowTearing, sizeof(allowTearing))))
    {
        m_tearingSupported = (allowTearing == TRUE);
    }
}

void GraphicsDevice::CreateDevice()
{
    if (m_adapterPolicy == AdapterPolicy::HardwareOnly)
    {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             m_factory->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue; // WARP etc. - production wants real hardware
            }
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&m_device))))
            {
#if defined(_DEBUG)
                // Optional: fails when the "Graphics Tools" Windows feature is
                // not installed, which is normal on a fresh machine. The count
                // then stays 0 - reported honestly by DebugMessageCount as "no
                // info queue" rather than as "no problems".
                m_device.As(&m_infoQueue);
#endif
                return;
            }
        }
        throw std::runtime_error("No D3D12-capable hardware GPU found");
    }

    // Deterministic test/headless mode: always use WARP, even on a machine
    // with a GPU. Production keeps HardwareOnly, so it can never silently
    // fall back to a software renderer.
    ComPtr<IDXGIAdapter> warpAdapter;
    if (SUCCEEDED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) &&
        SUCCEEDED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&m_device))))
    {
#if defined(_DEBUG)
        m_device.As(&m_infoQueue);
#endif
        return;
    }
    throw std::runtime_error("No D3D12-capable WARP adapter found");
}

UINT64 GraphicsDevice::DebugMessageCount() const
{
    return m_infoQueue ? m_infoQueue->GetNumStoredMessages() : 0;
}

void GraphicsDevice::CreateQueueAndFence()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // graphics-capable
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)),
                  "CreateCommandQueue");

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
                  "CreateFence");

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
    {
        throw std::runtime_error("CreateEvent failed");
    }
}

UINT64 GraphicsDevice::Signal()
{
    const UINT64 value = ++m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), value), "Queue Signal");
    return value;
}

void GraphicsDevice::WaitForFenceValue(UINT64 value)
{
    if (value == 0 || m_fence->GetCompletedValue() >= value)
    {
        return; // already done - no kernel round trip
    }
    ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent),
                  "SetEventOnCompletion");
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void GraphicsDevice::WaitForGpu()
{
    WaitForFenceValue(Signal());
}
