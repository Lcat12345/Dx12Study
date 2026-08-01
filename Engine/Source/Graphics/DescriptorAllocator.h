// DescriptorAllocator.h : hands out slots in a descriptor heap.
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

// A descriptor's address is "heap start + index * increment", and the
// increment is GPU-specific. Computing that by hand at every call site is
// how "slot 0 is the texture, slot 1 is..." ends up hardcoded across the
// renderer. Allocate() hands out a slot and the two handles that go with it.
struct DescriptorHandle
{
    static constexpr UINT kInvalidIndex = UINT(-1);

    D3D12_CPU_DESCRIPTOR_HANDLE cpu   = {};
    // Stays zero on CPU-only heaps (RTV/DSV) - those have no GPU handle at
    // all, and asking for one is a debug-layer error.
    D3D12_GPU_DESCRIPTOR_HANDLE gpu   = {};
    UINT                        index = kInvalidIndex;

    bool IsValid() const { return index != kInvalidIndex; }
};

// One heap, one type. Deliberately not general: a bump pointer plus a free
// list of returned slots covers everything this engine does today. When a
// use case needs more (a per-frame ring for dynamic descriptors), add it
// then rather than guessing now.
class DescriptorAllocator
{
public:
    DescriptorAllocator(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                        UINT capacity, bool shaderVisible);

    DescriptorAllocator(const DescriptorAllocator&)            = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

    // Throws when the heap is full - silently overrunning a descriptor heap
    // corrupts unrelated views and is miserable to debug.
    DescriptorHandle Allocate();

    // Returns a slot for reuse. Safe to call with an invalid handle.
    void Free(const DescriptorHandle& handle);

    // Only shader-visible heaps get bound with SetDescriptorHeaps.
    ID3D12DescriptorHeap* Heap() const { return m_heap.Get(); }

    UINT Capacity()  const { return m_capacity; }
    UINT UsedCount() const { return m_bump - UINT(m_freeList.size()); }

private:
    DescriptorHandle MakeHandle(UINT index) const;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;

    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};

    UINT m_descriptorSize = 0;
    UINT m_capacity       = 0;
    UINT m_bump           = 0; // next never-used slot
    bool m_shaderVisible  = false;

    std::vector<UINT> m_freeList; // slots handed back, reused first
};
