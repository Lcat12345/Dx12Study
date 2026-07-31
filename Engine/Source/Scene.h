// Scene.h : what is in the world, independent of how it gets drawn.
#pragma once

#include "Mesh.h"

#include <DirectXMath.h>
#include <vector>

// How a surface responds to light. Uploaded in the per-object constants.
struct Material
{
    DirectX::XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 specularColor = { 0.3f, 0.3f, 0.3f };
    float             shininess     = 32.0f; // high = small tight highlight
};

struct SceneObject
{
    // Index into Scene::meshes, not a pointer: an index survives the vector
    // reallocating or the whole Scene being moved.
    size_t            meshIndex = 0;
    DirectX::XMFLOAT3 position  = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale     = { 1.0f, 1.0f, 1.0f };
    float             spinSpeed = 0.0f; // radians per second around Y
    Material          material;
};

struct Scene
{
    std::vector<Mesh>        meshes;
    std::vector<SceneObject> objects;
};

// Builds the meshes and lays out the demo scene.
void BuildScene(ID3D12Device* device, Scene& outScene);
