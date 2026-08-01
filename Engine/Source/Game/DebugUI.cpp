#include "Game/DebugUI.h"

#include "Game/Components.h"

#include "imgui.h"

#include <cstdio>
#include <vector>

using namespace DirectX;

namespace
{
    // Which entity the inspector is showing. Persisting it across frames is
    // editor state, NOT scene data - it deliberately does not live in a
    // component, because saving the scene should not save what was selected.
    Entity g_selected;

    const char* LightTypeName(Light::Type type)
    {
        return type == Light::Type::Directional ? "Directional" : "Point";
    }

    // A one-line label for the entity list. Entities have no name component,
    // so describe them by what they carry.
    void DescribeEntity(World& world, Entity entity, char* out, size_t size)
    {
        const char* kind = "Entity";
        if (world.Get<CameraComponent>(entity))      kind = "Camera";
        else if (Light* light = world.Get<Light>(entity)) kind = LightTypeName(light->type);
        else if (world.Get<MeshRenderer>(entity))    kind = "Mesh";
        else if (world.Get<Environment>(entity))     kind = "Environment";

        std::snprintf(out, size, "%s #%u", kind, entity.index);
    }

    void DrawEntityList(World& world)
    {
        // FirstUseEver, not Always: this is only a starting layout. Once the
        // user drags a panel, imgui.ini remembers it and this stops applying.
        ImGui::SetNextWindowPos(ImVec2(20, 150), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(220, 380), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Entities"))
        {
            ImGui::End();
            return;
        }

        // Every entity with a Transform - which in this scene is all of them.
        // This loop IS the ECS query story: iterate one component, look the
        // rest up per entity.
        int count = 0;
        world.ForEach<Transform>([&](Entity entity, Transform&) {
            char label[64];
            DescribeEntity(world, entity, label, sizeof(label));
            if (ImGui::Selectable(label, g_selected == entity))
            {
                g_selected = entity;
            }
            ++count;
        });

        ImGui::Separator();
        ImGui::Text("%d entities", count);
        ImGui::End();
    }

    void DrawInspector(World& world)
    {
        ImGui::SetNextWindowPos(ImVec2(1000, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(260, 460), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }

        if (!world.IsAlive(g_selected))
        {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::End();
            return;
        }

        char label[64];
        DescribeEntity(world, g_selected, label, sizeof(label));
        ImGui::SeparatorText(label);

        // The inspector is written per component type. Adding a component to
        // the engine means adding a block here - no central switch to extend.
        if (Transform* transform = world.Get<Transform>(g_selected))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform->rotation.x, 0.01f);
                ImGui::DragFloat3("Scale",    &transform->scale.x,    0.05f, 0.01f, 100.0f);
            }
        }

        if (MeshRenderer* renderer = world.Get<MeshRenderer>(g_selected))
        {
            if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::ColorEdit3("Albedo",   &renderer->material.diffuseAlbedo.x);
                ImGui::ColorEdit3("Specular", &renderer->material.specularColor.x);
                ImGui::DragFloat("Shininess", &renderer->material.shininess,
                                 1.0f, 1.0f, 512.0f);
            }
        }

        if (Light* light = world.Get<Light>(g_selected))
        {
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::LabelText("Type", "%s", LightTypeName(light->type));
                ImGui::ColorEdit3("Color", &light->color.x);
                if (light->type == Light::Type::Point)
                {
                    ImGui::DragFloat("Range", &light->range, 0.5f, 0.1f, 200.0f);
                }
                else
                {
                    ImGui::TextDisabled("Direction comes from the Transform's rotation.");
                }
            }
        }

        if (Environment* environment = world.Get<Environment>(g_selected))
        {
            if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::ColorEdit3("Ambient", &environment->ambient.x);
            }
        }

        if (Spin* spin = world.Get<Spin>(g_selected))
        {
            if (ImGui::CollapsingHeader("Spin", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat("Speed", &spin->speed, 0.05f, -5.0f, 5.0f);
            }
        }

        ImGui::End();
    }

    // The scene, as a texture inside a window. Everything about this panel is
    // driven by the size ImGui gives it - the renderer follows the panel, not
    // the other way round.
    void DrawSceneViewport(DebugUIContext& ui)
    {
        // No padding: the image should reach the window border, the way every
        // editor's viewport does.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        const bool visible = ImGui::Begin("Scene");
        ImGui::PopStyleVar();

        if (!visible)
        {
            // Collapsed. Leaving the size at zero tells the renderer to keep
            // the target it has rather than shrink it to nothing.
            ImGui::End();
            return;
        }

        // The space left after the title bar - what the image gets, and
        // therefore what the render target should be.
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x >= 1.0f && size.y >= 1.0f)
        {
            ui.viewportWidth  = unsigned(size.x);
            ui.viewportHeight = unsigned(size.y);

            // The texture is one frame old in the sense that it was rendered
            // at the PREVIOUS size if the panel was just resized. It is
            // stretched for that one frame, then matches again.
            ImGui::Image(ImTextureID(ui.sceneTexture), size);
        }

        ui.viewportHovered = ImGui::IsWindowHovered();
        ImGui::End();
    }

    void DrawStats(DebugUIContext& ui)
    {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(220, 160), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Frame"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("%d fps   %.3f ms", ui.fps, ui.dt * 1000.0f);

        // A rolling history so a stutter is visible rather than averaged away.
        static float history[120] = {};
        static int   cursor       = 0;
        history[cursor] = ui.dt * 1000.0f;
        cursor = (cursor + 1) % IM_ARRAYSIZE(history);
        ImGui::PlotLines("##frametime", history, IM_ARRAYSIZE(history), cursor,
                         "frame time (ms)", 0.0f, 20.0f, ImVec2(0, 60));

        bool vsyncValue = ui.vsync;
        if (ImGui::Checkbox("VSync", &vsyncValue))
        {
            ui.vsyncToggled = true;
        }
        if (!ui.tearingSupported)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(no tearing support)");
        }

        // The scene is no longer the window size, and the difference matters
        // for anything that unprojects a click (10.4).
        ImGui::Separator();
        ImGui::Text("viewport %ux%u", ui.viewportWidth, ui.viewportHeight);

        ImGui::End();
    }
}

void DrawDebugUI(World& world, DebugUIContext& ui)
{
    // A full-window dock space so the panels can be rearranged and docked.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    // Before DrawStats, which reports the size this panel just asked for.
    DrawSceneViewport(ui);
    DrawStats(ui);
    DrawEntityList(world);
    DrawInspector(world);
}
