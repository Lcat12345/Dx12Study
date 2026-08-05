#include "Graphics/DescriptorAllocator.h"

#include "Core/Common.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    // Slots kept spare in the shader-visible heap beyond what is staged.
    //
    // The heap may only be replaced at a frame boundary, but descriptors are
    // allocated at any time - ImGui's backend allocates from inside
    // RenderDrawData, and loading a scene creates a texture per material and
    // then draws it in the same frame. Anything allocated after the boundary
    // has to fit in the heap the frame is already using, and the reserve is
    // that room. Generous on purpose: a descriptor is 32 bytes, so 256 spare
    // slots cost 8 KB, and running out is a hard error rather than a slow
    // frame.
    constexpr UINT kShaderVisibleReserve = 256;

    // The most descriptors a shader-visible CBV/SRV/UAV heap may hold. Both
    // resource binding tiers cap at the same number, so there is nothing to
    // query - the constant IS the limit.
    constexpr UINT kMaxShaderVisibleDescriptors =
        D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;

    UINT RoundUpToPowerOfTwo(UINT value)
    {
        UINT result = 1;
        while (result < value)
        {
            if (result > (UINT(-1) / 2u))
            {
                throw std::length_error("descriptor heap capacity overflow");
            }
            result *= 2u;
        }
        return result;
    }
}

DescriptorAllocator::DescriptorAllocator(ID3D12Device* device,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         UINT capacity, bool shaderVisible)
    : m_device(device)
    , m_type(type)
    , m_pageSize((std::max)(capacity, 1u))
    , m_shaderVisible(shaderVisible)
{
    // Descriptor sizes are GPU-specific - always ask, never hardcode.
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    AddPage();
    if (m_shaderVisible)
    {
        // Sized to the first page plus the recording-time reserve, so the
        // very first frame already has the headroom the reserve exists for.
        GrowShaderVisibleHeap();
    }
}

