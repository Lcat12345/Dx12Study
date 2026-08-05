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
    MeshHandle        SelectedMesh() const;
    TextureHandle     SelectedTexture() const;
    CubeTextureHandle SelectedSkybox() const;
    const char*       SelectedMeshLabel() const;
    const char*       SelectedTextureLabel() const;
    const char*       SelectedSkyboxLabel() const;

    // Armed placement: while this is on, a click on the scene viewport drops
    // the selected mesh there. A mode rather than "always place when a mesh
    // is selected", so the viewport stays clickable for everything else -
    // picking an existing entity lands here next.
    bool PlaceOnClick() const { return m_placeOnClick && SelectedMesh().IsValid(); }

private:
    struct Entry
    {
        std::wstring   fileName; // what ResourceManager keys its cache on
        std::string    label;    // UTF-8, what ImGui draws
        std::uintmax_t byteSize = 0;

        // Filled in on first selection; invalid means "not loaded yet".
        MeshHandle        mesh;
        TextureHandle     texture;
        CubeTextureHandle skybox;

        // Only meaningful once loaded.
        UINT   vertexCount = 0;
        UINT   indexCount  = 0;
        UINT   width       = 0;
        UINT   height      = 0;
        double loadMilliseconds = 0.0;
    };

    void Scan(std::vector<Entry>& out, const wchar_t* extension);
    // A skybox is a DIRECTORY of six faces, so this walks folders rather
    // than files and reports each valid one as a single logical asset.
    void ScanSkyboxes();
    void LoadMeshEntry(Entry& entry);
    void LoadTextureEntry(Entry& entry);
    void LoadSkyboxEntry(Entry& entry);

    enum class ListKind { Mesh, Texture, Skybox };

    bool DrawList(const char* id, std::vector<Entry>& entries, int& selected,
                  ListKind kind);
    void DrawDetails();

    ResourceManager& m_resources;

    std::vector<Entry> m_meshes;
    std::vector<Entry> m_textures;
    std::vector<Entry> m_skyboxes;

    // Both stay live: the inspector assigns a mesh and a texture in either
    // order. Focus only decides which one the details pane describes.
    int m_selectedMesh    = -1;
    int m_selectedTexture = -1;
    int m_selectedSkybox  = -1;

    enum class Focus { None, Mesh, Texture, Skybox };
    Focus m_focus = Focus::None;

    bool m_placeOnClick = false;

    std::string m_assetDirLabel;
};
