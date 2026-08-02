// RenderData.h : the boundary between the game and the renderer.
//
// The renderer takes these three plain structures and nothing else - no
// World, no Entity, no components. That is what lets the Phase 10 editor
// drive the same renderer from a completely different scene representation.
#pragma once

#include "Graphics/ResourceManager.h"

#include <DirectXMath.h>

// How a surface responds to light. Everything except the texture is
// uploaded in the per-object constants; the texture is bound as a descriptor.
struct Material
{
    DirectX::XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 specularColor = { 0.3f, 0.3f, 0.3f };
    float             shininess     = 32.0f; // high = small tight highlight
    TextureHandle     texture;
};

// One draw call's worth of input. The world matrix arrives already built -
// the renderer never sees a Transform.
struct DrawItem
{
    MeshHandle          mesh;
    DirectX::XMFLOAT4X4 world;
    Material            material;
};

// The camera as the renderer needs it. Note what is NOT here: the aspect
// ratio. That belongs to whatever surface the scene lands on - since 10.1
// the offscreen render target - so the renderer builds the projection itself
// and resizing the viewport keeps working without the game's help.
struct CameraView
{
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 forward  = { 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 up       = { 0.0f, 1.0f, 0.0f };
    float             fovY     = DirectX::XM_PIDIV4;
    float             nearZ    = 0.1f;
    float             farZ     = 200.0f;
};

// Flattened lighting for one frame. Still one directional and one point
// light, but the values now come from entities instead of being hardcoded
// in the renderer.
struct LightingData
{
    DirectX::XMFLOAT3 ambient = { 0.18f, 0.19f, 0.22f };

    DirectX::XMFLOAT3 directionalDirection = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT3 directionalColor     = { 0.0f, 0.0f, 0.0f };

    DirectX::XMFLOAT3 pointPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 pointColor    = { 0.0f, 0.0f, 0.0f };
    float             pointRange    = 1.0f;
};
