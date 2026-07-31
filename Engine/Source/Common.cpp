#include "Common.h"

#include <stdexcept>
#include <cstdio>

using Microsoft::WRL::ComPtr;

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
        if (dir.parent_path() == dir) // reached the drive root
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

std::filesystem::path GetAssetDir()
{
    return GetProjectRoot() / L"Engine" / L"Assets";
}

ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, const void* initData,
                                          UINT64 byteSize, const char* debugWhat)
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    // Buffers are 1D: Width is the byte size, everything else is fixed.
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
    ThrowIfFailed(device->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, // upload heap's fixed state
                      nullptr, IID_PPV_ARGS(&buffer)),
                  debugWhat);

    if (initData)
    {
        void* mapped = nullptr;
        D3D12_RANGE readRange = {}; // empty = we will not read back
        ThrowIfFailed(buffer->Map(0, &readRange, &mapped), "Map");
        memcpy(mapped, initData, static_cast<size_t>(byteSize));
        buffer->Unmap(0, nullptr);
    }
    return buffer;
}
