// Components.h : pure data. No methods, no logic - that lives in systems.
#pragma once

#include "Graphics/RenderData.h"
#include "Graphics/ResourceManager.h"

#include <DirectXMath.h>

// Where something is. The world matrix is NOT stored here; systems compute
// it, so there is never a cached copy that can go stale.
struct Transform
{
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    // Euler angles in radians. Fine for this engine; a quaternion becomes
    // worth it once things start interpolating between orientations.
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale    = { 1.0f, 1.0f, 1.0f };
};

// Makes an entity visible.
struct MeshRenderer
{
    MeshHandle mesh;
    Material   material;
};

// Lens settings. The position and orientation come from the Transform on
// the same entity - a camera is just an entity that happens to have this.
struct CameraComponent
{
    float fovY  = DirectX::XM_PIDIV4;
    float nearZ = 0.1f;
    float farZ  = 200.0f;
};

struct Light
{
    enum class Type { Directional, Point };

    Type              type  = Type::Directional;
    DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    // Point lights only. Directional lights take their direction from the
    // owning Transform's rotation.
    float             range = 30.0f;
};

// Demo-only: constant rotation about Y. Exists to show that behaviour is a
// component you attach, not a field every object has to carry.
struct Spin
{
    float speed = 0.0f; // radians per second
};

// Marks the entity the camera systems should follow. A tag component -
// no data, just a type. This is how ECS expresses "the active one".
struct ActiveCamera
{
};

// Ambient light is a property of the scene rather than of any one light, so
// it hangs off whichever entity is tagged as the environment.
struct Environment
{
    DirectX::XMFLOAT3 ambient = { 0.18f, 0.19f, 0.22f };
};
