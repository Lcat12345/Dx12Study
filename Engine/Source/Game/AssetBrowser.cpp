#include "Game/AssetBrowser.h"

#include "Core/Common.h"
#include "Core/TextEncoding.h"
#include "Graphics/Mesh.h"

#include "imgui.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <exception>
#include <filesystem>

namespace
{
    std::wstring ToLowerExtension(const std::filesystem::path& path)
    {
        std::wstring extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t c) { return wchar_t(::towlower(c)); });
        return extension;
    }

    // Zero when the size cannot be read, rather than the uintmax_t(-1) that
    // file_size reports on failure.
    std::uintmax_t QueryFileSize(const std::filesystem::directory_entry& item)
    {
        std::error_code error;
        const std::uintmax_t size = item.file_size(error);
        return error ? 0 : size;
    }

    void FormatSize(std::uintmax_t bytes, char* out, size_t size)
    {
        if (bytes >= 1024 * 1024)
        {
            std::snprintf(out, size, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
        }
        else if (bytes >= 1024)
        {
            std::snprintf(out, size, "%.1f KB", double(bytes) / 1024.0);
        }
        else
        {
            std::snprintf(out, size, "%llu B", static_cast<unsigned long long>(bytes));
        }
    }
}

AssetBrowser::AssetBrowser(ResourceManager& resources)
    : m_resources(resources)
{
    m_assetDirLabel = ToUtf8(GetAssetDir().wstring());
    Refresh();
}

void AssetBrowser::Scan(std::vector<Entry>& out, const wchar_t* extension)
{
    // Remember what was already loaded so a refresh does not throw away
    // handles - the file list is rebuilt, the loaded assets are not.
    std::vector<Entry> previous = std::move(out);
    out.clear();

    // Opening the directory is the only failure worth giving up on. A
    // missing Assets/ means an empty list, not a crash.
    //
    // RECURSIVE: a downloaded model arrives as a folder - the .obj, its .mtl
    // and the textures they reference only make sense together - so assets
    // are no longer all at the top level.
    const std::filesystem::path root = GetAssetDir();
    std::error_code openError;
    std::filesystem::recursive_directory_iterator directory(root, openError);
    if (openError)
    {
        return;
    }

    for (const auto& item : directory)
    {
        // A FRESH error_code per item. Sharing one across the loop means a
        // single unreadable file latches the flag and every later file gets
        // skipped as if it did not exist.
        std::error_code itemError;

        // Skyboxes are six faces that mean nothing individually; they have
        // their own list. Scenes are not assets at all.
        const std::wstring relative =
            std::filesystem::relative(item.path(), root, itemError).wstring();
        if (itemError || relative.rfind(L"Skyboxes", 0) == 0 ||
                         relative.rfind(L"Scenes", 0) == 0)
        {
            continue;
        }

        if (!item.is_regular_file(itemError) || itemError)
        {
            continue;
        }
        if (ToLowerExtension(item.path()) != extension)
        {
            continue;
        }

        Entry entry;
        // The path RELATIVE TO Assets/, which is exactly the string the
        // ResourceManager keys its cache on. A bare filename would collide
        // the moment two folders each hold a "model.obj".
        entry.fileName = relative;
        entry.label    = ToUtf8(entry.fileName);
        // file_size returns uintmax_t(-1) on failure, which would print as a
        // nonsense size. Zero reads as "unknown" instead.
        entry.byteSize = QueryFileSize(item);

        auto it = std::find_if(previous.begin(), previous.end(),
                               [&](const Entry& old) { return old.fileName == entry.fileName; });
        if (it != previous.end())
        {
            const std::uintmax_t byteSize = entry.byteSize;
            entry = *it;
            entry.byteSize = byteSize;
        }
        else
        {
            // The startup scene loaded some of these before this browser
            // existed. Adopt the cached handles rather than reporting them
            // as unloaded and reloading on the first click.
            entry.mesh    = m_resources.FindMesh(entry.fileName);
            entry.texture = m_resources.FindTexture(entry.fileName);
            if (entry.mesh.IsValid())
            {
                const Mesh& mesh  = m_resources.GetMesh(entry.mesh);
                entry.vertexCount = mesh.vertexCount;
                entry.indexCount  = mesh.indexCount;
            }
            if (entry.texture.IsValid())
            {
                m_resources.TextureSize(entry.texture, entry.width, entry.height);
            }
        }
        out.push_back(std::move(entry));
    }

    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.fileName < b.fileName; });
}

