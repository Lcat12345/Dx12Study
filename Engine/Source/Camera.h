// Camera.h : free-look camera state and its per-frame update.
#pragma once

#include <DirectXMath.h>

// A free-look camera is just a position plus two angles; the view matrix is
// derived from them every frame.
struct Camera
{
    DirectX::XMFLOAT3 position = { 0.0f, 3.5f, -22.0f };
    float             yaw      = 0.0f; // left/right, radians
    float             pitch    = 0.0f; // up/down, radians
};

// Yaw/pitch -> a forward direction. Yaw 0 looks down +Z.
DirectX::XMVECTOR CameraForward(const Camera& camera);

// Applies mouse delta accumulated since the last frame, then the movement
// keys currently held. dt keeps the speed frame-rate independent.
void UpdateCamera(Camera& camera, float mouseDeltaX, float mouseDeltaY, float dt);
