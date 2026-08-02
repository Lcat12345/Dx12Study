// Mesh.h : vertex format and GPU geometry buffers.
#pragma once

#include "Graphics/Handles.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

// The input layout in Renderer.cpp must match this struct exactly.
struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 uv;
};

// The mesh's extent in its OWN space, before any Transform.
//
// Local rather than world on purpose: a world box would have to be rebuilt
// every time something moves, and for a rotated mesh it is a loose box
// around a rotated box. Testing against the local one instead means moving
// the RAY into local space - one matrix inverse, and the box stays tight.
struct Aabb
{
    DirectX::XMFLOAT3 min = {  0.0f,  0.0f,  0.0f };
    DirectX::XMFLOAT3 max = {  0.0f,  0.0f,  0.0f };

    DirectX::XMFLOAT3 Center() const
    {
        return { (min.x + max.x) * 0.5f,
                 (min.y + max.y) * 0.5f,
                 (min.z + max.z) * 0.5f };
    }
    DirectX::XMFLOAT3 Extents() const
    {
        return { (max.x - min.x) * 0.5f,
                 (max.y - min.y) * 0.5f,
                 (max.z - min.z) * 0.5f };
    }
    // An empty mesh leaves min == max, which is a degenerate box rather than
    // a wrong one - every ray misses it.
    bool IsEmpty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }
};

// One material's worth of a mesh: a contiguous run of the index buffer.
//
// An .obj switches material with `usemtl` partway through its faces, and a
// real model does it several times - hair, cloth, skin. Drawing the whole
// index buffer with one texture paints all of them with whichever one
// happened to be assigned.
//
// The vertex and index buffers stay single and shared; only the draw call is
// split, by StartIndexLocation.
struct Submesh
{
    UINT          indexOffset = 0;
    UINT          indexCount  = 0;
    // From the .mtl. Invalid when the file named no texture, in which case
    // whatever the MeshRenderer carries is used instead.
    TextureHandle texture;
    std::wstring  materialName; // as written in the .obj, for the inspector
};

struct Mesh
{
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW               vbv = {};
    D3D12_INDEX_BUFFER_VIEW                ibv = {};
    UINT                                   indexCount = 0;
    // ALWAYS at least one, covering the whole index buffer, so nothing has
    // to special-case "single material".
    std::vector<Submesh>                   submeshes;
    // Not needed to draw - kept so the asset browser can report what a file
    // actually contained after the loader deduplicated its vertices.
    UINT                                   vertexCount = 0;
    // Computed at upload time, which is the last moment the CPU-side
    // vertices exist. Nothing else keeps them.
    Aabb                                   bounds;
};

// The extent of a vertex span. Returns an inverted (empty) box for no
// vertices, so IsEmpty() reports it rather than a box at the origin.
Aabb ComputeBounds(const Vertex* vertices, UINT vertexCount);

// A submesh as the LOADER sees it: index range plus the names it read. The
// loader cannot resolve a texture - that needs the ResourceManager - so it
// hands over the path it found and lets the manager turn it into a handle.
struct SubmeshData
{
    UINT         indexOffset = 0;
    UINT         indexCount  = 0;
    std::wstring materialName;
    // Relative to Assets/, ready for LoadTexture. Empty when the .mtl named
    // no map_Kd, or when the file it named could not be found.
    std::wstring diffuseTexture;
};

// Geometry still on the CPU: what a loader produces before upload.
struct MeshData
{
    std::vector<Vertex>      vertices;
    std::vector<uint32_t>    indices;
    // Empty means "one material" - CreateMesh then synthesises the single
    // submesh, so procedural geometry needs to say nothing about this.
    std::vector<SubmeshData> submeshes;
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
