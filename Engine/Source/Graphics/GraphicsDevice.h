// GraphicsDevice.h : the D3D12 device and the queue/fence that go with it.
#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>

// Owns the objects that exist exactly once for the lifetime of the app:
// the DXGI factory, the device, the graphics queue, and the fence used to
// tell when the GPU has caught up.
//
// A thin wrapper on purpose - Get-style accessors hand out the raw pointers.
// The point is to make creation order and destruction order explicit, not to
// hide D3D behind an abstraction.
class GraphicsDevice
{
public:
    enum class AdapterPolicy
    {
        HardwareOnly,
        SoftwareOnly
    };

    explicit GraphicsDevice(AdapterPolicy adapterPolicy = AdapterPolicy::HardwareOnly);
    ~GraphicsDevice();

    // Non-copyable, non-movable: exactly one owner, no lifetime puzzles.
    GraphicsDevice(const GraphicsDevice&)            = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    ID3D12Device*       Device()  const { return m_device.Get(); }
    ID3D12CommandQueue* Queue()   const { return m_commandQueue.Get(); }
    IDXGIFactory6*      Factory() const { return m_factory.Get(); }
    ID3D12Fence*        Fence()   const { return m_fence.Get(); }

    // Whether the display path can tear. Without it, SyncInterval 0 still
    // gets paced to vblank by the desktop compositor.
    bool IsTearingSupported() const { return m_tearingSupported; }
    const std::wstring& AdapterName() const { return m_adapterName; }

    // How many messages the debug layer has stored, all severities.
    //
    // Worth surfacing rather than leaving in the Output window: a D3D12
    // validation error does not throw, does not crash, and usually does not
    // even change the picture - it just quietly means the next driver, or the
    // next machine, may do something else. A number on screen turns "did I
    // break a barrier?" from an archaeology exercise into a glance.
    //
    // Always 0 in Release, where the debug layer is not enabled.
    UINT64 DebugMessageCount() const;
    // Whether there is an info queue at all. Without this, a count of 0 is
    // ambiguous - "nothing went wrong" and "nobody was watching" look the
    // same, and reporting the second as the first is how a clean bill of
    // health gets claimed on no evidence.
    bool   HasDebugLayer() const { return m_infoQueue != nullptr; }

    // Queues "set the fence to the next value when everything submitted so
    // far is done" and returns that value. Does not block.
    UINT64 Signal();

    // Blocks until the fence reaches value. Returns immediately if already
    // there, which is the common case with frame resources.
    void WaitForFenceValue(UINT64 value);

    // Signal + wait: nothing is in flight afterwards. Reserved for init
    // uploads, resizing, and shutdown - never the per-frame path.
    void WaitForGpu();

private:
    void EnableDebugLayer();
    void CreateFactory();
    void CreateDevice();
    void CreateQueueAndFence();

    // Declaration order IS destruction order, reversed. The factory is
    // declared first so it outlives everything created from it.
    Microsoft::WRL::ComPtr<IDXGIFactory6>      m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device>       m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence>        m_fence;
    // Null in Release, and in Debug on a machine without the graphics tools
    // installed - the debug layer is an optional Windows feature.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue>    m_infoQueue;

    UINT64 m_fenceValue       = 0;
    HANDLE m_fenceEvent       = nullptr;
    bool   m_tearingSupported = false;
    AdapterPolicy m_adapterPolicy = AdapterPolicy::HardwareOnly;
    std::wstring m_adapterName;
};
