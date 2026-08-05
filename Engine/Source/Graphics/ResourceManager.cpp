#include "Graphics/ResourceManager.h"

#include "Core/Common.h"
#include "Core/TextEncoding.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Image.h"
#include "Graphics/SwapChain.h"
#include "Loaders/ObjLoader.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

ResourceManager::ResourceManager(GraphicsDevice& device, DescriptorAllocator& srvAllocator,
                                 const RuntimePaths& paths)
    : m_paths(paths)
    , m_device(device)
    , m_srvAllocator(srvAllocator)
{
    ThrowIfFailed(m_device.Device()->CreateCommandAllocator(
                      D3D12_COMMAND_LIST_TYPE_DIRECT,
                      IID_PPV_ARGS(&m_uploadAllocator)),
                  "CreateCommandAllocator(upload)");

    ThrowIfFailed(m_device.Device()->CreateCommandList(
                      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                      m_uploadAllocator.Get(), nullptr,
                      IID_PPV_ARGS(&m_uploadCommandList)),
                  "CreateCommandList(upload)");
    ThrowIfFailed(m_uploadCommandList->Close(), "Close upload list");

    // A material with no texture of its own still has to sample something.
    // One white texel multiplies the albedo by 1, so the material's colour
    // comes through untouched - which is exactly what a freshly placed mesh
    // should look like before an image is chosen for it.
    ImageData white;
    white.width  = 1;
    white.height = 1;
    white.pixels = { 255, 255, 255, 255 };
    m_defaultTexture = AddTexture(L"#white", white);

    // The normal-map equivalent of that white texel.
    //
    // A normal map stores a direction as a colour, packed from [-1,1] into
    // [0,1]: the shader reads it and does `value * 2 - 1`. So the texel that
    // means "no change - the surface points exactly the way the geometry
    // says" is (0, 0, 1) encoded, which is (128, 128, 255).
    //
    // 128 rather than 127.5 leaves x and y at 0.0039 instead of 0, a tilt of
    // 0.2 degrees. Invisible, and the alternative is a texture format with
    // room for the half.
    ImageData flatNormal;
    flatNormal.width  = 1;
    flatNormal.height = 1;
    flatNormal.pixels = { 128, 128, 255, 255 };
    m_defaultNormalTexture = AddTexture(L"#flatnormal", flatNormal);
}

void ResourceManager::BeginUpload()
{
    ThrowIfFailed(m_uploadAllocator->Reset(), "Upload allocator Reset");
    ThrowIfFailed(m_uploadCommandList->Reset(m_uploadAllocator.Get(), nullptr),
                  "Upload list Reset");
}

void ResourceManager::EndUpload()
{
    ThrowIfFailed(m_uploadCommandList->Close(), "Upload list Close");
    ID3D12CommandList* lists[] = { m_uploadCommandList.Get() };
    m_device.Queue()->ExecuteCommandLists(1, lists);

    // Staging buffers die when the caller returns, so the GPU has to be
    // finished with them. Load time only - never the per-frame path.
    m_device.WaitForGpu();
}

std::wstring ResourceManager::NormalizeKey(const std::wstring& name)
{
    // Procedural recipes are names, not paths. '#cube' is already canonical.
    if (name.empty() || name.front() == L'#')
    {
        return name;
    }

    std::wstring key = name;
    std::replace(key.begin(), key.end(), L'\\', L'/');
    // On WIDE characters, never on the UTF-8 bytes: tolower() on a char
    // would slice multi-byte sequences apart.
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return wchar_t(std::towlower(c)); });
    return key;
}

// ---------------------------------------------------------------- meshes

MeshHandle ResourceManager::AddMesh(const std::wstring& name, const MeshData& data)
{
    ++m_stats.meshRequests;

    const std::wstring key = NormalizeKey(name);

    auto it = m_meshCache.find(key);
    if (it != m_meshCache.end())
    {
        return it->second;
    }

    const MeshHandle handle{ uint32_t(m_meshes.size()) };
    m_meshes.push_back(CreateMesh(m_device.Device(), data));
    m_meshNames.push_back(key);
    m_meshCache.emplace(key, handle);
    ++m_stats.meshLoads;
    return handle;
}

