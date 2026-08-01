#include "Game/DebugUI.h"

#include "Game/Components.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

using namespace DirectX;

namespace
{
    // Which entity the inspector is showing. Persisting it across frames is
    // editor state, NOT scene data - it deliberately does not live in a
    // component, because saving the scene should not save what was selected.
    Entity g_selected;

    // ---------------------------------------------------------------------
    // The component registry
    //
    // Manual, on purpose. With eight types a table is shorter than the
    // reflection machinery that would generate it, and every entry is one
    // line. Adding a component type to the engine means adding one line here
    // and one editor block further down.
    // ---------------------------------------------------------------------

    struct ComponentOps
    {
        const char* name;
        bool (*has)(World&, Entity);
        void (*add)(World&, Entity);
        void (*remove)(World&, Entity);
        void (*copy)(World&, Entity from, Entity to);
    };

    // Captureless lambdas convert to plain function pointers, so this stays a
    // static table with no allocation and no virtual dispatch.
    template <typename T>
    constexpr ComponentOps MakeOps(const char* name)
    {
        return {
            name,
            [](World& world, Entity entity) { return world.Has<T>(entity); },
            [](World& world, Entity entity) { world.Add<T>(entity); },
            [](World& world, Entity entity) { world.Remove<T>(entity); },
            [](World& world, Entity from, Entity to) {
                if (const T* value = world.Get<T>(from)) { world.Add<T>(to, *value); }
            },
        };
    }

    // Order must match kComponents below - the static_assert catches drift,
    // but only in the count, so keep them side by side.
    enum class Comp
    {
        Name, Transform, MeshRenderer, Camera, Light, Spin, ActiveCamera, Environment,
        Count
    };

    const ComponentOps kComponents[] = {
        MakeOps<Name>("Name"),
        MakeOps<Transform>("Transform"),
        MakeOps<MeshRenderer>("Mesh Renderer"),
        MakeOps<CameraComponent>("Camera"),
        MakeOps<Light>("Light"),
        MakeOps<Spin>("Spin"),
        MakeOps<ActiveCamera>("Active Camera"),
        MakeOps<Environment>("Environment"),
    };
    static_assert(std::size(kComponents) == size_t(Comp::Count),
                  "kComponents and Comp must list the same types in the same order");

    // ---------------------------------------------------------------------
    // Deferred structural edits
    //
    // Panels QUEUE, they never mutate. Both panels walk the world while they
    // draw: Create pushes onto a dense array that ForEach is iterating, and
    // Destroy swap-and-pops out from under it. Editing values in place is
    // fine - that changes no array's shape - which is why the inspector's
    // sliders write directly and only the buttons queue.
    // ---------------------------------------------------------------------

    struct Command
    {
        enum class Kind { Create, Destroy, Duplicate, AddComponent, RemoveComponent };

        Kind   kind;
        Entity target;
        Comp   component = Comp::Count; // only for Add/RemoveComponent
    };

    std::vector<Command> g_commands;

    void Queue(Command::Kind kind, Entity target, Comp component = Comp::Count)
    {
        g_commands.push_back({ kind, target, component });
    }

    void ApplyCommands(World& world)
    {
        for (const Command& command : g_commands)
        {
            switch (command.kind)
            {
            case Command::Kind::Create:
            {
                // A bare entity would be invisible in every panel, so it
                // starts with the two components that make it addressable.
                const Entity created = world.Create();
                world.Add<Name>(created, { "New Entity" });
                world.Add<Transform>(created);
                g_selected = created;
                break;
            }

            case Command::Kind::Duplicate:
            {
                if (!world.IsAlive(command.target))
                {
                    break;
                }
                const Entity copy = world.Create();
                for (size_t i = 0; i < std::size(kComponents); ++i)
                {
                    // ActiveCamera is a "there is exactly one" tag. Copying
                    // it would leave two, and FindActiveCamera would pick
                    // whichever the storage happened to order first.
                    if (i == size_t(Comp::ActiveCamera))
                    {
                        continue;
                    }
                    kComponents[i].copy(world, command.target, copy);
                }
                if (Name* name = world.Get<Name>(copy))
                {
                    char buffer[Name::kCapacity];
                    std::snprintf(buffer, sizeof(buffer), "%s copy", name->value);
                    std::memcpy(name->value, buffer, sizeof(buffer));
                }
                g_selected = copy;
                break;
            }

            case Command::Kind::Destroy:
                world.Destroy(command.target);
                // IsAlive would already report false thanks to the bumped
                // generation; clearing it makes the intent explicit.
                if (command.target == g_selected)
                {
                    g_selected = Entity{};
                }
                break;

            case Command::Kind::AddComponent:
                kComponents[size_t(command.component)].add(world, command.target);
                break;

            case Command::Kind::RemoveComponent:
                kComponents[size_t(command.component)].remove(world, command.target);
                break;
            }
        }
        g_commands.clear();
    }

