#include "Loaders/ObjLoader.h"

#include "Core/TextEncoding.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cfloat>

using namespace DirectX;

namespace
{
    // One corner of a face, exactly as OBJ writes it: three INDEPENDENT
    // indices into three separate lists. A sphere's seam is the classic
    // case - the same 3D position with two different uvs, so "position 1"
    // shows up paired with uv 1 on one side and uv 33 on the other.
    // The GPU has one index into one vertex list, so every distinct
    // combination has to become its own vertex.
    struct VertexRef
    {
        int position = -1;
        int uv       = -1;
        int normal   = -1;

        bool operator==(const VertexRef& other) const
        {
            return position == other.position
                && uv       == other.uv
                && normal   == other.normal;
        }
    };

    struct VertexRefHash
    {
        size_t operator()(const VertexRef& ref) const noexcept
        {
            // +1 so the "absent" value (-1) does not hash to zero.
            size_t hash = size_t(ref.position + 1) * 73856093u;
            hash ^= size_t(ref.uv + 1) * 19349663u;
            hash ^= size_t(ref.normal + 1) * 83492791u;
            return hash;
        }
    };

    // OBJ indices are 1-based, and a NEGATIVE index counts backwards from
    // the end of the list read SO FAR. Returns 0-based, or -1 when absent.
    int ResolveIndex(const std::string& text, size_t listSize)
    {
        if (text.empty())
        {
            return -1;
        }
        const int raw = std::stoi(text);
        if (raw > 0)
        {
            return raw - 1;
        }
        if (raw < 0)
        {
            return int(listSize) + raw;
        }
        return -1; // 0 is not a legal OBJ index
    }

    // Accepts "12", "12/7", "12/7/3" and "12//3".
    VertexRef ParseFaceToken(const std::string& token, size_t positionCount,
                             size_t uvCount, size_t normalCount)
    {
        std::string part[3];
        int slot = 0;
        for (const char c : token)
        {
            if (c == '/')
            {
                if (++slot > 2)
                {
                    break;
                }
            }
            else
            {
                part[slot] += c;
            }
        }

        VertexRef ref;
        ref.position = ResolveIndex(part[0], positionCount);
        ref.uv       = ResolveIndex(part[1], uvCount);
        ref.normal   = ResolveIndex(part[2], normalCount);
        return ref;
    }

    // Fallback when the file carries no vn lines: sum the face normals
    // touching each vertex, then normalize. Vertices shared between faces
    // end up smooth, which is the usual intent for a model without normals.
    void ComputeSmoothNormals(MeshData& mesh)
    {
        for (Vertex& vertex : mesh.vertices)
        {
            vertex.normal = { 0.0f, 0.0f, 0.0f };
        }

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const uint32_t i0 = mesh.indices[i];
            const uint32_t i1 = mesh.indices[i + 1];
            const uint32_t i2 = mesh.indices[i + 2];

            const XMVECTOR p0 = XMLoadFloat3(&mesh.vertices[i0].position);
            const XMVECTOR p1 = XMLoadFloat3(&mesh.vertices[i1].position);
            const XMVECTOR p2 = XMLoadFloat3(&mesh.vertices[i2].position);

            // cross(edge1, edge2) points outward for our clockwise-front
            // winding - the same convention the cube mesh is built with.
            const XMVECTOR faceNormal = XMVector3Cross(XMVectorSubtract(p1, p0),
                                                       XMVectorSubtract(p2, p0));

            for (const uint32_t index : { i0, i1, i2 })
            {
                XMVECTOR sum = XMLoadFloat3(&mesh.vertices[index].normal);
                XMStoreFloat3(&mesh.vertices[index].normal,
                              XMVectorAdd(sum, faceNormal));
            }
        }

        for (Vertex& vertex : mesh.vertices)
        {
            XMStoreFloat3(&vertex.normal,
                          XMVector3Normalize(XMLoadFloat3(&vertex.normal)));
        }
    }
}

