// Common.h : small helpers shared by every other file.
#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

// Throws std::runtime_error carrying the HRESULT when hr indicates failure.
// 'what' names the call site so the message box is actually useful.
void ThrowIfFailed(HRESULT hr, const char* what);

// Rounds up to the next multiple of alignment (constant buffers need 256).
constexpr UINT Align(UINT size, UINT alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

// "This resource changes from state A to state B" - the barrier the GPU
// needs before a resource is used a different way.
D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after);

// Upload-heap buffer: CPU-writable, GPU-readable. Pass initData to fill it
// on creation, or nullptr to leave it empty (for buffers written per frame).
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(
    ID3D12Device* device, const void* initData, UINT64 byteSize,
    const char* debugWhat);
