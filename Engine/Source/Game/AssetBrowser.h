// AssetBrowser.h : what is in Assets/, and what the editor has picked.
//
// Deliberately NOT part of ResourceManager. The manager owns loaded assets
// and knows nothing about directories; this walks the filesystem and calls
// Load only when something is actually chosen. The cache underneath means
// choosing the same file twice is free.
#pragma once

#include "Graphics/ResourceManager.h"

#include <cstdint>
#include <string>
#include <vector>

class AssetBrowser
{
public:
    explicit AssetBrowser(ResourceManager& resources);

    // Rescans Assets/. There is no file watcher - a button is enough, and
    // watching is a thread's worth of machinery for a problem we do not
    // have yet.
    void Refresh();

    // The panel.
    void Draw();

    // What the inspector may assign. Invalid until the entry has been
    // clicked at least once, because loading is deferred to selection.
    MeshHandle    SelectedMesh() const;
    TextureHandle SelectedTexture() const;
    const char*   SelectedMeshLabel() const;
    const char*   SelectedTextureLabel() const;

private:
    struct Entry
    {
        std::wstring   fileName; // what ResourceManager keys its cache on
        std::string    label;    // UTF-8, what ImGui draws
        std::uintmax_t byteSize = 0;

        // Filled in on first selection; invalid means "not loaded yet".
        MeshHandle    mesh;
        TextureHandle texture;

        // Only meaningful once loaded.
        UINT   vertexCount = 0;
        UINT   indexCount  = 0;
        UINT   width       = 0;
        UINT   height      = 0;
        double loadMilliseconds = 0.0;
    };

    void Scan(std::vector<Entry>& out, const wchar_t* extension);
    void LoadMeshEntry(Entry& entry);
    void LoadTextureEntry(Entry& entry);

    bool DrawList(const char* id, std::vector<Entry>& entries, int& selected,
                  bool meshes);
    void DrawDetails();

    ResourceManager& m_resources;

    std::vector<Entry> m_meshes;
    std::vector<Entry> m_textures;

    // Both stay live: the inspector assigns a mesh and a texture in either
    // order. Focus only decides which one the details pane describes.
    int m_selectedMesh    = -1;
    int m_selectedTexture = -1;

    enum class Focus { None, Mesh, Texture };
    Focus m_focus = Focus::None;

    std::string m_assetDirLabel;
};
