// ResourceManager.h : loads and caches meshes, textures and shaders.
#pragma once

#include "Graphics/Mesh.h"
#include "Graphics/DescriptorAllocator.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class GraphicsDevice;

// Handles, not pointers. An index survives the storage vector reallocating,
// and it costs 4 bytes in a Material instead of 8-plus-lifetime-questions.
//
// No generation counter yet: nothing is ever unloaded, so a handle cannot go
// stale. When the Phase 10 editor starts freeing assets, add a generation
// field here and bump it on release - that is what turns a dangling handle
// into a detectable error instead of a silent wrong-mesh.
struct MeshHandle
{
    static constexpr uint32_t kInvalid = uint32_t(-1);
    uint32_t index = kInvalid;
    bool IsValid() const { return index != kInvalid; }
};

struct TextureHandle
{
    static constexpr uint32_t kInvalid = uint32_t(-1);
    uint32_t index = kInvalid;
    bool IsValid() const { return index != kInvalid; }
};

// Owns every loaded asset for the lifetime of the app. Loading the same path
// twice returns the same handle and does no work the second time.
class ResourceManager
{
public:
    ResourceManager(GraphicsDevice& device, DescriptorAllocator& srvAllocator);

    ResourceManager(const ResourceManager&)            = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // --- meshes ---
    // Registers geometry built in code. 'name' is the cache key, so calling
    // it twice with the same name returns the first mesh.
    MeshHandle AddMesh(const std::wstring& name, const MeshData& data);
    // Loads an .obj from Assets/. fitToSize > 0 recenters and rescales it -
    // models authored elsewhere are rarely in our units.
    MeshHandle LoadMesh(const std::wstring& fileName, float fitToSize = 0.0f);

    // Turns a saved name back into a handle: a '#' prefix means a procedural
    // recipe built in code, anything else is a file under Assets/. This is
    // what a scene file's mesh names go through on load, so the recipes and
    // their parameters live in exactly one place.
    MeshHandle ResolveMesh(const std::wstring& name);

    const Mesh& GetMesh(MeshHandle handle) const;

    // The canonical name a handle was registered under - what a scene file
    // stores instead of the index, which means nothing in the next run.
    // Empty for an invalid handle.
    const std::wstring& MeshName(MeshHandle handle) const;
    const std::wstring& TextureName(TextureHandle handle) const;

    // "Is this already loaded?" without loading it. The asset browser needs
    // to say so from the first frame, and assets loaded by the startup scene
    // were cached long before the browser existed.
    MeshHandle    FindMesh(const std::wstring& name) const;
    TextureHandle FindTexture(const std::wstring& name) const;

    // --- textures ---
    // Loads an image from Assets/ into a default-heap texture and creates
    // its SRV in the shader-visible heap.
    TextureHandle LoadTexture(const std::wstring& fileName);
    // Uploads pixels produced in code. Same relationship to LoadTexture that
    // AddMesh has to LoadMesh: one cache, two ways in.
    TextureHandle AddTexture(const std::wstring& name, const struct ImageData& image);

    // A 1x1 white texel, so a material that names no image still has
    // something to sample. Multiplying the albedo by white leaves it alone.
    TextureHandle DefaultTexture() const { return m_defaultTexture; }

    DescriptorHandle TextureSRV(TextureHandle handle) const;

    // Pixel dimensions of a loaded texture. Anything laying one out needs
    // them - a preview drawn square would squash a 16:9 image. Zero for an
    // invalid handle rather than throwing: callers here are UI, not draws.
    void TextureSize(TextureHandle handle, UINT& outWidth, UINT& outHeight) const;

    // --- shaders ---
    // Cached on path + entry point + target, since one file holds several.
    Microsoft::WRL::ComPtr<ID3DBlob> LoadShader(const std::filesystem::path& path,
                                                const char* entryPoint,
                                                const char* target);

    // Load counts vs request counts - the cache is working when the two
    // differ. Surfaced in the debug title bar.
    struct Stats
    {
        UINT meshLoads = 0, meshRequests = 0;
        UINT textureLoads = 0, textureRequests = 0;
        UINT shaderCompiles = 0, shaderRequests = 0;
    };
    const Stats& GetStats() const { return m_stats; }

private:
    struct Texture
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        DescriptorHandle                       srv;
    };

    void BeginUpload();
    void EndUpload();

    GraphicsDevice&      m_device;
    DescriptorAllocator& m_srvAllocator;

    // A command list used only at load time, so uploads never contend with
    // the per-frame allocators.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_uploadAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_uploadCommandList;

    // The cache key for an asset name.
    //
    // POLICY (decided in 10.5, deferred from 10.3): lowercase, forward
    // slashes. The cache keys on the STRING, and Windows opens "Laevat.obj"
    // and "laevat.obj" as the same file - without this, two spellings of one
    // path become two entries and the same 23 MB of geometry is uploaded
    // twice. Scene files store this canonical form.
    //
    // Lowercasing is a WINDOWS ASSUMPTION. On a case-sensitive filesystem it
    // is the wrong answer and this has to become the identity function.
    // Names starting with '#' are procedural recipes, not paths, and pass
    // through untouched.
    static std::wstring NormalizeKey(const std::wstring& name);

    std::vector<Mesh>    m_meshes;
    std::vector<Texture> m_textures;

    // Parallel to the two vectors above: the canonical name each was
    // registered under, so serialization can go from handle back to name.
    std::vector<std::wstring> m_meshNames;
    std::vector<std::wstring> m_textureNames;

    TextureHandle m_defaultTexture;

    std::unordered_map<std::wstring, MeshHandle>    m_meshCache;
    std::unordered_map<std::wstring, TextureHandle> m_textureCache;
    std::unordered_map<std::string,
                       Microsoft::WRL::ComPtr<ID3DBlob>> m_shaderCache;

    Stats m_stats;
};
