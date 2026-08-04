// Systems.h : pure logic. Systems own no state; they read and write
// components on a World.
#pragma once

#include "Core/World.h"
#include "Game/Components.h"
#include "Game/ExecutionContext.h"
#include "Graphics/RenderData.h"

#include <DirectXMath.h>
#include <vector>

// Scale, then rotate, then translate - the order every other piece of code
// has to agree on. Shared rather than reimplemented because picking has to
// invert exactly the matrix the renderer drew with.
DirectX::XMMATRIX WorldMatrixOf(const Transform& transform);

// The editor camera deliberately lives outside World. Moving it cannot dirty
// a Scene or change the ActiveCamera that the game will use on Play.
struct EditorCamera
{
    Transform       transform;
    CameraComponent lens;
};

// Copies the initial Phase 11 view once. Later scene replacements leave the
// editor camera alone because it is the editor user's viewpoint.
bool InitializeEditorCamera(World& world, EditorCamera& outCamera);
CameraView GetEditorCameraView(const EditorCamera& camera);

// Advances every entity that has both a Spin and a Transform.
void SpinSystem(World& world, float dt);

// Applies flattened input to the active game camera. No system polls Win32.
void GameCameraSystem(World& world, const InputContext& input, float dt);

// Moves point lights that have a Spin, so the orbiting light is now scene
// data rather than a hardcoded sine in the renderer.
void LightOrbitSystem(World& world, float totalSeconds);

// Fills outCamera from the entity tagged ActiveCamera. False when there is
// none, or when it has no Transform to sit in.
//
// Split out of BuildRenderData because the host must select the game camera
// separately from EditorCamera and then share that exact CameraView with
// presentation and picking.
//
// On success it overwrites EVERY field, so the result never depends on what
// outCamera happened to contain. On failure it writes nothing, and the two
// callers then diverge deliberately: the renderer keeps drawing from the
// last good view, while picking refuses to place anything. Both are the
// safe answer to "there is no camera" for their own job.
bool GetActiveCameraView(World& world, CameraView& outCamera);

// The two explicit mode-specific groups. The host calls exactly one per
// frame; this is intentionally smaller than a general scheduler.
void RunEditorSystems(EditorCamera& camera, const FrameContext& frame);
// resources may be null only for logic-only tests with no asset-backed
// interaction. Editor Play and Player always supply the shared manager.
void RunPlaySystems(World& world, const PlaySession& session,
                    const ResourceManager* resources = nullptr);

// Fires a normalized ray from the ActiveCamera and toggles the nearest Spin
// mesh within a deliberately short gameplay interaction range. The edge is
// consumed by RunPlaySystems, so holding E cannot toggle every frame.
bool InteractSpinSystem(World& world, const ResourceManager& resources,
                        const InputContext& input);

// --- the render boundary ---
// Walks the world once and flattens it into what the renderer accepts.
// Nothing below this line knows about entities.
//
// Needs the ResourceManager to read each mesh's submesh table: a model with
// several materials becomes several DrawItems, and only the loaded mesh
// knows where those ranges are.
void BuildRenderData(World& world,
                     const ResourceManager& resources,
                     std::vector<DrawItem>& outItems,
                     LightingData& outLighting);
