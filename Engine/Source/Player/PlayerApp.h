// PlayerApp.h : runtime-only host with no editor or overlay dependency.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Game/ExecutionContext.h"
#include "Graphics/RenderData.h"

#include <vector>

class PlayerApp final : public Engine
{
public:
    explicit PlayerApp(HINSTANCE instance);

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    FrameContext CaptureHostFrame(float dt);

    World                 m_world;
    PlaySession           m_play;
    std::vector<DrawItem> m_drawItems;
    CameraView            m_camera;
    LightingData          m_lighting;
};