MeshHandle ResourceManager::LoadMesh(const std::wstring& fileName, float fitToSize)
{
    ++m_stats.meshRequests;

    const std::wstring key = NormalizeKey(fileName);

    auto it = m_meshCache.find(key);
    if (it != m_meshCache.end())
    {
        return it->second; // already on the GPU - no file read, no upload
    }

    // Opened by the canonical name too, so there is exactly one spelling in
    // play. Windows resolves it to the file whatever its case on disk.
    MeshData data = LoadObj(m_paths.assetDir / key, m_paths.assetDir);
    if (fitToSize > 0.0f)
    {
        FitMeshToSize(data, fitToSize);
    }

    Mesh mesh = CreateMesh(m_device.Device(), data);

    // The loader hands over texture PATHS because it cannot make handles.
    // Resolving them here is what connects a .mtl's material to a real SRV.
    // A texture that fails to load leaves the submesh's handle invalid,
    // which the renderer reads as "use the MeshRenderer's" - one broken
    // texture must not cost the whole model.
    for (size_t i = 0; i < mesh.submeshes.size() && i < data.submeshes.size(); ++i)
    {
        const std::wstring& texturePath = data.submeshes[i].diffuseTexture;
        if (texturePath.empty())
        {
            continue;
        }
        try
        {
            mesh.submeshes[i].texture = LoadTexture(texturePath);
        }
        catch (const std::exception&)
        {
            // Left invalid on purpose.
        }
    }

    const MeshHandle handle{ uint32_t(m_meshes.size()) };
    m_meshes.push_back(std::move(mesh));
    m_meshNames.push_back(key);
    m_meshCache.emplace(key, handle);
    ++m_stats.meshLoads;
    return handle;
}

MeshHandle ResourceManager::ResolveMesh(const std::wstring& name)
{
    if (name.empty())
    {
        return MeshHandle{};
    }
    if (name.front() != L'#')
    {
        return LoadMesh(name);
    }

    // The procedural recipes. Their PARAMETERS live here and nowhere else -
    // a name like "#floor" has to mean the same geometry in the scene that
    // saved it and the one that loads it.
    const std::wstring key = NormalizeKey(name);
    if (key == L"#cube")    { return AddMesh(key, MakeCubeMeshData()); }
    if (key == L"#pyramid") { return AddMesh(key, MakePyramidMeshData()); }
    if (key == L"#floor")   { return AddMesh(key, MakeFloorMeshData(40.0f, 20.0f)); }
    if (key == L"#sphere")  { return AddMesh(key, MakeSphereMeshData(1.0f, 32, 16)); }
    if (key == L"#capsule") { return AddMesh(key, MakeCapsuleMeshData(0.5f, 0.5f, 24, 8)); }
    if (key == L"#torus")   { return AddMesh(key, MakeTorusMeshData(1.0f, 0.4f, 48, 24)); }

    return MeshHandle{}; // unknown recipe - the caller reports it
}

const Mesh& ResourceManager::GetMesh(MeshHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_meshes.size())
    {
        throw std::runtime_error("GetMesh called with an invalid handle");
    }
    return m_meshes[handle.index];
}

const std::wstring& ResourceManager::MeshName(MeshHandle handle) const
{
    static const std::wstring kNone;
    if (!handle.IsValid() || handle.index >= m_meshNames.size())
    {
        return kNone;
    }
    return m_meshNames[handle.index];
}

const std::wstring& ResourceManager::TextureName(TextureHandle handle) const
{
    static const std::wstring kNone;
    if (!handle.IsValid() || handle.index >= m_textureNames.size())
    {
        return kNone;
    }
    return m_textureNames[handle.index];
}

MeshHandle ResourceManager::FindMesh(const std::wstring& name) const
{
    // Not a request: asking does no work and must not skew the cache stats.
    auto it = m_meshCache.find(NormalizeKey(name));
    return it == m_meshCache.end() ? MeshHandle{} : it->second;
}

TextureHandle ResourceManager::FindTexture(const std::wstring& name) const
{
    auto it = m_textureCache.find(NormalizeKey(name));
    return it == m_textureCache.end() ? TextureHandle{} : it->second;
}

// ---------------------------------------------------------------- textures

TextureHandle ResourceManager::LoadTexture(const std::wstring& fileName)
{
    const std::wstring key = NormalizeKey(fileName);

    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end())
    {
        // The whole point of the cache: five materials naming the same
        // image decode it once and share one SRV slot.
        ++m_stats.textureRequests;
        return it->second;
    }
    // Decoding is the only file-specific part; everything after it is the
    // same upload whether the pixels came from disk or from code.
    return AddTexture(key, LoadImageRGBA(m_paths.assetDir / key));
}

