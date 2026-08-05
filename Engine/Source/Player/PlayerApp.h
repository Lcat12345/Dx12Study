// PlayerApp.h : runtime-only host with no editor or overlay dependency.
#pragma once

#include "Core/Engine.h"
#include "Core/World.h"
#include "Game/ExecutionContext.h"
#include "Graphics/RenderData.h"
#include "Player/PlayerStartup.h"

#include <vector>
#include <filesystem>
#include <string_view>

class PlayerApp final : public Engine
{
public:
    PlayerApp(HINSTANCE instance, const RuntimePaths& runtimePaths,
              PlayerStartup startup, const wchar_t* windowTitle);

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnFrameSampleSummary(const FrameSampleSummary& summary,
                              const Renderer::FrameStats& frame) override;

private:
    FrameContext CaptureHostFrame(float dt);
    uint64_t MeasurementEnemyCount() override;
    std::wstring MeasurementTitleStatus() override;
    bool WriteBenchmarkRow(std::string_view status,
                           const FrameSampleSummary* summary,
                           const Renderer::FrameStats& frame,
                           std::string_view error = {});

    World                 m_world;
    PlaySession           m_play;
    std::vector<DrawItem> m_drawItems;
    CameraView            m_camera;
    LightingData          m_lighting;
    PlayerStartup         m_startup;
};
