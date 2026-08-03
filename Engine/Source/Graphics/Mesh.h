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
    // Which way the texture's +U runs across the surface, in object space.
    //
    // A normal map stores directions in TANGENT space - relative to the
    // texture's own axes - so the shader cannot use one without knowing where
    // those axes point on this particular triangle. normal alone is not
    // enough: it fixes only one of the three axes.
    //
    // float4, not float3: `w` is +1 or -1 and says which way the bitangent
    // goes. Mirrored UVs (one texture island reused flipped, which is how
    // most character models save space) flip it, and without that sign those
    // islands light from the wrong side.
    DirectX::XMFLOAT4 tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
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

// Fills in every vertex's tangent from the positions and uvs around it.
//
// Called by CreateMesh, so procedural geometry and loaded files get the same
// treatment - the vertex contract is "all four fields are valid", and there
// is no second path that could produce a half-filled vertex.
//
// Degenerate triangles are excluded TWICE, on two independent grounds, and
// the two catch different failures:
//
//   - zero UV area - a face the exporter never unwrapped. There is no
//     direction for "+U runs this way" to name, and the 1/det that would
//     compute it blows up.
//
//   - zero geometric area - a face with no surface. MakeSphereMeshData leaves
//     64 of these on purpose, one per pole quad, because letting the
//     rasterizer discard them is cheaper than special-casing the poles. Their
//     uvs are perfectly good, so the UV test does NOT catch them.
//
//     What they corrupt is HANDEDNESS, not the tangent. Worked through for a
//     pole quad, where p0 == p1 makes the first edge zero:
//         t = (e1*dv2 - e2*dv1) * r,  dv1 = 0  ->  exactly zero, harmless
//         b = (e2*du1 - e1*du2) * r            ->  large and meaningless
//     so the accumulated tangent survives on the quad's other, real triangle
//     while the accumulated bitangent is polluted - and the bitangent is what
//     decides `w`. A wrong `w` lights that texture island from the wrong side.
//
// NaN is a different failure: it needs EVERY triangle at a vertex to be
// rejected, leaving a zero sum to normalize. That is what the fallback below
// is for, not these tests.
//
// Both tests are relative to the triangle's own size (the uv one against the
// uv edges' own lengths, not a fixed epsilon), so they mean the same thing on
// a 1-unit sphere, a 64,000-unit model, and a tightly packed texture atlas
// where a triangle's uv footprint is a tiny fraction of the [0,1] square.
//
// A THIRD case needs handling that is not a degenerate triangle at all: a
// vertex sitting on a MIRRORED-UV SEAM, shared by one triangle whose uv winds
// the same way as its geometry and one whose uv winds the opposite way - the
// standard way a symmetric character texture reuses one arm's pixels for the
// other, flipped. Both triangles are perfectly valid; their tangents point in
// genuinely different directions (roughly opposite in one axis), and summing
// them at the shared vertex does not average two similar answers - it
// SUBTRACTS them, which for a clean mirror cancels EXACTLY to zero and falls
// through to the arbitrary fallback, discarding both real directions in
// favour of a direction that matches neither. This function detects that
// case by each triangle's uv-handedness sign and duplicates the vertex - one
// copy per handedness - the same fix a content pipeline makes by not
// deduplicating vertices across a uv seam in the first place. `indices` is
// therefore mutable: values may be redirected to a newly appended vertex, but
// the array never changes SIZE, so index ranges a submesh already recorded
// (offset and count into this same array) stay valid without adjustment.
void GenerateTangents(std::vector<Vertex>& vertices,
                      std::vector<uint32_t>& indices);

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

// A UV sphere: rings of quads from pole to pole.
//
// The default scene needs at least one CURVED surface. Cube, pyramid and
// floor are all flat, and a flat surface cannot show what smooth normals do
// - the specular highlight just lights a whole facet at once. This is also
// the shape normal mapping and shadows are worth looking at.
//
// Normals are the normalised position, which is what makes a sphere the
// cheapest possible smooth-shading test.
MeshData MakeSphereMeshData(float radius, UINT slices, UINT stacks);

// A torus standing UPRIGHT - its ring lies in the XY plane.
//
// Deliberately not flat in XZ: spinning a flat torus about Y is
// rotationally symmetric and looks frozen. Upright, the rotation shows.
MeshData MakeTorusMeshData(float majorRadius, float minorRadius,
                           UINT majorSegments, UINT minorSegments);
