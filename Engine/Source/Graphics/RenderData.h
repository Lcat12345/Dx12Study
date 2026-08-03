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
    enum class BlendMode
    {
        Opaque,
        AlphaBlend
    };

    DirectX::XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 specularColor = { 0.3f, 0.3f, 0.3f };
    float             shininess     = 32.0f; // high = small tight highlight
    TextureHandle     texture;

    // Per-pixel surface direction. Invalid means "use the flat default", so
    // an untouched material looks exactly as it did before 11.3.
    TextureHandle     normalTexture;
    // How far to believe the map. 0 ignores it entirely (useful for an A/B
    // without unassigning the texture), 1 is the map as authored, and above 1
    // exaggerates - handy for seeing whether a subtle map is working at all.
    float             normalStrength = 1.0f;
    // Explicit rather than inferred from diffuseAlbedo.a: render state must
    // not change just because an opaque material happens to store alpha.
    BlendMode         blendMode = BlendMode::Opaque;
};

enum class RenderLayer
{
    Opaque,
    Transparent
};

// One draw call's worth of input. The world matrix arrives already built -
// the renderer never sees a Transform.
struct DrawItem
{
    MeshHandle          mesh;
    // Which slice of the mesh's index buffer this call draws. A mesh with
    // several materials produces one DrawItem per range - the buffers are
    // shared, only the draw is split.
    UINT                indexOffset = 0;
    UINT                indexCount  = 0;
    DirectX::XMFLOAT4X4 world;
    Material            material;
    // The mesh AABB centre transformed to world space. Transparent sorting
    // uses this instead of entity order or Euclidean distance.
    DirectX::XMFLOAT3   worldBoundsCenter = { 0.0f, 0.0f, 0.0f };
    RenderLayer         layer = RenderLayer::Opaque;
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

    // The background. It travels with the lighting because both come from
    // the scene's Environment - a skybox is a property of the world, not an
    // object sitting in it, so there is no entity and no Transform.
    // Invalid means "no skybox": the viewport keeps its clear colour.
    CubeTextureHandle skybox;

    DirectX::XMFLOAT3 directionalDirection = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT3 directionalColor     = { 0.0f, 0.0f, 0.0f };

    // Directional-shadow controls flattened from the Environment. Bias is
    // in the light projection's [0,1] depth range; strength blends between
    // fully lit and the 3x3 comparison result.
    bool  shadowsEnabled = true;
    float shadowBias     = 0.001f;
    float shadowStrength = 1.0f;

    DirectX::XMFLOAT3 pointPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 pointColor    = { 0.0f, 0.0f, 0.0f };
    float             pointRange    = 1.0f;
};
