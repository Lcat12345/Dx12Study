#include "Game/Systems.h"

#include "Game/Components.h"
#include "Game/Picking.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float kMoveSpeed      = 8.0f;   // units per second
    constexpr float kFastMultiplier = 3.0f;
    constexpr float kMouseSpeed     = 0.004f; // radians per pixel

    // Transform.rotation is (pitch, yaw, roll). Yaw 0 looks down +Z.
    XMVECTOR ForwardFrom(const Transform& transform)
    {
        const float pitch    = transform.rotation.x;
        const float yaw      = transform.rotation.y;
        const float cosPitch = std::cos(pitch);
        return XMVector3Normalize(XMVectorSet(cosPitch * std::sin(yaw),
                                              std::sin(pitch),
                                              cosPitch * std::cos(yaw),
                                              0.0f));
    }

    // "Iterate one component, look up the other" - the whole query story.
    // Finding the single tagged camera is the same pattern.
    Entity FindActiveCamera(World& world)
    {
        Entity found;
        world.ForEach<ActiveCamera>([&](Entity entity, ActiveCamera&) {
            if (!found.IsValid())
            {
                found = entity;
            }
        });
        return found;
    }

    CameraView CameraViewOf(const Transform& transform, const CameraComponent& lens)
    {
        CameraView camera;
        camera.position = transform.position;
        XMStoreFloat3(&camera.forward, ForwardFrom(transform));
        camera.up    = { 0.0f, 1.0f, 0.0f };
        camera.fovY  = lens.fovY;
        camera.nearZ = lens.nearZ;
        camera.farZ  = lens.farZ;
        return camera;
    }

    void ApplyCameraInput(Transform& transform, const InputContext& input, float dt,
                          bool allowVerticalMovement)
    {
        transform.rotation.y += input.mouseDeltaX * kMouseSpeed;
        transform.rotation.x -= input.mouseDeltaY * kMouseSpeed;

        constexpr float kPitchLimit = XM_PIDIV2 - 0.01f;
        transform.rotation.x = std::clamp(transform.rotation.x,
                                         -kPitchLimit, kPitchLimit);

        const XMVECTOR forward = ForwardFrom(transform);
        const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

        XMVECTOR move = XMVectorZero();
        if (input.IsDown('W')) move = XMVectorAdd(move, forward);
        if (input.IsDown('S')) move = XMVectorSubtract(move, forward);
        if (input.IsDown('D')) move = XMVectorAdd(move, right);
        if (input.IsDown('A')) move = XMVectorSubtract(move, right);
        if (allowVerticalMovement && input.IsDown('E'))
            move = XMVectorAdd(move, worldUp);
        if (allowVerticalMovement && input.IsDown('Q'))
            move = XMVectorSubtract(move, worldUp);

        if (XMVectorGetX(XMVector3LengthSq(move)) > 0.0f)
        {
            float speed = kMoveSpeed;
            if (input.IsDown(VK_SHIFT))
            {
                speed *= kFastMultiplier;
            }
            move = XMVectorScale(XMVector3Normalize(move), speed * dt);

            XMVECTOR position = XMLoadFloat3(&transform.position);
            XMStoreFloat3(&transform.position, XMVectorAdd(position, move));
        }
    }
}

XMMATRIX WorldMatrixOf(const Transform& t)
{
    return XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z) *
           XMMatrixRotationRollPitchYaw(t.rotation.x, t.rotation.y, t.rotation.z) *
           XMMatrixTranslation(t.position.x, t.position.y, t.position.z);
}

void SpinSystem(World& world, float dt)
{
    world.ForEach<Spin>([&](Entity entity, Spin& spin) {
        if (Transform* transform = world.Get<Transform>(entity))
        {
            transform->rotation.y += spin.speed * dt;
        }
    });
}

bool InitializeEditorCamera(World& world, EditorCamera& outCamera)
{
    const Entity camera = FindActiveCamera(world);
    const Transform* transform = world.Get<Transform>(camera);
    if (!transform)
    {
        return false;
    }

    outCamera.transform = *transform;
    if (const CameraComponent* lens = world.Get<CameraComponent>(camera))
    {
        outCamera.lens = *lens;
    }
    else
    {
        outCamera.lens = CameraComponent{};
    }
    return true;
}

CameraView GetEditorCameraView(const EditorCamera& camera)
{
    return CameraViewOf(camera.transform, camera.lens);
}

void GameCameraSystem(World& world, const InputContext& input, float dt)
{
    const Entity camera = FindActiveCamera(world);
    if (Transform* transform = world.Get<Transform>(camera))
    {
        ApplyCameraInput(*transform, input, dt, false);
    }
}

void LightOrbitSystem(World& world, float totalSeconds)
{
    world.ForEach<Light>([&](Entity entity, Light& light) {
        if (light.type != Light::Type::Point)
        {
            return;
        }
        Spin* spin = world.Get<Spin>(entity);
        Transform* transform = world.Get<Transform>(entity);
        if (!spin || !transform)
        {
            return;
        }
        // The orbit used to be a sine buried in the renderer. Now it is a
        // system acting on a light entity's Transform, so the renderer just
        // reads wherever the light ended up.
        constexpr float kRadius = 14.0f;
        const float angle = spin->speed * totalSeconds;
        transform->position.x = kRadius * std::cos(angle);
        transform->position.z = kRadius * std::sin(angle);
    });
}

