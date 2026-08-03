#include "Graphics/Mesh.h"
#include "Core/Common.h"

#include <algorithm>
#include <unordered_map>
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

namespace
{
    // Any unit vector perpendicular to n. Used only where the real tangent is
    // unknowable - every triangle touching the vertex was degenerate. The
    // DIRECTION is arbitrary, but it must be perpendicular and finite, or the
    // shader's TBN collapses and the pixel goes black.
    //
    // Crossing with the axis n leans on LEAST guarantees the two are far from
    // parallel, so the cross product is never near zero.
    XMVECTOR AnyPerpendicular(FXMVECTOR n)
    {
        XMFLOAT3 a;
        XMStoreFloat3(&a, n);
        const XMVECTOR axis =
            (std::fabs(a.x) <= std::fabs(a.y) && std::fabs(a.x) <= std::fabs(a.z))
                ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
            : (std::fabs(a.y) <= std::fabs(a.z))
                ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
                : XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        return XMVector3Normalize(XMVector3Cross(n, axis));
    }
}

void GenerateTangents(std::vector<Vertex>& vertices,
                      std::vector<uint32_t>& indices)
{
    if (vertices.empty())
    {
        return;
    }

    // --- pass 0: split vertices that sit on a mirrored-uv seam ---
    //
    // Must run BEFORE accumulation, since it can grow `vertices` and
    // redirects some `indices` entries to the new copies. Recomputes the
    // same two degeneracy tests as the accumulation loop below (cheap next
    // to the parse that already produced this data) so a degenerate
    // triangle contributes no handedness opinion and never forces a split.
    {
        // 0 = not yet touched by any real triangle, otherwise the uv-winding
        // sign (+1/-1) of the first one that did.
        std::vector<int8_t> firstSign(vertices.size(), 0);
        // original vertex index -> the one duplicate created for "the other"
        // handedness, so every triangle of that second sign redirects to the
        // SAME copy instead of fragmenting into one duplicate per triangle.
        std::unordered_map<uint32_t, uint32_t> duplicateOf;

        const size_t triangleCount = indices.size() / 3;
        for (size_t tri = 0; tri < triangleCount; ++tri)
        {
            const size_t   base  = tri * 3;
            const uint32_t idx[3] = { indices[base], indices[base + 1], indices[base + 2] };
            if (idx[0] >= vertices.size() || idx[1] >= vertices.size() ||
                idx[2] >= vertices.size())
            {
                continue;
            }

            const XMVECTOR p0 = XMLoadFloat3(&vertices[idx[0]].position);
            const XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&vertices[idx[1]].position), p0);
            const XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&vertices[idx[2]].position), p0);
            const float crossSq = XMVectorGetX(XMVector3LengthSq(XMVector3Cross(e1, e2)));
            const float scaleSq = XMVectorGetX(XMVector3LengthSq(e1)) *
                                  XMVectorGetX(XMVector3LengthSq(e2));
            if (!(crossSq > 1e-12f * scaleSq))
            {
                continue;
            }

            const XMFLOAT2& uv0 = vertices[idx[0]].uv;
            const float du1 = vertices[idx[1]].uv.x - uv0.x, dv1 = vertices[idx[1]].uv.y - uv0.y;
            const float du2 = vertices[idx[2]].uv.x - uv0.x, dv2 = vertices[idx[2]].uv.y - uv0.y;
            const float det = du1 * dv2 - du2 * dv1;
            // Relative to the uv edges' OWN lengths, matching the geometric
            // test above - not a fixed epsilon. A fixed one would reject a
            // legitimately tiny (but not degenerate) uv footprint, which is
            // ordinary in a densely packed texture atlas.
            const float uvScaleSq = (du1 * du1 + dv1 * dv1) * (du2 * du2 + dv2 * dv2);
            if (!(det * det > 1e-12f * uvScaleSq))
            {
                continue;
            }

            const int8_t sign = (det < 0.0f) ? -1 : 1;

            for (int c = 0; c < 3; ++c)
            {
                const uint32_t v = idx[c];
                if (firstSign[v] == 0)
                {
                    firstSign[v] = sign;
                    continue;
                }
                if (firstSign[v] == sign)
                {
                    continue;
                }

                // A triangle of the opposite handedness touches a vertex
                // that already committed to one. Averaging the two would
                // partially or - for a clean mirror - EXACTLY cancel, which
                // is what a naive accumulator does and why this pass exists.
                auto it = duplicateOf.find(v);
                uint32_t dupIndex;
                if (it == duplicateOf.end())
                {
                    dupIndex = uint32_t(vertices.size());
                    vertices.push_back(vertices[v]); // identical pos/normal/uv
                    firstSign.push_back(sign);
                    duplicateOf.emplace(v, dupIndex);
                }
                else
                {
                    dupIndex = it->second;
                }
                indices[base + c] = dupIndex;
            }
        }
    }

    // Accumulated per vertex, because a vertex shared by several triangles
    // should get their average - otherwise the tangent jumps at every edge
    // and the normal map creases along the triangulation. The seam split
    // above is what keeps this averaging from mixing two triangles that
    // disagree about which way is "forward".
    std::vector<XMFLOAT3> tangents(vertices.size(), XMFLOAT3{ 0, 0, 0 });
    std::vector<XMFLOAT3> bitangents(vertices.size(), XMFLOAT3{ 0, 0, 0 });

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t i0 = indices[i + 0];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
        {
            continue;
        }

        const XMVECTOR p0 = XMLoadFloat3(&vertices[i0].position);
        const XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&vertices[i1].position), p0);
        const XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&vertices[i2].position), p0);

        // |e1 x e2|^2 = |e1|^2 |e2|^2 sin^2(theta), so dividing by the two
        // squared lengths leaves sin^2 - a pure angle, independent of how big
        // the model is. A zero-length edge makes the right-hand side 0 and
        // fails the test too, which is exactly the sphere's pole case.
        const float crossSq = XMVectorGetX(XMVector3LengthSq(XMVector3Cross(e1, e2)));
        const float scaleSq = XMVectorGetX(XMVector3LengthSq(e1)) *
                              XMVectorGetX(XMVector3LengthSq(e2));
        if (!(crossSq > 1e-12f * scaleSq))  // NOT >, so a NaN also falls out
        {
            continue;
        }

        const XMFLOAT2& uv0 = vertices[i0].uv;
        const float du1 = vertices[i1].uv.x - uv0.x, dv1 = vertices[i1].uv.y - uv0.y;
        const float du2 = vertices[i2].uv.x - uv0.x, dv2 = vertices[i2].uv.y - uv0.y;

        // The determinant of the uv edge matrix. Zero means the triangle has
        // no area in texture space - the exporter collapsed it - so there is
        // no direction for "+U runs this way" to name.
        const float det = du1 * dv2 - du2 * dv1;
        // Relative to the uv edges' own lengths, matching the geometric test
        // above - NOT a fixed epsilon against raw uv units. A fixed one would
        // reject a legitimately tiny uv footprint, which is ordinary for a
        // triangle packed into a small corner of a shared texture atlas; the
        // same physical triangle, unwrapped at a different atlas scale,
        // would then pass or fail this test for a reason that has nothing to
        // do with whether it is actually degenerate.
        const float uvScaleSq = (du1 * du1 + dv1 * dv1) * (du2 * du2 + dv2 * dv2);
        if (!(det * det > 1e-12f * uvScaleSq))
        {
            continue;
        }
        const float r = 1.0f / det;

        // Solve for the two object-space directions that the uv axes map to.
        const XMVECTOR t = XMVectorScale(
            XMVectorSubtract(XMVectorScale(e1, dv2), XMVectorScale(e2, dv1)), r);
        const XMVECTOR b = XMVectorScale(
            XMVectorSubtract(XMVectorScale(e2, du1), XMVectorScale(e1, du2)), r);

        for (const uint32_t index : { i0, i1, i2 })
        {
            XMStoreFloat3(&tangents[index],
                          XMVectorAdd(XMLoadFloat3(&tangents[index]), t));
            XMStoreFloat3(&bitangents[index],
                          XMVectorAdd(XMLoadFloat3(&bitangents[index]), b));
        }
    }

    for (size_t v = 0; v < vertices.size(); ++v)
    {
        const XMVECTOR n         = XMVector3Normalize(XMLoadFloat3(&vertices[v].normal));
        const XMVECTOR summed    = XMLoadFloat3(&tangents[v]);
        const float    summedSq  = XMVectorGetX(XMVector3LengthSq(summed));

        XMVECTOR tangent;
        if (!(summedSq > 1e-20f))
        {
            // Every triangle here was rejected, or they cancelled out exactly.
            tangent = AnyPerpendicular(n);
        }
        else
        {
            // Gram-Schmidt: the accumulated direction is only approximately
            // in the surface, so subtract off whatever part points along the
            // normal. Interpolating between vertices tilts it again, which is
            // why the pixel shader repeats this.
            const XMVECTOR projected = XMVectorSubtract(
                summed, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, summed))));

            // Nothing left means the tangent was parallel to the normal -
            // normalizing that is a divide by zero.
            tangent = (XMVectorGetX(XMVector3LengthSq(projected)) > 1e-20f)
                        ? XMVector3Normalize(projected)
                        : AnyPerpendicular(n);
        }

        // Does the bitangent we computed agree with cross(n, t), or point the
        // other way? That one bit is what mirrored UV islands need.
        const float agreement = XMVectorGetX(
            XMVector3Dot(XMVector3Cross(n, tangent), XMLoadFloat3(&bitangents[v])));

        XMFLOAT3 stored;
        XMStoreFloat3(&stored, tangent);
        vertices[v].tangent = { stored.x, stored.y, stored.z,
                                agreement < 0.0f ? -1.0f : 1.0f };
    }
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
    // Tangents are filled in HERE rather than in each producer, so a loader
    // or a Make*MeshData that never heard of tangents still yields a complete
    // vertex. Both ways into the ResourceManager - AddMesh and LoadMesh -
    // come through this overload, so there is no path that skips it.
    //
    // The copy is the price of leaving MeshData const for its producers. It
    // is one memcpy of the vertices and indices; laevat's 216,912 of them
    // cost a few milliseconds against the 5,200 ms its .obj already spent
    // being parsed.
    //
    // `indices` is copied too, not just `vertices` - GenerateTangents may
    // redirect some entries to a duplicate vertex it appends when a
    // mirrored-uv seam needs splitting. The array's SIZE never changes, only
    // some of its values, so `data.submeshes`' offset/count ranges (computed
    // against the original array) still land on the same triangles.
    std::vector<Vertex>   vertices = data.vertices;
    std::vector<uint32_t> indices  = data.indices;
    GenerateTangents(vertices, indices);

    Mesh mesh = CreateMesh(device,
                           vertices.data(), UINT(vertices.size()),
                           indices.data(), UINT(indices.size()));

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
