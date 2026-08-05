// PlayerApp.h : runtime-only host with no editor or overlay dependency.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Game/ExecutionContext.h"
#include "Graphics/RenderData.h"

#include <vector>
#include <filesystem>

class PlayerApp final : public Engine
{
public:
    PlayerApp(HINSTANCE instance, const RuntimePaths& runtimePaths,
              std::filesystem::path scenePath, const wchar_t* windowTitle);

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    FrameContext CaptureHostFrame(float dt);
    uint64_t MeasurementEnemyCount() override;
    std::wstring MeasurementTitleStatus() override;

    World                 m_world;
    PlaySession           m_play;
    std::vector<DrawItem> m_drawItems;
    CameraView            m_camera;
    LightingData          m_lighting;
    std::filesystem::path m_scenePath;
};
