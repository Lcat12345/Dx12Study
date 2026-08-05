// DescriptorAllocator.h : hands out slots in a descriptor heap.
#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

// A slot, named by INDEX rather than by address.
//
// The address was the obvious identifier right up until the heap had to grow.
// A D3D12 descriptor heap cannot be resized, so growing means a different
// heap at a different address, and every handle stored anywhere would be
// pointing into freed memory. An index survives that; the allocator resolves
// it against whichever heap is current.
struct DescriptorHandle
{
    static constexpr UINT kInvalidIndex = UINT(-1);

    UINT index = kInvalidIndex;

    bool IsValid() const { return index != kInvalidIndex; }
};

// One logical heap, one type, no fixed ceiling.
//
// Storage is split in two, because the two jobs a descriptor heap does have
// incompatible requirements:
//
//   staging - CPU-only PAGES that hold the authoritative descriptors. Views
//             are written here. Growing appends a page, so a CPU handle
//             handed out years ago stays valid forever, and nothing the GPU
//             is reading is disturbed.
//
//   shader-visible - ONE heap the GPU reads, kept as a published copy of the
//             staging pages. It has to be a single heap because
//             SetDescriptorHeaps binds exactly one per type, so growing it
//             means replacing it, which is only safe when the GPU is idle.
//
// The split is forced by D3D12, not chosen: a shader-visible heap is CPU
// write-only, so it cannot be the SOURCE of a descriptor copy. Without a
// CPU-readable original there would be nothing to rebuild a bigger heap from.
class DescriptorAllocator
{
public:
    // `capacity` is the initial size and also the page size growth works in.
    DescriptorAllocator(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                        UINT capacity, bool shaderVisible);

    DescriptorAllocator(const DescriptorAllocator&)            = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

    // Never fails for want of room - a page is added instead. Callable at any
    // time, including while a command list is recording, because appending a
    // CPU-only page touches nothing the GPU can see. (ImGui's backend
    // allocates from inside RenderDrawData, so this is a requirement.)
    DescriptorHandle Allocate();

    // Returns a slot for reuse. Safe to call with an invalid handle.
    void Free(const DescriptorHandle& handle);

    // Recovers the slot a CPU handle belongs to. Needed because third-party
    // code (ImGui's descriptor callbacks) hands back raw handles rather than
    // the DescriptorHandle we gave it. Works across growth precisely because
    // staging pages are never moved or replaced.
    void FreeByCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu);

    // Where a view is WRITTEN. Always a staging address.
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(const DescriptorHandle& handle) const;
    // What a root descriptor table is pointed at. Only meaningful on a
    // shader-visible heap, and only after the slot has been published.
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(const DescriptorHandle& handle) const;

    // Say that a slot's descriptor was rewritten in place, so the next
    // publish copies it forward. Allocate() does this for you; this is for
    // the rewrite case - a render target resized into the slots it already
    // owns.
    //
    // Only safe at a point where the GPU is not reading that slot. Both
    // callers today rewrite during a resize, which already drains the GPU.
    void MarkWritten(const DescriptorHandle& handle);

    // Copies everything written since the last call into the shader-visible
    // heap. Must run before the command list that reads those slots is
    // EXECUTED - recording is fine, because a descriptor is only fetched when
    // the GPU runs the list.
    void PublishPendingWrites();

    // Whether the shader-visible heap no longer covers the staged slots, or
    // has too little headroom left for the allocations a frame may make while
    // recording. Checked at a frame boundary; acting on it is GrowShaderVisibleHeap.
    bool NeedsShaderVisibleGrowth() const;

    // Replaces the shader-visible heap with a bigger one and republishes
    // every live slot into it. THE CALLER MUST HAVE DRAINED THE GPU: the old
    // heap is released here, and any command list still referencing it would
    // be reading freed memory.
    void GrowShaderVisibleHeap();

    // Only shader-visible heaps get bound with SetDescriptorHeaps. The
    // returned pointer changes when the heap grows - see Generation().
    ID3D12DescriptorHeap* Heap() const { return m_shaderVisibleHeap.Get(); }

    // Bumped every time Heap() becomes a different object. Anything that
    // CACHED the heap pointer or a GPU handle - ImGui's DX12 backend does
    // both - has to notice this and rebuild.
    uint64_t Generation() const { return m_generation; }

    UINT Capacity()  const { return UINT(m_pages.size()) * m_pageSize; }
    UINT UsedCount() const { return m_bump - UINT(m_freeList.size()); }

private:
    void AddPage();
    D3D12_CPU_DESCRIPTOR_HANDLE StagingCpu(UINT index) const;
    void CopyToShaderVisible(UINT index);

    ID3D12Device*              m_device = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type   = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    // The authoritative descriptors. Index i lives in page i / m_pageSize.
    std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> m_pages;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>                  m_pageStarts;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderVisibleHeap;
    // The previous generation, kept alive one growth longer so a host layer
    // that cached the pointer does not bind freed memory before it notices.
    std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> m_retiredHeaps;
    D3D12_CPU_DESCRIPTOR_HANDLE m_shaderVisibleCpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_shaderVisibleGpuStart = {};
    UINT                        m_shaderVisibleCapacity = 0;

    uint64_t m_generation = 0;

    UINT m_descriptorSize = 0;
    UINT m_pageSize       = 0;
    UINT m_bump           = 0; // next never-used slot
    bool m_shaderVisible  = false;

    std::vector<UINT> m_freeList;    // slots handed back, reused first
    std::vector<UINT> m_pendingCopy; // slots written but not yet published
};
