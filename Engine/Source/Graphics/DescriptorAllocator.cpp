#include "Graphics/DescriptorAllocator.h"

#include "Core/Common.h"

#include <stdexcept>

DescriptorAllocator::DescriptorAllocator(ID3D12Device* device,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         UINT capacity, bool shaderVisible)
    : m_capacity(capacity)
    , m_shaderVisible(shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = type;
    desc.NumDescriptors = capacity;
    desc.Flags          = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap)),
                  "CreateDescriptorHeap");

    // Descriptor sizes are GPU-specific - always ask, never hardcode.
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    m_cpuStart       = m_heap->GetCPUDescriptorHandleForHeapStart();

    // Only legal on shader-visible heaps.
    if (shaderVisible)
    {
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    }
}

DescriptorHandle DescriptorAllocator::MakeHandle(UINT index) const
{
    DescriptorHandle handle;
    handle.index   = index;
    handle.cpu.ptr = m_cpuStart.ptr + SIZE_T(index) * m_descriptorSize;
    if (m_shaderVisible)
    {
        handle.gpu.ptr = m_gpuStart.ptr + UINT64(index) * m_descriptorSize;
    }
    return handle;
}

DescriptorHandle DescriptorAllocator::Allocate()
{
    if (!m_freeList.empty())
    {
        const UINT index = m_freeList.back();
        m_freeList.pop_back();
        return MakeHandle(index);
    }

    if (m_bump >= m_capacity)
    {
        throw std::runtime_error(
            "descriptor heap exhausted: used=" + std::to_string(UsedCount()) +
            " capacity=" + std::to_string(m_capacity));
    }
    return MakeHandle(m_bump++);
}

void DescriptorAllocator::Free(const DescriptorHandle& handle)
{
    if (handle.IsValid() && handle.index < m_capacity)
    {
        m_freeList.push_back(handle.index);
    }
}

void DescriptorAllocator::FreeByCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
{
    if (cpu.ptr < m_cpuStart.ptr || m_descriptorSize == 0)
    {
        return;
    }
    const SIZE_T offset = cpu.ptr - m_cpuStart.ptr;
    if (offset % m_descriptorSize != 0)
    {
        return; // not a handle this heap ever produced
    }
    DescriptorHandle handle;
    handle.index = UINT(offset / m_descriptorSize);
    Free(handle);
}