    // ---------------------------------------------------------------------
    // Panels
    // ---------------------------------------------------------------------

    const char* LightTypeName(Light::Type type)
    {
        return type == Light::Type::Directional ? "Directional" : "Point";
    }

    // A one-line label for the entity list. The Name component if there is
    // one; otherwise describe the entity by what it carries, which is still
    // more useful than a bare number.
    void DescribeEntity(World& world, Entity entity, char* out, size_t size)
    {
        if (const Name* name = world.Get<Name>(entity))
        {
            // The index stays visible: two entities may share a name, and
            // the list has to remain unambiguous.
            std::snprintf(out, size, "%s##%u", name->value, entity.index);
            return;
        }

        const char* kind = "Entity";
        if (world.Get<CameraComponent>(entity))           kind = "Camera";
        else if (Light* light = world.Get<Light>(entity)) kind = LightTypeName(light->type);
        else if (world.Get<MeshRenderer>(entity))         kind = "Mesh";
        else if (world.Get<Environment>(entity))          kind = "Environment";

        std::snprintf(out, size, "%s #%u", kind, entity.index);
    }

    void DrawEntityList(World& world)
    {
        // FirstUseEver, not Always: this is only a starting layout. Once the
        // user drags a panel, imgui.ini remembers it and this stops applying.
        ImGui::SetNextWindowPos(ImVec2(20, 190), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(240, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Entities"))
        {
            ImGui::End();
            return;
        }

        const bool hasSelection = world.IsAlive(g_selected);

        if (ImGui::Button("New"))
        {
            Queue(Command::Kind::Create, Entity{});
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection);
        if (ImGui::Button("Duplicate"))
        {
            Queue(Command::Kind::Duplicate, g_selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            Queue(Command::Kind::Destroy, g_selected);
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        // Every LIVE entity, not every entity with a Transform. Strip an
        // entity's last component and it must still be here to get one back.
        int count = 0;
        if (ImGui::BeginChild("list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
        {
            world.ForEachEntity([&](Entity entity) {
                char label[96];
                DescribeEntity(world, entity, label, sizeof(label));
                if (ImGui::Selectable(label, g_selected == entity))
                {
                    g_selected = entity;
                }
                ++count;
            });
        }
        ImGui::EndChild();

        ImGui::Text("%d entities", count);
        ImGui::End();
    }

    // A component header with ImGui's built-in close button. Returns true
    // when the body should be drawn; the X queues a removal.
    bool ComponentHeader(const char* label, Comp type, Entity entity)
    {
        bool keep = true;
        const bool open = ImGui::CollapsingHeader(label, &keep,
                                                  ImGuiTreeNodeFlags_DefaultOpen);
        if (!keep)
        {
            Queue(Command::Kind::RemoveComponent, entity, type);
        }
        return open;
    }

    void DrawAddComponentMenu(World& world)
    {
        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::OpenPopup("add_component");
        }
        if (!ImGui::BeginPopup("add_component"))
        {
            return;
        }

        bool anyOffered = false;
        for (size_t i = 0; i < std::size(kComponents); ++i)
        {
            // Add is really "add or overwrite" in the storage, so offering a
            // type the entity already has would quietly reset its values.
            if (kComponents[i].has(world, g_selected))
            {
                continue;
            }
            anyOffered = true;
            if (ImGui::Selectable(kComponents[i].name))
            {
                Queue(Command::Kind::AddComponent, g_selected, Comp(i));
            }
        }
        if (!anyOffered)
        {
            ImGui::TextDisabled("Nothing left to add.");
        }
        ImGui::EndPopup();
    }

    void DrawInspector(World& world)
    {
        ImGui::SetNextWindowPos(ImVec2(1000, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);
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

        ImGui::Text("Entity #%u  (generation %u)", g_selected.index, g_selected.generation);
        ImGui::Separator();

        // The inspector is written per component type. Adding a component to
        // the engine means adding a block here - no central switch to extend.
        if (Name* name = world.Get<Name>(g_selected))
        {
            if (ComponentHeader("Name", Comp::Name, g_selected))
            {
                // Writes straight into the component's buffer. Safe because
                // structural edits are deferred - nothing can move this
                // storage while the widget holds a pointer into it.
                ImGui::InputText("##name", name->value, Name::kCapacity);
            }
        }

        if (Transform* transform = world.Get<Transform>(g_selected))
        {
            if (ComponentHeader("Transform", Comp::Transform, g_selected))
            {
                ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform->rotation.x, 0.01f);
                ImGui::DragFloat3("Scale",    &transform->scale.x,    0.05f, 0.01f, 100.0f);
            }
        }

        if (MeshRenderer* renderer = world.Get<MeshRenderer>(g_selected))
        {
            if (ComponentHeader("Mesh Renderer", Comp::MeshRenderer, g_selected))
            {
                // A freshly added MeshRenderer has no mesh, so it draws
                // nothing. Choosing one is 10.3's asset browser.
                if (renderer->mesh.IsValid())
                {
                    ImGui::LabelText("Mesh", "handle %u", renderer->mesh.index);
                }
                else
                {
                    ImGui::TextDisabled("No mesh - nothing is drawn.");
                }
                ImGui::ColorEdit3("Albedo",   &renderer->material.diffuseAlbedo.x);
                ImGui::ColorEdit3("Specular", &renderer->material.specularColor.x);
                ImGui::DragFloat("Shininess", &renderer->material.shininess,
                                 1.0f, 1.0f, 512.0f);
            }
        }

        if (CameraComponent* lens = world.Get<CameraComponent>(g_selected))
        {
            if (ComponentHeader("Camera", Comp::Camera, g_selected))
            {
                ImGui::SliderAngle("FOV", &lens->fovY, 10.0f, 150.0f);
                ImGui::DragFloat("Near", &lens->nearZ, 0.01f, 0.01f, 10.0f);
                ImGui::DragFloat("Far",  &lens->farZ,  1.0f,  1.0f,  5000.0f);
            }
        }

        if (Light* light = world.Get<Light>(g_selected))
        {
            if (ComponentHeader("Light", Comp::Light, g_selected))
            {
                int type = int(light->type);
                if (ImGui::Combo("Type", &type, "Directional\0Point\0"))
                {
                    light->type = Light::Type(type);
                }
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

        if (Spin* spin = world.Get<Spin>(g_selected))
        {
            if (ComponentHeader("Spin", Comp::Spin, g_selected))
            {
                ImGui::DragFloat("Speed", &spin->speed, 0.05f, -5.0f, 5.0f);
            }
        }

        if (world.Has<ActiveCamera>(g_selected))
        {
            if (ComponentHeader("Active Camera", Comp::ActiveCamera, g_selected))
            {
                ImGui::TextDisabled("A tag: no data, just a type.");
            }
        }

        if (Environment* environment = world.Get<Environment>(g_selected))
        {
            if (ComponentHeader("Environment", Comp::Environment, g_selected))
            {
                ImGui::ColorEdit3("Ambient", &environment->ambient.x);
            }
        }

        ImGui::Separator();
        DrawAddComponentMenu(world);

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

    void DrawStats(DebugUIContext& ui, int entityCount, int drawnCount)
    {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(240, 160), ImGuiCond_FirstUseEver);
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
        // Draw items, not entities: placing by hand is what will eventually
        // hit the object constant buffer's ceiling.
        ImGui::Text("%d entities, %d drawn / %d", entityCount, drawnCount, ui.maxDrawItems);

        ImGui::End();
    }
}

void DrawDebugUI(World& world, DebugUIContext& ui)
{
    // A full-window dock space so the panels can be rearranged and docked.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    int entityCount = 0;
    int drawnCount  = 0;
    world.ForEachEntity([&](Entity entity) {
        ++entityCount;
        if (world.Has<MeshRenderer>(entity) && world.Has<Transform>(entity))
        {
            ++drawnCount;
        }
    });

    // Before DrawStats, which reports the size this panel just asked for.
    DrawSceneViewport(ui);
    DrawStats(ui, entityCount, drawnCount);
    DrawEntityList(world);
    DrawInspector(world);

    // Every panel has finished iterating, so it is finally safe to change
    // the shape of the world.
    ApplyCommands(world);
}