TextureHandle ResourceManager::AddTexture(const std::wstring& name, const ImageData& image)
{
    ++m_stats.textureRequests;

    const std::wstring key = NormalizeKey(name);

    auto it = m_textureCache.find(key);
    if (it != m_textureCache.end())
    {
        return it->second;
    }

    // A GPU-local (default heap) resource cannot be written by the CPU, so
    // the data takes two hops:
    //   CPU memory -> upload heap (CPU-writable) -> default heap
    // The second hop is a GPU copy command: record, submit, wait.
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = image.width;
    texDesc.Height           = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1; // no mipmaps yet
    texDesc.Format           = SwapChain::kFormat;
    texDesc.SampleDesc.Count = 1;

    Texture texture;
    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST,
                      nullptr, IID_PPV_ARGS(&texture.resource)),
                  "CreateCommittedResource(Texture)");

    // Textures are NOT tightly packed: each row must start on a 256-byte
    // boundary. GetCopyableFootprints computes the exact layout.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows        = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 uploadSize     = 0;
    m_device.Device()->GetCopyableFootprints(&texDesc, 0, 1, 0,
                                             &footprint, &numRows, &rowSizeInBytes,
                                             &uploadSize);

    ComPtr<ID3D12Resource> uploadBuffer =
        CreateUploadBuffer(m_device.Device(), nullptr, uploadSize,
                           "CreateCommittedResource(TexUpload)");

    // Copy ROW BY ROW: the source is tightly packed, the destination has
    // padding at the end of every row.
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = {};
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
                  "TexUpload Map");
    for (UINT y = 0; y < numRows; ++y)
    {
        memcpy(mapped + footprint.Offset + SIZE_T(y) * footprint.Footprint.RowPitch,
               image.pixels.data() + SIZE_T(y) * image.width * 4,
               static_cast<size_t>(rowSizeInBytes));
    }
    uploadBuffer->Unmap(0, nullptr);

    BeginUpload();

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource        = texture.resource.Get();
    dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource       = uploadBuffer.Get();
    src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    m_uploadCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Copy done -> the texture becomes readable by the pixel shader.
    D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
        texture.resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_uploadCommandList->ResourceBarrier(1, &toShaderResource);

    EndUpload(); // submits and waits, so uploadBuffer may die below

    // Each texture gets its own slot, so a draw can point the root table at
    // whichever one its material names.
    texture.srv = m_srvAllocator.Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = SwapChain::kFormat;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = 1;
    m_device.Device()->CreateShaderResourceView(texture.resource.Get(), &srvDesc,
                                                texture.srv.cpu);

    const TextureHandle handle{ uint32_t(m_textures.size()) };
    m_textures.push_back(std::move(texture));
    m_textureNames.push_back(key);
    m_textureCache.emplace(key, handle);
    ++m_stats.textureLoads;
    return handle;
}

void ResourceManager::TextureSize(TextureHandle handle, UINT& outWidth, UINT& outHeight) const
{
    outWidth  = 0;
    outHeight = 0;
    if (!handle.IsValid() || handle.index >= m_textures.size())
    {
        return;
    }
    // The resource remembers its own dimensions, so nothing has to be
    // cached alongside the handle.
    const D3D12_RESOURCE_DESC desc = m_textures[handle.index].resource->GetDesc();
    outWidth  = UINT(desc.Width);
    outHeight = desc.Height;
}

// ------------------------------------------------------------ cube textures

// The order is fixed by D3D12: subresource 0 is +X, 1 is -X, and so on.
// Naming the files after the axes rather than "left/right" keeps the mapping
// mechanical - a face called "left" invites an argument about whose left.
const wchar_t* const ResourceManager::kCubeFaceSuffixes[6] = {
    L"px", L"nx", L"py", L"ny", L"pz", L"nz"
};