void DescriptorAllocator::AddPage()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = m_type;
    desc.NumDescriptors = m_pageSize;
    // Always CPU-only, whatever this allocator is for. A shader-visible heap
    // cannot be read back, and these pages exist precisely to be readable.
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> page;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&page)),
                  "CreateDescriptorHeap(staging page)");
    m_pageStarts.push_back(page->GetCPUDescriptorHandleForHeapStart());
    m_pages.push_back(std::move(page));
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::StagingCpu(UINT index) const
{
    const size_t page   = index / m_pageSize;
    const UINT   offset = index % m_pageSize;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pageStarts[page];
    handle.ptr += SIZE_T(offset) * m_descriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE
DescriptorAllocator::CpuHandle(const DescriptorHandle& handle) const
{
    if (!handle.IsValid() || handle.index >= Capacity())
    {
        return {};
    }
    return StagingCpu(handle.index);
}

D3D12_GPU_DESCRIPTOR_HANDLE
DescriptorAllocator::GpuHandle(const DescriptorHandle& handle) const
{
    if (!m_shaderVisible || !handle.IsValid())
    {
        return {};
    }
    if (handle.index >= m_shaderVisibleCapacity)
    {
        // The reserve is meant to make this unreachable. Reaching it would
        // mean binding a descriptor the GPU heap has no slot for, which reads
        // as whatever happens to be there - so say so instead.
        throw std::logic_error(
            "descriptor slot " + std::to_string(handle.index) +
            " is outside the shader-visible heap (capacity " +
            std::to_string(m_shaderVisibleCapacity) + ")");
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_shaderVisibleGpuStart;
    gpu.ptr += UINT64(handle.index) * m_descriptorSize;
    return gpu;
}

DescriptorHandle DescriptorAllocator::Allocate()
{
    DescriptorHandle handle;
    if (!m_freeList.empty())
    {
        handle.index = m_freeList.back();
        m_freeList.pop_back();
    }
    else
    {
        if (m_bump >= Capacity())
        {
            AddPage();
        }
        handle.index = m_bump++;
    }

    MarkWritten(handle);
    return handle;
}

void DescriptorAllocator::Free(const DescriptorHandle& handle)
{
    if (handle.IsValid() && handle.index < Capacity())
    {
        m_freeList.push_back(handle.index);
    }
}

void DescriptorAllocator::FreeByCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
    if (m_descriptorSize == 0)
    {
        return;
    }

    // Which page the address falls in. Linear over a handful of pages, and
    // the alternative - arithmetic on one base - is exactly what stopped
    // working once there was more than one heap.
    for (size_t page = 0; page < m_pageStarts.size(); ++page)
    {
        const SIZE_T start = m_pageStarts[page].ptr;
        const SIZE_T end   = start + SIZE_T(m_pageSize) * m_descriptorSize;
        if (cpu.ptr < start || cpu.ptr >= end)
        {
            continue;
        }
        const SIZE_T offset = cpu.ptr - start;
        if (offset % m_descriptorSize != 0)
        {
            return; // not a handle this heap ever produced
        }
        DescriptorHandle handle;
        handle.index = UINT(page) * m_pageSize + UINT(offset / m_descriptorSize);
        Free(handle);
        return;
    }
}

void DescriptorAllocator::MarkWritten(const DescriptorHandle& handle)
{
    if (!m_shaderVisible || !handle.IsValid())
    {
        return; // CPU-only heaps are read straight from the pages
    }
    m_pendingCopy.push_back(handle.index);
}

void DescriptorAllocator::CopyToShaderVisible(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE destination = m_shaderVisibleCpuStart;
    destination.ptr += SIZE_T(index) * m_descriptorSize;
    m_device->CopyDescriptorsSimple(1, destination, StagingCpu(index), m_type);
}

void DescriptorAllocator::PublishPendingWrites()
{
    if (!m_shaderVisible || m_pendingCopy.empty())
    {
        return;
    }

    for (const UINT index : m_pendingCopy)
    {
        // A slot can be staged and then outrun by growth in the same frame.
        // GrowShaderVisibleHeap republishes everything, so skipping here
        // loses nothing - and copying past the heap's end would be a real
        // out-of-bounds write.
        if (index < m_shaderVisibleCapacity)
        {
            CopyToShaderVisible(index);
        }
    }
    m_pendingCopy.clear();
}

bool DescriptorAllocator::NeedsShaderVisibleGrowth() const
{
    if (!m_shaderVisible)
    {
        return false;
    }
    // Two ways to fall behind: slots exist that the heap cannot address at
    // all, or the headroom a recording frame may consume has been eaten.
    return m_bump + kShaderVisibleReserve > m_shaderVisibleCapacity;
}

void DescriptorAllocator::GrowShaderVisibleHeap()
{
    if (!m_shaderVisible)
    {
        return;
    }

    const UINT required = (std::max)(m_bump + kShaderVisibleReserve, m_pageSize);
    if (required > kMaxShaderVisibleDescriptors)
    {
        throw std::length_error(
            "shader-visible descriptor heap needs " + std::to_string(required) +
            " descriptors, past the D3D12 hardware limit of " +
            std::to_string(kMaxShaderVisibleDescriptors));
    }
    // Powers of two keep growth to O(log n) replacements, but the last step
    // before the limit would ask for 1,048,576 - more than any tier allows.
    // Clamping there means the heap reaches the real hardware ceiling instead
    // of failing at roughly half of it.
    const UINT capacity =
        (std::min)(RoundUpToPowerOfTwo(required), kMaxShaderVisibleDescriptors);

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = m_type;
    desc.NumDescriptors = capacity;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    // Built before the old one is let go, so a failed allocation leaves a
    // working heap rather than none at all.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)),
                  "CreateDescriptorHeap(shader visible)");

    // Anything retired by an EARLIER growth is safe to drop now: the caller
    // has drained the GPU, so no list from back then can still be running.
    m_retiredHeaps.clear();
    // The heap being replaced is NOT dropped. A host layer that caches the
    // pointer - ImGui's DX12 backend does - may still bind it for the rest of
    // this frame, and it only learns to rebuild once the generation changes.
    // One frame of grace costs a few kilobytes.
    if (m_shaderVisibleHeap)
    {
        m_retiredHeaps.push_back(std::move(m_shaderVisibleHeap));
    }

    m_shaderVisibleHeap     = std::move(heap);
    m_shaderVisibleCpuStart = m_shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
    m_shaderVisibleGpuStart = m_shaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
    m_shaderVisibleCapacity = capacity;
    ++m_generation;

    // Every LIVE slot, not just the pending ones: the new heap starts empty,
    // so anything allocated before this moment has to be copied forward. The
    // staging pages are CPU-only, which is what makes them a legal source.
    //
    // Returned slots are skipped. Their descriptor still describes whatever
    // resource has since been released, and nothing binds it - carrying that
    // forward would only put a view of a dead resource in the live heap.
    std::vector<bool> returned(m_bump, false);
    for (const UINT index : m_freeList)
    {
        if (index < m_bump)
        {
            returned[index] = true;
        }
    }

    m_pendingCopy.clear();
    for (UINT index = 0; index < m_bump; ++index)
    {
        if (!returned[index])
        {
            CopyToShaderVisible(index);
        }
    }
}
