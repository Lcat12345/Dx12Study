// ResourceManager.h : loads and caches meshes, textures and shaders.
#pragma once

#include "Graphics/Handles.h"
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

    // A 1x1 (128, 128, 255) texel - the encoding of "straight out of the
    // surface". Bound whenever a material names no normal map, so the shader
    // never branches and the picture is unchanged from before normal mapping.
    TextureHandle DefaultNormalTexture() const { return m_defaultNormalTexture; }

    // --- cube textures ---
    // 'name' is a LOGICAL name, not a path: "Test" resolves to the six faces
    // under Assets/Skyboxes/Test/. The scene file stores that one name, so a
    // skybox is one asset rather than six coincidentally related files.
    //
    // Throws when a face is missing, or when the six are not all square and
    // the same size - a cube map with mismatched faces has no meaning.
    CubeTextureHandle LoadCubeTexture(const std::wstring& name);
    CubeTextureHandle FindCubeTexture(const std::wstring& name) const;

    DescriptorHandle     CubeTextureSRV(CubeTextureHandle handle) const;
    const std::wstring&  CubeTextureName(CubeTextureHandle handle) const;

    // The face order D3D12 expects, as subresource indices 0..5.
    static const wchar_t* const kCubeFaceSuffixes[6];

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
    // Same shape as a 2D texture, different view. Kept in its own list so a
    // handle of one kind can never index the other.
    std::vector<Texture> m_cubeTextures;
    std::vector<std::wstring> m_cubeTextureNames;
    std::unordered_map<std::wstring, CubeTextureHandle> m_cubeTextureCache;

    // Parallel to the two vectors above: the canonical name each was
    // registered under, so serialization can go from handle back to name.
    std::vector<std::wstring> m_meshNames;
    std::vector<std::wstring> m_textureNames;

    TextureHandle m_defaultTexture;
    TextureHandle m_defaultNormalTexture;

    std::unordered_map<std::wstring, MeshHandle>    m_meshCache;
    std::unordered_map<std::wstring, TextureHandle> m_textureCache;
    std::unordered_map<std::string,
                       Microsoft::WRL::ComPtr<ID3DBlob>> m_shaderCache;

    Stats m_stats;
};
