#include "Camera.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    constexpr float kMoveSpeed      = 8.0f;   // units per second
    constexpr float kFastMultiplier = 3.0f;
    constexpr float kMouseSpeed     = 0.004f; // radians per pixel
}

XMVECTOR CameraForward(const Camera& camera)
{
    const float cosPitch = std::cos(camera.pitch);
    return XMVector3Normalize(XMVectorSet(cosPitch * std::sin(camera.yaw),
                                          std::sin(camera.pitch),
                                          cosPitch * std::cos(camera.yaw),
                                          0.0f));
}

void UpdateCamera(Camera& camera, float mouseDeltaX, float mouseDeltaY, float dt)
{
    camera.yaw   += mouseDeltaX * kMouseSpeed;
    camera.pitch -= mouseDeltaY * kMouseSpeed;

    // Clamp just short of straight up/down, where the camera would flip.
    constexpr float kPitchLimit = XM_PIDIV2 - 0.01f;
    camera.pitch = std::clamp(camera.pitch, -kPitchLimit, kPitchLimit);

    const XMVECTOR forward = CameraForward(camera);
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    // Left-handed: cross(up, forward) points right.
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

    // GetAsyncKeyState polls the physical key state - good enough here;
    // WM_INPUT (raw input) is the upgrade path for precise handling.
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

    XMVECTOR move = XMVectorZero();
    if (down('W')) move = XMVectorAdd(move, forward);
    if (down('S')) move = XMVectorSubtract(move, forward);
    if (down('D')) move = XMVectorAdd(move, right);
    if (down('A')) move = XMVectorSubtract(move, right);
    if (down('E')) move = XMVectorAdd(move, worldUp);
    if (down('Q')) move = XMVectorSubtract(move, worldUp);

    if (XMVectorGetX(XMVector3LengthSq(move)) > 0.0f)
    {
        float speed = kMoveSpeed;
        if (down(VK_SHIFT))
        {
            speed *= kFastMultiplier;
        }
        // Normalize first: otherwise diagonal movement would be faster.
        // Multiplying by dt is what makes speed frame-rate independent.
        move = XMVectorScale(XMVector3Normalize(move), speed * dt);

        XMVECTOR position = XMLoadFloat3(&camera.position);
        XMStoreFloat3(&camera.position, XMVectorAdd(position, move));
    }
}