// A skybox is a folder, not a file. It counts only when all six faces are
// present - showing a half-populated one as selectable would only produce a
// load error on click.
void AssetBrowser::ScanSkyboxes()
{
    std::vector<Entry> previous = std::move(m_skyboxes);
    m_skyboxes.clear();

    std::error_code openError;
    std::filesystem::directory_iterator directory(GetSkyboxDir(), openError);
    if (openError)
    {
        return; // no Skyboxes folder yet
    }

    for (const auto& item : directory)
    {
        std::error_code itemError;
        if (!item.is_directory(itemError) || itemError)
        {
            continue;
        }

        bool           complete = true;
        std::uintmax_t bytes    = 0;
        for (const wchar_t* const face : ResourceManager::kCubeFaceSuffixes)
        {
            const std::filesystem::path facePath =
                item.path() / (std::wstring(face) + L".png");
            std::error_code faceError;
            if (!std::filesystem::is_regular_file(facePath, faceError) || faceError)
            {
                complete = false;
                break;
            }
            bytes += std::filesystem::file_size(facePath, faceError);
        }
        if (!complete)
        {
            continue;
        }

        Entry entry;
        entry.fileName = item.path().filename().wstring();
        entry.label    = ToUtf8(entry.fileName);
        entry.byteSize = bytes;

        auto it = std::find_if(previous.begin(), previous.end(),
                               [&](const Entry& old) { return old.fileName == entry.fileName; });
        if (it != previous.end())
        {
            const std::uintmax_t size = entry.byteSize;
            entry = *it;
            entry.byteSize = size;
        }
        else
        {
            entry.skybox = m_resources.FindCubeTexture(entry.fileName);
        }
        m_skyboxes.push_back(std::move(entry));
    }

    std::sort(m_skyboxes.begin(), m_skyboxes.end(),
              [](const Entry& a, const Entry& b) { return a.fileName < b.fileName; });
}

void AssetBrowser::LoadSkyboxEntry(Entry& entry)
{
    if (entry.skybox.IsValid())
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    try
    {
        entry.skybox = m_resources.LoadCubeTexture(entry.fileName);
    }
    catch (const std::exception&)
    {
        return; // mismatched or unreadable faces - the entry stays unloaded
    }
    const auto finish = std::chrono::steady_clock::now();

    entry.loadMilliseconds =
        std::chrono::duration<double, std::milli>(finish - start).count();
}

void AssetBrowser::Refresh()
{
    Scan(m_meshes,   L".obj");
    Scan(m_textures, L".png");
    ScanSkyboxes();

    // Indices point into lists that were just rebuilt. The handles they had
    // resolved to survive inside Scan, so nothing is reloaded.
    m_selectedMesh    = -1;
    m_selectedTexture = -1;
    m_selectedSkybox  = -1;
    m_focus           = Focus::None;
}

void AssetBrowser::LoadMeshEntry(Entry& entry)
{
    if (entry.mesh.IsValid())
    {
        return;
    }

    // Synchronous, on the UI thread. A big .obj freezes the editor for as
    // long as it takes - measured and shown rather than hidden, because the
    // fix (a loading thread) is a whole subject of its own.
    const auto start = std::chrono::steady_clock::now();
    try
    {
        entry.mesh = m_resources.LoadMesh(entry.fileName);
    }
    catch (const std::exception&)
    {
        // A malformed file must not take the editor down with it. The entry
        // simply stays unloaded and says so.
        return;
    }
    const auto finish = std::chrono::steady_clock::now();

    entry.loadMilliseconds =
        std::chrono::duration<double, std::milli>(finish - start).count();

    const Mesh& mesh  = m_resources.GetMesh(entry.mesh);
    entry.vertexCount = mesh.vertexCount;
    entry.indexCount  = mesh.indexCount;
}

