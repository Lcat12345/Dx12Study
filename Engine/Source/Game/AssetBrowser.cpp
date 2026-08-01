#include "Game/AssetBrowser.h"

#include "Core/Common.h"
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
    // Paths are wide (a Korean folder name has to survive the round trip);
    // ImGui speaks UTF-8. This is the only place the two meet.
    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                                             nullptr, 0, nullptr, nullptr);
        std::string result(size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()),
                            result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToLowerExtension(const std::filesystem::path& path)
    {
        std::wstring extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t c) { return wchar_t(::towlower(c)); });
        return extension;
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

    std::error_code error;
    for (const auto& item : std::filesystem::directory_iterator(GetAssetDir(), error))
    {
        if (error || !item.is_regular_file() || ToLowerExtension(item.path()) != extension)
        {
            continue;
        }

        Entry entry;
        entry.fileName = item.path().filename().wstring();
        entry.label    = ToUtf8(entry.fileName);
        entry.byteSize = item.file_size(error);

        auto it = std::find_if(previous.begin(), previous.end(),
                               [&](const Entry& old) { return old.fileName == entry.fileName; });
        if (it != previous.end())
        {
            entry = *it;
            entry.byteSize = item.file_size(error);
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
        }
        out.push_back(std::move(entry));
    }

    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.fileName < b.fileName; });
}

void AssetBrowser::Refresh()
{
    Scan(m_meshes,   L".obj");
    Scan(m_textures, L".png");

    // Indices point into lists that were just rebuilt. The handles they had
    // resolved to survive inside Scan, so nothing is reloaded.
    m_selectedMesh    = -1;
    m_selectedTexture = -1;
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
}

// Returns true when something was clicked this frame.
bool AssetBrowser::DrawList(const char* id, std::vector<Entry>& entries,
                            int& selected, bool meshes)
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
        const bool loaded = meshes ? entry.mesh.IsValid() : entry.texture.IsValid();
        if (ImGui::Selectable(entry.label.c_str(), selected == i))
        {
            selected = i;
            clicked  = true;
            // Load on SELECTION, not on scan. Listing a folder must stay
            // instant however many models are in it.
            if (meshes) { LoadMeshEntry(entry); } else { LoadTextureEntry(entry); }
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

    if (!entry)
    {
        ImGui::TextDisabled("Select an asset.");
        return;
    }

    char size[32];
    FormatSize(entry->byteSize, size, sizeof(size));
    ImGui::Text("%s", entry->label.c_str());
    ImGui::TextDisabled("%s", size);

    const bool loaded = isMesh ? entry->mesh.IsValid() : entry->texture.IsValid();
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

    // Textures are free to preview: the SRV the renderer samples is the same
    // one ImGui draws with.
    const D3D12_GPU_DESCRIPTOR_HANDLE srv = m_resources.TextureSRV(entry->texture).gpu;
    const float width = std::min(ImGui::GetContentRegionAvail().x, 128.0f);
    ImGui::Image(ImTextureID(srv.ptr), ImVec2(width, width));
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
    ImGui::TextDisabled("%zu obj, %zu png", m_meshes.size(), m_textures.size());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", m_assetDirLabel.c_str());
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (DrawList("mesh", m_meshes, m_selectedMesh, /*meshes*/ true))
        {
            m_focus = Focus::Mesh;
        }
    }

    if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (DrawList("tex", m_textures, m_selectedTexture, /*meshes*/ false))
        {
            m_focus = Focus::Texture;
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