CubeTextureHandle ResourceManager::LoadCubeTexture(const std::wstring& name)
{
    const std::wstring key = NormalizeKey(name);

    auto it = m_cubeTextureCache.find(key);
    if (it != m_cubeTextureCache.end())
    {
        return it->second;
    }

    // Decode all six BEFORE creating anything on the GPU, so a missing or
    // mismatched face fails without leaving a half-built resource behind.
    ImageData faces[6];
    const std::filesystem::path directory = m_paths.SkyboxDir() / key;
    for (int face = 0; face < 6; ++face)
    {
        const std::filesystem::path facePath =
            directory / (std::wstring(kCubeFaceSuffixes[face]) + L".png");
        faces[face] = LoadImageRGBA(facePath);

        if (faces[face].width != faces[face].height)
        {
            throw std::runtime_error("Cube map face is not square:\n" +
                                     ToUtf8(facePath.wstring()));
        }
        if (faces[face].width != faces[0].width)
        {
            throw std::runtime_error("Cube map faces differ in size:\n" +
                                     ToUtf8(facePath.wstring()));
        }
    }

    const UINT edge = faces[0].width;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    // A cube map is an array of six 2D slices. Nothing about the RESOURCE
    // says "cube" - that is entirely the view's doing, below.
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = edge;
    texDesc.Height           = edge;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels        = 1;
    texDesc.Format           = SwapChain::kFormat;
    texDesc.SampleDesc.Count = 1;

    Texture texture;
    ThrowIfFailed(m_device.Device()->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST,
                      nullptr, IID_PPV_ARGS(&texture.resource)),
                  "CreateCommittedResource(CubeTexture)");

    // Six subresources in one upload buffer. GetCopyableFootprints lays them
    // out with the alignment the copy engine wants, which is why the offsets
    // come from it rather than from edge * edge * 4.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[6] = {};
    UINT   rowCounts[6]     = {};
    UINT64 rowSizes[6]      = {};
    UINT64 uploadSize       = 0;
    m_device.Device()->GetCopyableFootprints(&texDesc, 0, 6, 0,
                                             footprints, rowCounts, rowSizes,
                                             &uploadSize);

    ComPtr<ID3D12Resource> uploadBuffer =
        CreateUploadBuffer(m_device.Device(), nullptr, uploadSize,
                           "CreateCommittedResource(CubeUpload)");

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = {};
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
                  "CubeUpload Map");
    for (int face = 0; face < 6; ++face)
    {
        for (UINT y = 0; y < rowCounts[face]; ++y)
        {
            memcpy(mapped + footprints[face].Offset
                          + SIZE_T(y) * footprints[face].Footprint.RowPitch,
                   faces[face].pixels.data() + SIZE_T(y) * edge * 4,
                   static_cast<size_t>(rowSizes[face]));
        }
    }
    uploadBuffer->Unmap(0, nullptr);

    BeginUpload();
    for (int face = 0; face < 6; ++face)
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = texture.resource.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = UINT(face);

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource       = uploadBuffer.Get();
        src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprints[face];

        m_uploadCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    D3D12_RESOURCE_BARRIER toShaderResource = TransitionBarrier(
        texture.resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_uploadCommandList->ResourceBarrier(1, &toShaderResource);

    EndUpload(); // submits and waits, so uploadBuffer may die below

    texture.srv = m_srvAllocator.Allocate();

    // THIS is what makes it a cube map. The same six slices viewed as
    // TEXTURE2DARRAY would be sampled by index; as TEXTURECUBE they are
    // sampled by direction.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                    = SwapChain::kFormat;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels     = 1;
    m_device.Device()->CreateShaderResourceView(texture.resource.Get(), &srvDesc,
                                                texture.srv.cpu);

    const CubeTextureHandle handle{ uint32_t(m_cubeTextures.size()) };
    m_cubeTextures.push_back(std::move(texture));
    m_cubeTextureNames.push_back(key);
    m_cubeTextureCache.emplace(key, handle);
    return handle;
}

CubeTextureHandle ResourceManager::FindCubeTexture(const std::wstring& name) const
{
    auto it = m_cubeTextureCache.find(NormalizeKey(name));
    return it == m_cubeTextureCache.end() ? CubeTextureHandle{} : it->second;
}

DescriptorHandle ResourceManager::CubeTextureSRV(CubeTextureHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_cubeTextures.size())
    {
        throw std::runtime_error("CubeTextureSRV called with an invalid handle");
    }
    return m_cubeTextures[handle.index].srv;
}

const std::wstring& ResourceManager::CubeTextureName(CubeTextureHandle handle) const
{
    static const std::wstring kNone;
    if (!handle.IsValid() || handle.index >= m_cubeTextureNames.size())
    {
        return kNone;
    }
    return m_cubeTextureNames[handle.index];
}

DescriptorHandle ResourceManager::TextureSRV(TextureHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_textures.size())
    {
        throw std::runtime_error("TextureSRV called with an invalid handle");
    }
    return m_textures[handle.index].srv;
}

// ---------------------------------------------------------------- shaders

const ShaderBytecode& ResourceManager::LoadShader(
    const std::filesystem::path& logicalPath)
{
    ++m_stats.shaderRequests;

    const std::filesystem::path normalized = logicalPath.lexically_normal();
    if (normalized.empty() || normalized.is_absolute() ||
        *normalized.begin() == std::filesystem::path(L".."))
    {
        throw std::runtime_error("Shader path must be relative to the runtime shader root: " +
                                 ToUtf8(logicalPath.wstring()));
    }

    const std::string key = ToUtf8(normalized.generic_wstring());
    auto it = m_shaderCache.find(key);
    if (it != m_shaderCache.end())
    {
        return it->second;
    }

    const std::filesystem::path path = m_paths.shaderDir / normalized;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Compiled shader not found:\n" +
                                 ToUtf8(path.wstring()) +
                                 "\nRuntime root: " + ToUtf8(m_paths.root.wstring()));
    }

    const std::streamoff length = file.tellg();
    if (length <= 0)
    {
        throw std::runtime_error("Compiled shader is empty:\n" + ToUtf8(path.wstring()));
    }

    ShaderBytecode bytecode;
    bytecode.bytes.resize(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytecode.bytes.data()), length))
    {
        throw std::runtime_error("Could not read compiled shader:\n" +
                                 ToUtf8(path.wstring()));
    }

    auto inserted = m_shaderCache.emplace(key, std::move(bytecode));
    ++m_stats.shaderLoads;
    return inserted.first->second;
}