MeshData LoadObj(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Model file not found:\n" + ToUtf8(path.wstring()));
    }

    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Could not open model file:\n" + ToUtf8(path.wstring()));
    }

    // The three source lists, each with its own numbering.
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT2> uvs;
    std::vector<XMFLOAT3> normals;

    MeshData mesh;
    std::unordered_map<VertexRef, uint32_t, VertexRefHash> emitted;
    bool fileHasNormals = false;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;

        if (tag == "v")
        {
            float x = 0, y = 0, z = 0;
            stream >> x >> y >> z;
            // OBJ is right-handed (+Z toward the viewer); our world is
            // left-handed (+Z away). Negating Z describes the same physical
            // point in our system.
            //
            // It does NOT fix the winding on its own - see the face loop
            // below, which reverses each triangle to compensate.
            positions.push_back({ x, y, -z });
        }
        else if (tag == "vt")
        {
            float u = 0, v = 0;
            stream >> u >> v;
            // OBJ's V origin is the BOTTOM-left corner, D3D's is the top
            // left. Skip this flip and every texture comes out upside down.
            uvs.push_back({ u, 1.0f - v });
        }
        else if (tag == "vn")
        {
            float x = 0, y = 0, z = 0;
            stream >> x >> y >> z;
            normals.push_back({ x, y, -z }); // same handedness fix
            fileHasNormals = true;
        }
        else if (tag == "f")
        {
            std::vector<uint32_t> corners;
            std::string token;
            while (stream >> token)
            {
                const VertexRef ref = ParseFaceToken(token, positions.size(),
                                                     uvs.size(), normals.size());

                // Seen this exact position/uv/normal triple before? Reuse
                // the vertex. This is the dedup that keeps a 544-position
                // sphere from exploding into one vertex per face corner.
                auto it = emitted.find(ref);
                if (it == emitted.end())
                {
                    Vertex vertex = {};
                    if (ref.position >= 0 && size_t(ref.position) < positions.size())
                    {
                        vertex.position = positions[ref.position];
                    }
                    if (ref.uv >= 0 && size_t(ref.uv) < uvs.size())
                    {
                        vertex.uv = uvs[ref.uv];
                    }
                    if (ref.normal >= 0 && size_t(ref.normal) < normals.size())
                    {
                        vertex.normal = normals[ref.normal];
                    }

                    it = emitted.emplace(ref, uint32_t(mesh.vertices.size())).first;
                    mesh.vertices.push_back(vertex);
                }
                corners.push_back(it->second);
            }

            // Triangulate as a fan around the first corner: a quad becomes
            // (0,1,2) and (0,2,3). Correct for convex polygons, which is
            // what exporters emit.
            //
            // The last two are emitted SWAPPED, and that is not a typo.
            // Negating Z above is a mirror, and a mirror reverses triangle
            // orientation - what was front-facing becomes back-facing. Left
            // uncorrected, back-face culling keeps the INSIDE of every
            // loaded model: a torus shows the far inner wall of its tube
            // through the hole instead of a solid ring. Reversing the
            // winding here undoes exactly what the mirror did.
            for (size_t i = 2; i < corners.size(); ++i)
            {
                mesh.indices.push_back(corners[0]);
                mesh.indices.push_back(corners[i]);
                mesh.indices.push_back(corners[i - 1]);
            }
        }
        // Everything else (#, o, g, s, usemtl, mtllib) is ignored.
    }

    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        throw std::runtime_error("No geometry found in:\n" + ToUtf8(path.wstring()));
    }

    if (!fileHasNormals)
    {
        ComputeSmoothNormals(mesh);
    }

    return mesh;
}

void FitMeshToSize(MeshData& mesh, float targetSize)
{
    if (mesh.vertices.empty() || targetSize <= 0.0f)
    {
        return;
    }

    XMFLOAT3 minCorner = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
    XMFLOAT3 maxCorner = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const Vertex& vertex : mesh.vertices)
    {
        minCorner.x = std::min(minCorner.x, vertex.position.x);
        minCorner.y = std::min(minCorner.y, vertex.position.y);
        minCorner.z = std::min(minCorner.z, vertex.position.z);
        maxCorner.x = std::max(maxCorner.x, vertex.position.x);
        maxCorner.y = std::max(maxCorner.y, vertex.position.y);
        maxCorner.z = std::max(maxCorner.z, vertex.position.z);
    }

    const XMFLOAT3 center = { (minCorner.x + maxCorner.x) * 0.5f,
                              (minCorner.y + maxCorner.y) * 0.5f,
                              (minCorner.z + maxCorner.z) * 0.5f };

    // Scale by the LARGEST dimension so the whole model fits inside a box
    // of targetSize - matching the smallest would push the rest outside.
    const float extent = std::max({ maxCorner.x - minCorner.x,
                                    maxCorner.y - minCorner.y,
                                    maxCorner.z - minCorner.z });
    if (extent <= 0.0f)
    {
        return;
    }
    const float scale = targetSize / extent;

    for (Vertex& vertex : mesh.vertices)
    {
        vertex.position.x = (vertex.position.x - center.x) * scale;
        vertex.position.y = (vertex.position.y - center.y) * scale;
        vertex.position.z = (vertex.position.z - center.z) * scale;
        // Normals are directions: uniform scaling does not change them, and
        // the recentering is a translation, which never applies to normals.
    }
}
