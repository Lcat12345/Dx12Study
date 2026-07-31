// Mesh.h : vertex format and GPU geometry buffers.
#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cstdint>

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

Mesh CreateMesh(ID3D12Device* device,
                const Vertex* vertices, UINT vertexCount,
                const uint16_t* indices, UINT indexCount);

// 24 vertices, not 8: a corner is shared by three faces but each face needs
// its own uv and its own flat normal there, and a vertex carries only one
// of each. (Phase 8's OBJ loader hits the same problem from the other side.)
Mesh CreateCubeMesh(ID3D12Device* device);

// Slanted faces - the shape that makes the inverse-transpose normal matrix
// visible, which an axis-aligned cube never can.
Mesh CreatePyramidMesh(ID3D12Device* device);

// One big quad. uvTiling > 1 repeats the texture (sampler is set to WRAP).
Mesh CreateFloorMesh(ID3D12Device* device, float halfExtent, float uvTiling);