bool GetActiveCameraView(World& world, CameraView& outCamera)
{
    const Entity cameraEntity = FindActiveCamera(world);
    const Transform* transform = world.Get<Transform>(cameraEntity);
    if (!transform)
    {
        return false;
    }

    // EVERY field is written, including when there is no lens. Leaving the
    // lens fields alone would make the answer depend on what the caller's
    // struct already held - and the two callers seed it differently: the
    // renderer reuses one CameraView across frames, picking passes a fresh
    // one. Strip the CameraComponent off after editing its FOV and the two
    // would quietly disagree about the projection, so the click would land
    // somewhere other than where it was pointed.
    //
    // A default-constructed component stands in for the missing one: an
    // entity can be a viewpoint with only a Transform, and this is what
    // "no lens specified" means.
    CameraComponent lens;
    if (const CameraComponent* found = world.Get<CameraComponent>(cameraEntity))
    {
        lens = *found;
    }
    outCamera = CameraViewOf(*transform, lens);

    return true;
}

void RunEditorSystems(EditorCamera& camera, const FrameContext& frame)
{
    ApplyCameraInput(camera.transform, frame.input, frame.deltaSeconds, true);
}

bool InteractSpinSystem(World& world, const ResourceManager& resources,
                        const InputContext& input)
{
    if (!input.WasPressed('E'))
    {
        return false;
    }

    CameraView camera;
    if (!GetActiveCameraView(world, camera))
    {
        return false;
    }

    Ray ray;
    ray.origin = camera.position;
    XMStoreFloat3(&ray.direction,
                  XMVector3Normalize(XMLoadFloat3(&camera.forward)));

    Entity target;
    constexpr float kInteractionDistance = 10.0f;
    if (!PickSpinEntity(world, resources, ray, kInteractionDistance, target))
    {
        return false;
    }

    Spin* spin = world.Get<Spin>(target);
    constexpr float kDefaultSpinSpeed = 1.2f;
    spin->speed = std::fabs(spin->speed) < 1e-6f ? kDefaultSpinSpeed : 0.0f;
    return true;
}

void RunPlaySystems(World& world, const PlaySession& session,
                    const ResourceManager* resources)
{
    if (!session.IsActive())
    {
        return;
    }
    GameCameraSystem(world, session.Input(), session.DeltaSeconds());
    if (resources)
    {
        InteractSpinSystem(world, *resources, session.Input());
    }
    SpinSystem(world, session.DeltaSeconds());
    LightOrbitSystem(world, session.ElapsedSeconds());
}

void BuildRenderData(World& world,
                     const ResourceManager& resources,
                     std::vector<DrawItem>& outItems,
                     LightingData& outLighting)
{
    // --- what to draw ---
    outItems.clear();
    world.ForEach<MeshRenderer>([&](Entity entity, MeshRenderer& renderer) {
        const Transform* transform = world.Get<Transform>(entity);
        if (!transform || !renderer.mesh.IsValid())
        {
            return; // a mesh with no place to be is not an error, just nothing
        }

        const XMMATRIX worldMatrix = WorldMatrixOf(*transform);
        XMFLOAT4X4 world;
        XMStoreFloat4x4(&world, worldMatrix);

        const Mesh& mesh = resources.GetMesh(renderer.mesh);
        XMFLOAT3 worldBoundsCenter = transform->position;
        if (!mesh.bounds.IsEmpty())
        {
            const XMFLOAT3 localCenter = mesh.bounds.Center();
            XMStoreFloat3(&worldBoundsCenter,
                          XMVector3TransformCoord(XMLoadFloat3(&localCenter), worldMatrix));
        }

        // ONE DRAW ITEM PER SUBMESH. A model that switches material partway
        // through its faces needs a draw per run, because a texture is bound
        // per draw. Every mesh has at least one submesh, so single-material
        // geometry falls out of the same loop with no special case.
        for (const Submesh& submesh : mesh.submeshes)
        {
            DrawItem item;
            item.mesh        = renderer.mesh;
            item.indexOffset = submesh.indexOffset;
            item.indexCount  = submesh.indexCount;
            item.world       = world;
            item.material    = renderer.material;
            item.worldBoundsCenter = worldBoundsCenter;
            item.layer = renderer.material.blendMode == Material::BlendMode::AlphaBlend
                       ? RenderLayer::Transparent
                       : RenderLayer::Opaque;

            // The submesh's own texture wins when the .mtl named one;
            // otherwise whatever the MeshRenderer carries applies. Colour,
            // specular and shininess always come from the renderer - those
            // are what the Inspector edits.
            if (submesh.texture.IsValid())
            {
                item.material.texture = submesh.texture;
            }
            outItems.push_back(item);
        }
    });

    // --- how it is lit ---
    outLighting = LightingData{};
    world.ForEach<Environment>([&](Entity, Environment& environment) {
        outLighting.ambient        = environment.ambient;
        outLighting.skybox         = environment.skybox;
        outLighting.shadowsEnabled = environment.shadowsEnabled;
        outLighting.shadowBias     = environment.shadowBias;
        outLighting.shadowStrength = environment.shadowStrength;
    });

    world.ForEach<Light>([&](Entity entity, Light& light) {
        const Transform* transform = world.Get<Transform>(entity);
        if (!transform)
        {
            return;
        }
        if (light.type == Light::Type::Directional)
        {
            // A directional light has no position - its Transform's
            // rotation is the only thing that matters.
            XMStoreFloat3(&outLighting.directionalDirection, ForwardFrom(*transform));
            outLighting.directionalColor = light.color;
        }
        else
        {
            outLighting.pointPosition = transform->position;
            outLighting.pointColor    = light.color;
            outLighting.pointRange    = light.range;
        }
    });
}
