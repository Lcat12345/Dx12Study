// Mesh.h : vertex format and GPU geometry buffers.
#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// The input layout in Renderer.cpp must match this struct exactly.
struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

struct Mesh
{
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW               vbv = {};
    D3D12_INDEX_BUFFER_VIEW                ibv = {};
    UINT                                   indexCount = 0;
};

// Geometry still on the CPU: what a loader produces before upload.
struct MeshData
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

// Indices are 32-bit. 16-bit halves the index memory but caps a mesh at
// 65535 vertices, which loaded models pass easily.
Mesh CreateMesh(ID3D12Device* device,
                const Vertex* vertices, UINT vertexCount,
                const uint32_t* indices, UINT indexCount);

Mesh CreateMesh(ID3D12Device* device, const MeshData& data);

// --- procedural geometry ---
// These return CPU-side data rather than a GPU Mesh, so the ResourceManager
// is the single place that uploads and caches. Same shape as what the OBJ
// loader returns, which is what lets both go through one code path.

// 24 vertices, not 8: a corner is shared by three faces but each face needs
// its own uv and its own flat normal there, and a vertex carries only one
// of each. (Phase 8's OBJ loader hits the same problem from the other side.)
MeshData MakeCubeMeshData();

// Slanted faces - the shape that makes the inverse-transpose normal matrix
// visible, which an axis-aligned cube never can.
MeshData MakePyramidMeshData();

// One big quad. uvTiling > 1 repeats the texture (sampler is set to WRAP).
MeshData MakeFloorMeshData(float halfExtent, float uvTiling);
