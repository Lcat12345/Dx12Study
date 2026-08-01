// DemoGame.h : the game side - what is in the world and how it is driven.
#pragma once

#include "Core/Engine.h"
#include "Game/Scene.h"
#include "Game/Camera.h"

// Everything specific to this demo lives here; Engine and Renderer stay
// unaware of Scene and Camera.
class DemoGame : public Engine
{
public:
    DemoGame(HINSTANCE instance);

protected:
    void OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;

private:
    Scene  m_scene;
    Camera m_camera;
};
