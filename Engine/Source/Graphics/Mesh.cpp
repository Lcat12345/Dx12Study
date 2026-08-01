#include "Graphics/Mesh.h"
#include "Core/Common.h"

#include <vector>

using namespace DirectX;

namespace
{
    // Face normals, reused by the cube and the floor.
    constexpr XMFLOAT3 kFront = {  0,  0, -1 };
    constexpr XMFLOAT3 kBack  = {  0,  0, +1 };
    constexpr XMFLOAT3 kLeft  = { -1,  0,  0 };
    constexpr XMFLOAT3 kRight = { +1,  0,  0 };
    constexpr XMFLOAT3 kUp    = {  0, +1,  0 };
    constexpr XMFLOAT3 kDown  = {  0, -1,  0 };
}

Mesh CreateMesh(ID3D12Device* device,
                const Vertex* vertices, UINT vertexCount,
                const uint32_t* indices, UINT indexCount)
{
    Mesh mesh;
    const UINT vertexBytes = vertexCount * sizeof(Vertex);
    const UINT indexBytes  = indexCount * sizeof(uint32_t);

    mesh.vertexBuffer = CreateUploadBuffer(device, vertices, vertexBytes, "Mesh VB");
    mesh.vbv.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes    = vertexBytes;
    mesh.vbv.StrideInBytes  = sizeof(Vertex);

    mesh.indexBuffer = CreateUploadBuffer(device, indices, indexBytes, "Mesh IB");
    mesh.ibv.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.ibv.SizeInBytes    = indexBytes;
    mesh.ibv.Format         = DXGI_FORMAT_R32_UINT;

    mesh.indexCount  = indexCount;
    mesh.vertexCount = vertexCount;
    return mesh;
}

Mesh CreateMesh(ID3D12Device* device, const MeshData& data)
{
    return CreateMesh(device,
                      data.vertices.data(), UINT(data.vertices.size()),
                      data.indices.data(), UINT(data.indices.size()));
}

MeshData MakeCubeMeshData()
{
    // Each face lists its corners as top-left, top-right, bottom-right,
    // bottom-left AS SEEN FROM OUTSIDE, which makes the winding clockwise -
    // matching the rasterizer's "clockwise = front face".
    const Vertex vertices[] = {
        { { -1, +1, -1 }, kFront, { 0, 0 } }, { { +1, +1, -1 }, kFront, { 1, 0 } },
        { { +1, -1, -1 }, kFront, { 1, 1 } }, { { -1, -1, -1 }, kFront, { 0, 1 } },

        { { +1, +1, +1 }, kBack,  { 0, 0 } }, { { -1, +1, +1 }, kBack,  { 1, 0 } },
        { { -1, -1, +1 }, kBack,  { 1, 1 } }, { { +1, -1, +1 }, kBack,  { 0, 1 } },

        { { -1, +1, +1 }, kLeft,  { 0, 0 } }, { { -1, +1, -1 }, kLeft,  { 1, 0 } },
        { { -1, -1, -1 }, kLeft,  { 1, 1 } }, { { -1, -1, +1 }, kLeft,  { 0, 1 } },

        { { +1, +1, -1 }, kRight, { 0, 0 } }, { { +1, +1, +1 }, kRight, { 1, 0 } },
        { { +1, -1, +1 }, kRight, { 1, 1 } }, { { +1, -1, -1 }, kRight, { 0, 1 } },

        { { -1, +1, +1 }, kUp,    { 0, 0 } }, { { +1, +1, +1 }, kUp,    { 1, 0 } },
        { { +1, +1, -1 }, kUp,    { 1, 1 } }, { { -1, +1, -1 }, kUp,    { 0, 1 } },

        { { -1, -1, -1 }, kDown,  { 0, 0 } }, { { +1, -1, -1 }, kDown,  { 1, 0 } },
        { { +1, -1, +1 }, kDown,  { 1, 1 } }, { { -1, -1, +1 }, kDown,  { 0, 1 } },
    };

    // Every face is the same two triangles over its own 4 vertices.
    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const uint32_t base = face * 4;
        indices.insert(indices.end(),
                       { uint32_t(base + 0), uint32_t(base + 1), uint32_t(base + 2),
                         uint32_t(base + 0), uint32_t(base + 2), uint32_t(base + 3) });
    }

    return MeshData{ std::vector<Vertex>(std::begin(vertices), std::end(vertices)),
                     indices };
}

MeshData MakePyramidMeshData()
{
    // Side faces slope at atan(1/2) from vertical, so their normals are
    // diagonal: normalize(0, 1, -2) = (0, 0.447, -0.894) for the front.
    constexpr float kSlope = 0.4472f; // the y component
    constexpr float kSide  = 0.8944f; // the sideways component

    constexpr XMFLOAT3 nFront = {  0,      kSlope, -kSide };
    constexpr XMFLOAT3 nRight = {  kSide,  kSlope,  0     };
    constexpr XMFLOAT3 nBack  = {  0,      kSlope,  kSide };
    constexpr XMFLOAT3 nLeft  = { -kSide,  kSlope,  0     };

    const Vertex vertices[] = {
        // each side face: apex, then its two base corners, clockwise seen
        // from outside, carrying its own flat normal
        { {  0, +1,  0 }, nFront, { 0.5f, 0 } },
        { { +1, -1, -1 }, nFront, { 1,    1 } },
        { { -1, -1, -1 }, nFront, { 0,    1 } },

        { {  0, +1,  0 }, nRight, { 0.5f, 0 } },
        { { +1, -1, +1 }, nRight, { 1,    1 } },
        { { +1, -1, -1 }, nRight, { 0,    1 } },

        { {  0, +1,  0 }, nBack,  { 0.5f, 0 } },
        { { -1, -1, +1 }, nBack,  { 1,    1 } },
        { { +1, -1, +1 }, nBack,  { 0,    1 } },

        { {  0, +1,  0 }, nLeft,  { 0.5f, 0 } },
        { { -1, -1, -1 }, nLeft,  { 1,    1 } },
        { { -1, -1, +1 }, nLeft,  { 0,    1 } },

        // base, facing down
        { { -1, -1, -1 }, kDown,  { 0, 0 } }, { { +1, -1, -1 }, kDown, { 1, 0 } },
        { { +1, -1, +1 }, kDown,  { 1, 1 } }, { { -1, -1, +1 }, kDown, { 0, 1 } },
    };

    const uint32_t indices[] = {
        0, 1, 2,   3, 4, 5,   6, 7, 8,   9, 10, 11, // sides
        12, 13, 14,  12, 14, 15,                    // base
    };

    return MeshData{ std::vector<Vertex>(std::begin(vertices), std::end(vertices)),
                     std::vector<uint32_t>(std::begin(indices), std::end(indices)) };
}

MeshData MakeFloorMeshData(float halfExtent, float uvTiling)
{
    const Vertex vertices[] = {
        { { -halfExtent, 0, +halfExtent }, kUp, { 0,        0        } },
        { { +halfExtent, 0, +halfExtent }, kUp, { uvTiling, 0        } },
        { { +halfExtent, 0, -halfExtent }, kUp, { uvTiling, uvTiling } },
        { { -halfExtent, 0, -halfExtent }, kUp, { 0,        uvTiling } },
    };
    const uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

    return MeshData{ std::vector<Vertex>(std::begin(vertices), std::end(vertices)),
                     std::vector<uint32_t>(std::begin(indices), std::end(indices)) };
}
