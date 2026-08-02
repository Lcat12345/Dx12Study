#include "Graphics/Mesh.h"
#include "Core/Common.h"

#include <algorithm>
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

Aabb ComputeBounds(const Vertex* vertices, UINT vertexCount)
{
    Aabb bounds;
    if (vertexCount == 0)
    {
        // Deliberately INVERTED, not zeroed: a zero box sits at the origin
        // and would swallow rays aimed there. This one reports IsEmpty().
        bounds.min = {  1.0f,  1.0f,  1.0f };
        bounds.max = { -1.0f, -1.0f, -1.0f };
        return bounds;
    }

    bounds.min = bounds.max = vertices[0].position;
    for (UINT i = 1; i < vertexCount; ++i)
    {
        const XMFLOAT3& p = vertices[i].position;
        bounds.min.x = std::min(bounds.min.x, p.x);
        bounds.min.y = std::min(bounds.min.y, p.y);
        bounds.min.z = std::min(bounds.min.z, p.z);
        bounds.max.x = std::max(bounds.max.x, p.x);
        bounds.max.y = std::max(bounds.max.y, p.y);
        bounds.max.z = std::max(bounds.max.z, p.z);
    }
    return bounds;
}

// Every mesh reaches the GPU through here - AddMesh for procedural geometry
// and LoadMesh for files both end up at this call. That makes it the one
// place where bounds can be computed once and cover both; doing it in
// AddMesh would silently leave every loaded .obj with an empty box.
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
    // Last chance: the caller's MeshData goes out of scope after this.
    mesh.bounds      = ComputeBounds(vertices, vertexCount);

    // One submesh covering everything, unless a caller replaces it. Every
    // mesh having at least one means the draw loop never asks "does this
    // have submeshes?".
    mesh.submeshes.push_back(Submesh{ 0, indexCount, TextureHandle{}, L"" });
    return mesh;
}

Mesh CreateMesh(ID3D12Device* device, const MeshData& data)
{
    Mesh mesh = CreateMesh(device,
                           data.vertices.data(), UINT(data.vertices.size()),
                           data.indices.data(), UINT(data.indices.size()));

    // Replace the synthesised single submesh when the loader found real
    // material groups. Textures stay invalid here - only the ResourceManager
    // can turn a path into a handle, and it does that right after.
    if (!data.submeshes.empty())
    {
        mesh.submeshes.clear();
        for (const SubmeshData& source : data.submeshes)
        {
            mesh.submeshes.push_back(
                Submesh{ source.indexOffset, source.indexCount,
                         TextureHandle{}, source.materialName });
        }
    }
    return mesh;
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

MeshData MakeSphereMeshData(float radius, UINT slices, UINT stacks)
{
    slices = std::max(slices, 3u);
    stacks = std::max(stacks, 2u);

    MeshData mesh;
    mesh.vertices.reserve(size_t(slices + 1) * (stacks + 1));

    // A grid in (stack, slice). Both edges are duplicated - slice 0 and
    // slice `slices` are the same place with u = 0 and u = 1 - because a
    // vertex carries one uv and the seam needs both.
    for (UINT stack = 0; stack <= stacks; ++stack)
    {
        const float phi    = XM_PI * float(stack) / float(stacks); // 0 = north
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (UINT slice = 0; slice <= slices; ++slice)
        {
            const float theta = XM_2PI * float(slice) / float(slices);

            Vertex vertex;
            vertex.normal   = { sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta) };
            vertex.position = { vertex.normal.x * radius,
                                vertex.normal.y * radius,
                                vertex.normal.z * radius };
            // v = 0 at the north pole, matching D3D's top-left uv origin.
            vertex.uv = { float(slice) / float(slices), float(stack) / float(stacks) };
            mesh.vertices.push_back(vertex);
        }
    }

    // Same corner order as the cube: top-left, top-right, bottom-right,
    // bottom-left AS SEEN FROM OUTSIDE. Increasing theta moves right and
    // increasing phi moves down from that viewpoint, so the grid indices map
    // straight onto it.
    //
    // The two rows touching the poles collapse to a point, making those
    // triangles degenerate. The rasterizer discards them; keeping them costs
    // nothing and keeps the loop free of pole special cases.
    const UINT rowStride = slices + 1;
    for (UINT stack = 0; stack < stacks; ++stack)
    {
        for (UINT slice = 0; slice < slices; ++slice)
        {
            const uint32_t topLeft     = stack * rowStride + slice;
            const uint32_t topRight    = topLeft + 1;
            const uint32_t bottomLeft  = topLeft + rowStride;
            const uint32_t bottomRight = bottomLeft + 1;

            mesh.indices.insert(mesh.indices.end(),
                                { topLeft, topRight, bottomRight,
                                  topLeft, bottomRight, bottomLeft });
        }
    }
    return mesh;
}

MeshData MakeTorusMeshData(float majorRadius, float minorRadius,
                           UINT majorSegments, UINT minorSegments)
{
    majorSegments = std::max(majorSegments, 3u);
    minorSegments = std::max(minorSegments, 3u);

    MeshData mesh;
    mesh.vertices.reserve(size_t(majorSegments + 1) * (minorSegments + 1));

    // alpha runs around the RING (in the XY plane), beta around the TUBE.
    for (UINT major = 0; major <= majorSegments; ++major)
    {
        const float alpha = XM_2PI * float(major) / float(majorSegments);
        const float cosA  = std::cos(alpha);
        const float sinA  = std::sin(alpha);

        for (UINT minor = 0; minor <= minorSegments; ++minor)
        {
            const float beta = XM_2PI * float(minor) / float(minorSegments);
            const float cosB = std::cos(beta);
            const float sinB = std::sin(beta);

            Vertex vertex;
            // Outward from the tube's centre line: cosB along the ring's own
            // radial direction, sinB along the ring's axis (Z here).
            vertex.normal   = { cosB * cosA, cosB * sinA, sinB };
            vertex.position = { (majorRadius + minorRadius * cosB) * cosA,
                                (majorRadius + minorRadius * cosB) * sinA,
                                minorRadius * sinB };
            vertex.uv = { float(major) / float(majorSegments),
                          float(minor) / float(minorSegments) };
            mesh.vertices.push_back(vertex);
        }
    }

    // Seen from outside, increasing BETA moves right and increasing ALPHA
    // moves UP - the opposite of the sphere, where the second parameter went
    // down. So the row that plays "top" is major + 1, not major.
    const UINT rowStride = minorSegments + 1;
    for (UINT major = 0; major < majorSegments; ++major)
    {
        for (UINT minor = 0; minor < minorSegments; ++minor)
        {
            const uint32_t bottomLeft  = major * rowStride + minor;
            const uint32_t bottomRight = bottomLeft + 1;
            const uint32_t topLeft     = bottomLeft + rowStride;
            const uint32_t topRight    = topLeft + 1;

            mesh.indices.insert(mesh.indices.end(),
                                { topLeft, topRight, bottomRight,
                                  topLeft, bottomRight, bottomLeft });
        }
    }
    return mesh;
}
