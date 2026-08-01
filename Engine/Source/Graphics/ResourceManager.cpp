#include "Graphics/ResourceManager.h"

#include "Core/Common.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Image.h"
#include "Graphics/SwapChain.h"
#include "Loaders/ObjLoader.h"

#include <d3dcompiler.h>
#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

ResourceManager::ResourceManager(GraphicsDevice& device, DescriptorAllocator& srvAllocator)
    : m_device(device)
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

// ---------------------------------------------------------------- meshes

MeshHandle ResourceManager::AddMesh(const std::wstring& name, const MeshData& data)
{
    ++m_stats.meshRequests;

    auto it = m_meshCache.find(name);
    if (it != m_meshCache.end())
    {
        return it->second;
    }

    const MeshHandle handle{ uint32_t(m_meshes.size()) };
    m_meshes.push_back(CreateMesh(m_device.Device(), data));
    m_meshCache.emplace(name, handle);
    ++m_stats.meshLoads;
    return handle;
}

MeshHandle ResourceManager::LoadMesh(const std::wstring& fileName, float fitToSize)
{
    ++m_stats.meshRequests;

    auto it = m_meshCache.find(fileName);
    if (it != m_meshCache.end())
    {
        return it->second; // already on the GPU - no file read, no upload
    }

    MeshData data = LoadObj(GetAssetDir() / fileName);
    if (fitToSize > 0.0f)
    {
        FitMeshToSize(data, fitToSize);
    }

    const MeshHandle handle{ uint32_t(m_meshes.size()) };
    m_meshes.push_back(CreateMesh(m_device.Device(), data));
    m_meshCache.emplace(fileName, handle);
    ++m_stats.meshLoads;
    return handle;
}

const Mesh& ResourceManager::GetMesh(MeshHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_meshes.size())
    {
        throw std::runtime_error("GetMesh called with an invalid handle");
    }
    return m_meshes[handle.index];
}

MeshHandle ResourceManager::FindMesh(const std::wstring& name) const
{
    // Not a request: asking does no work and must not skew the cache stats.
    auto it = m_meshCache.find(name);
    return it == m_meshCache.end() ? MeshHandle{} : it->second;
}

TextureHandle ResourceManager::FindTexture(const std::wstring& name) const
{
    auto it = m_textureCache.find(name);
    return it == m_textureCache.end() ? TextureHandle{} : it->second;
}

// ---------------------------------------------------------------- textures

TextureHandle ResourceManager::LoadTexture(const std::wstring& fileName)
{
    auto it = m_textureCache.find(fileName);
    if (it != m_textureCache.end())
    {
        // The whole point of the cache: five materials naming the same
        // image decode it once and share one SRV slot.
        ++m_stats.textureRequests;
        return it->second;
    }
    // Decoding is the only file-specific part; everything after it is the
    // same upload whether the pixels came from disk or from code.
    return AddTexture(fileName, LoadImageRGBA(GetAssetDir() / fileName));
}

TextureHandle ResourceManager::AddTexture(const std::wstring& name, const ImageData& image)
{
    ++m_stats.textureRequests;

    auto it = m_textureCache.find(name);
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
    m_textureCache.emplace(name, handle);
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

DescriptorHandle ResourceManager::TextureSRV(TextureHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_textures.size())
    {
        throw std::runtime_error("TextureSRV called with an invalid handle");
    }
    return m_textures[handle.index].srv;
}

// ---------------------------------------------------------------- shaders

ComPtr<ID3DBlob> ResourceManager::LoadShader(const std::filesystem::path& path,
                                             const char* entryPoint, const char* target)
{
    ++m_stats.shaderRequests;

    // One .hlsl holds several entry points, so the key has to include them.
    const std::string key = path.string() + "|" + entryPoint + "|" + target;
    auto it = m_shaderCache.find(key);
    if (it != m_shaderCache.end())
    {
        return it->second;
    }

    // D3DCompileFromFile only reports a bare HRESULT for a missing file,
    // which is painful to diagnose - check first and name the path.
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Shader file not found:\n" + path.string());
    }

    UINT flags = 0;
#if defined(_DEBUG)
    // Embed debug info and keep the HLSL readable in PIX/RenderDoc.
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr,
                                    D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entryPoint, target, flags, 0,
                                    &bytecode, &errors);
    if (FAILED(hr))
    {
        // The compiler writes human-readable errors into the blob.
        if (errors)
        {
            throw std::runtime_error(
                static_cast<const char*>(errors->GetBufferPointer()));
        }
        ThrowIfFailed(hr, "D3DCompileFromFile");
    }

    m_shaderCache.emplace(key, bytecode);
    ++m_stats.shaderCompiles;
    return bytecode;
}