void AssetBrowser::LoadTextureEntry(Entry& entry)
{
    if (entry.texture.IsValid())
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    try
    {
        entry.texture = m_resources.LoadTexture(entry.fileName);
    }
    catch (const std::exception&)
    {
        return;
    }
    const auto finish = std::chrono::steady_clock::now();

    entry.loadMilliseconds =
        std::chrono::duration<double, std::milli>(finish - start).count();

    m_resources.TextureSize(entry.texture, entry.width, entry.height);
}

// Returns true when something was clicked this frame.
bool AssetBrowser::DrawList(const char* id, std::vector<Entry>& entries,
                            int& selected, ListKind kind)
{
    if (entries.empty())
    {
        ImGui::TextDisabled("  (none)");
        return false;
    }

    bool clicked = false;
    for (int i = 0; i < int(entries.size()); ++i)
    {
        Entry& entry = entries[i];
        ImGui::PushID(id);
        ImGui::PushID(i);

        // A dot marks what is already on the GPU, so the cost of a click is
        // predictable before making it.
        const bool loaded = kind == ListKind::Mesh    ? entry.mesh.IsValid()
                          : kind == ListKind::Texture ? entry.texture.IsValid()
                                                      : entry.skybox.IsValid();
        if (ImGui::Selectable(entry.label.c_str(), selected == i))
        {
            selected = i;
            clicked  = true;
            // Load on SELECTION, not on scan. Listing a folder must stay
            // instant however many models are in it.
            switch (kind)
            {
            case ListKind::Mesh:    LoadMeshEntry(entry);    break;
            case ListKind::Texture: LoadTextureEntry(entry); break;
            case ListKind::Skybox:  LoadSkyboxEntry(entry);  break;
            }
        }
        if (loaded)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("*");
        }

        ImGui::PopID();
        ImGui::PopID();
    }
    return clicked;
}

void AssetBrowser::DrawDetails()
{
    // The details pane shows the LAST thing clicked. Both selections stay
    // live - the inspector assigns a mesh and a texture in either order, so
    // clicking one must not drop the other.
    const Entry* entry  = nullptr;
    const bool   isMesh = m_focus == Focus::Mesh;

    if (m_focus == Focus::Mesh && m_selectedMesh >= 0)
    {
        entry = &m_meshes[size_t(m_selectedMesh)];
    }
    else if (m_focus == Focus::Texture && m_selectedTexture >= 0)
    {
        entry = &m_textures[size_t(m_selectedTexture)];
    }
    else if (m_focus == Focus::Skybox && m_selectedSkybox >= 0)
    {
        entry = &m_skyboxes[size_t(m_selectedSkybox)];
    }

    if (!entry)
    {
        ImGui::TextDisabled("Select an asset.");
        return;
    }

    char size[32];
    FormatSize(entry->byteSize, size, sizeof(size));
    ImGui::Text("%s", entry->label.c_str());
    ImGui::TextDisabled("%s", size);

    const bool loaded = m_focus == Focus::Mesh    ? entry->mesh.IsValid()
                      : m_focus == Focus::Texture ? entry->texture.IsValid()
                                                  : entry->skybox.IsValid();
    if (!loaded)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "failed to load");
        return;
    }

    if (entry->loadMilliseconds > 0.0)
    {
        ImGui::Text("loaded in %.1f ms", entry->loadMilliseconds);
    }
    else
    {
        ImGui::TextDisabled("already loaded");
    }

    if (isMesh)
    {
        // Vertices AFTER deduplication: an .obj lists position, uv and
        // normal in separate index spaces, and the loader merges them into
        // the single stream the input assembler wants.
        ImGui::Text("%u vertices, %u triangles",
                    entry->vertexCount, entry->indexCount / 3);
        ImGui::TextDisabled("No 3D thumbnail - that needs a render target per\n"
                            "asset, which waits for a reason to exist.");
        return;
    }

    if (m_focus == Focus::Skybox)
    {
        // No preview: showing a cube map flat means picking a projection,
        // and the six faces are already the answer to "what is in it".
        ImGui::Text("6 faces (px nx py ny pz nz)");
        ImGui::TextDisabled("Assign in the Inspector's Environment section.");
        return;
    }

    ImGui::Text("%u x %u", entry->width, entry->height);

    // Textures are free to preview: the SRV the renderer samples is the same
    // one ImGui draws with.
    const D3D12_GPU_DESCRIPTOR_HANDLE srv = m_resources.TextureSRV(entry->texture).gpu;

    // Fit inside a square box while KEEPING the aspect ratio - drawing every
    // image square would squash anything that is not.
    const float box = std::min(ImGui::GetContentRegionAvail().x, 128.0f);
    ImVec2 preview(box, box);
    if (entry->width > 0 && entry->height > 0)
    {
        const float aspect = float(entry->width) / float(entry->height);
        if (aspect >= 1.0f) { preview.y = box / aspect; } // wider than tall
        else                { preview.x = box * aspect; }
    }
    ImGui::Image(ImTextureID(srv.ptr), preview);
}

