// Scene.h : what is in the world, independent of how it gets drawn.
#pragma once

#include "Graphics/ResourceManager.h"

#include <DirectXMath.h>
#include <vector>

// How a surface responds to light. Everything here except the texture is
// uploaded in the per-object constants; the texture is bound as a descriptor.
struct Material
{
    DirectX::XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 specularColor = { 0.3f, 0.3f, 0.3f };
    float             shininess     = 32.0f; // high = small tight highlight
    TextureHandle     texture;
};

struct SceneObject
{
    // Handles, not pointers: the Scene owns no GPU data any more, it only
    // names what the ResourceManager is holding.
    MeshHandle        mesh;
    DirectX::XMFLOAT3 position  = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale     = { 1.0f, 1.0f, 1.0f };
    float             spinSpeed = 0.0f; // radians per second around Y
    Material          material;
};

struct Scene
{
    std::vector<SceneObject> objects;
};

// Lays out the demo scene. Every asset goes through the manager, so naming
// the same file twice costs nothing the second time.
void BuildScene(ResourceManager& resources, Scene& outScene);
