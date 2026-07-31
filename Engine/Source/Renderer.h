// Renderer.h : everything D3D12. The rest of the program does not include
// a single D3D header.
#pragma once

#include <Windows.h>
#include <d3d12.h>

struct Scene;
struct Camera;

// Free functions over file-static state, not a class - Phase 9 is where the
// globals become members and frame resources arrive.
namespace Renderer
{
    // Device through PSO. Throws std::runtime_error on failure.
    void Initialize(HWND hwnd, UINT width, UINT height);

    // Waits for the GPU, then releases what needs explicit cleanup.
    void Shutdown();

    // Meshes are created against this device (see BuildScene).
    ID3D12Device* GetDevice();

    // Recreates the swap chain buffers and the depth buffer at the new size.
    void Resize(UINT width, UINT height);

    void Render(const Scene& scene, const Camera& camera, float totalSeconds);
}