void AssetBrowser::Draw()
{
    // Under the entity list, still inside a 720-tall window on first run.
    ImGui::SetNextWindowPos(ImVec2(20, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 240), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Assets"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Refresh"))
    {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu obj, %zu png, %zu sky",
                        m_meshes.size(), m_textures.size(), m_skyboxes.size());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", m_assetDirLabel.c_str());
    }

    ImGui::BeginDisabled(!SelectedMesh().IsValid());
    ImGui::Checkbox("Place on click", &m_placeOnClick);
    ImGui::EndDisabled();
    if (PlaceOnClick())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("- click the floor");
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (DrawList("mesh", m_meshes, m_selectedMesh, ListKind::Mesh))
        {
            m_focus = Focus::Mesh;
        }
    }

    if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (DrawList("tex", m_textures, m_selectedTexture, ListKind::Texture))
        {
            m_focus = Focus::Texture;
        }
    }

    if (ImGui::CollapsingHeader("Skyboxes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (DrawList("sky", m_skyboxes, m_selectedSkybox, ListKind::Skybox))
        {
            m_focus = Focus::Skybox;
        }
    }

    ImGui::Separator();
    DrawDetails();

    ImGui::End();
}

MeshHandle AssetBrowser::SelectedMesh() const
{
    return m_selectedMesh >= 0 ? m_meshes[size_t(m_selectedMesh)].mesh : MeshHandle{};
}

TextureHandle AssetBrowser::SelectedTexture() const
{
    return m_selectedTexture >= 0 ? m_textures[size_t(m_selectedTexture)].texture
                                  : TextureHandle{};
}

const char* AssetBrowser::SelectedMeshLabel() const
{
    return m_selectedMesh >= 0 ? m_meshes[size_t(m_selectedMesh)].label.c_str() : "none";
}

const char* AssetBrowser::SelectedTextureLabel() const
{
    return m_selectedTexture >= 0 ? m_textures[size_t(m_selectedTexture)].label.c_str()
                                  : "none";
}

CubeTextureHandle AssetBrowser::SelectedSkybox() const
{
    return m_selectedSkybox >= 0 ? m_skyboxes[size_t(m_selectedSkybox)].skybox
                                 : CubeTextureHandle{};
}

const char* AssetBrowser::SelectedSkyboxLabel() const
{
    return m_selectedSkybox >= 0 ? m_skyboxes[size_t(m_selectedSkybox)].label.c_str()
                                 : "none";
}
