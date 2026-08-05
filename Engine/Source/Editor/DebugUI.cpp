#include "Editor/DebugUI.h"

#include "Editor/AssetBrowser.h"
#include "Game/BuildWorld.h"
#include "Game/Components.h"
#include "Editor/EditorSession.h"
#include "Game/Picking.h"
#include "Game/Scene.h"
#include "Core/Common.h"
#include "Core/TextEncoding.h"

#include "imgui.h"
// DockBuilder lives in the internal header. It is the only supported way to
// describe a default layout in code rather than shipping a prebuilt
// imgui.ini, which would have to be copied next to every build output and
// would go stale the moment a panel is renamed.
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
    // The size range a click-placed mesh is clamped into, in world units.
    //
    // 20 is a quarter of the 80-unit floor and well inside the 200 far plane,
    // so a model at the limit is big but framed. It is also about what the
    // old hardcoded laevat placement worked out to (fitToSize 8 x scale 2.6
    // = 20.8), which is the size that actually looked right on screen.
    //
    // The lower bound matters for the same reason as the upper one: a file
    // authored in millimetres arrives a thousand times too small and is just
    // as invisible as one that is too large.
    constexpr float kMaxPlacedExtent = 20.0f;
    constexpr float kMinPlacedExtent = 0.25f;

    // The uniform scale that brings a mesh's longest axis into that range.
    //
    // Exactly 1.0 when it is already there, which is the property that keeps
    // procedural meshes and sanely authored files placing as they always did.
    // Both call sites - click placement and the inspector's Fit button - go
    // through here so there is one rule, not two that drift apart.
    float FitScaleFor(const Aabb& bounds)
    {
        if (bounds.IsEmpty())
        {
            return 1.0f;
        }
        const XMFLOAT3 extents = bounds.Extents();
        const float    longest = 2.0f * (std::max)({ extents.x, extents.y, extents.z });

        if (longest <= 0.0f)            { return 1.0f; } // a single point
        if (longest > kMaxPlacedExtent) { return kMaxPlacedExtent / longest; }
        if (longest < kMinPlacedExtent) { return kMinPlacedExtent / longest; }
        return 1.0f;
    }

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
    using Comp    = EditorComponent;
    using Command = EditorCommand;

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

    void Queue(EditorSession& session, Command::Kind kind, Entity target,
               Comp component = Comp::Count)
    {
        Command command;
        command.kind      = kind;
        command.target    = target;
        command.component = component;
        session.commands.push_back(std::move(command));
    }

    void QueuePlace(EditorSession& session, const XMFLOAT3& position,
                    MeshHandle mesh, TextureHandle texture, const char* name)
    {
        Command command;
        command.kind     = Command::Kind::Place;
        command.position = position;
        command.mesh     = mesh;
        command.texture  = texture;
        command.name     = name;
        session.commands.push_back(std::move(command));
    }

    void QueueScene(EditorSession& session, Command::Kind kind,
                    const std::wstring& path = {})
    {
        Command command;
        command.kind = kind;
        command.name = ToUtf8(path);
        session.commands.push_back(std::move(command));
    }

    void ApplyCommands(World& world, ResourceManager& resources, EditorSession& session)
    {
        // Move the batch out first. OnWorldReplaced can then clear the public
        // queue without invalidating this iteration, and no command queued for
        // an old World can survive into its replacement.
        std::vector<Command> commands = std::move(session.commands);
        session.commands.clear();
        for (const Command& command : commands)
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
                session.selected = created;
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
                session.selected = copy;
                break;
            }

            case Command::Kind::Destroy:
                world.Destroy(command.target);
                // IsAlive would already report false thanks to the bumped
                // generation; clearing it makes the intent explicit.
                if (command.target == session.selected)
                {
                    session.selected = Entity{};
                }
                break;

            case Command::Kind::Place:
            {
                const Entity placed = world.Create();

                Name name;
                std::snprintf(name.value, Name::kCapacity, "%s", command.name.c_str());
                world.Add<Name>(placed, name);

                Transform transform;
                transform.position = command.position;
                if (command.mesh.IsValid())
                {
                    const Aabb& bounds = resources.GetMesh(command.mesh).bounds;
                    if (!bounds.IsEmpty())
                    {
                        const XMFLOAT3 center = bounds.Center();

                        // A model authored elsewhere is rarely in our units.
                        // laevat.obj is 64,453 units on its longest axis and
                        // sits 34,123 from its own origin - placed as-is it
                        // lands entirely behind the camera, so the editor
                        // reports a successful load and shows nothing.
                        //
                        // The fix is a Transform SCALE, not FitMeshToSize:
                        // baking it into the vertices would put the shrunken
                        // copy in the mesh cache under the same key, with no
                        // way back to the original, and would hide the number
                        // from the inspector. As a Transform field it is one
                        // value the user can see and overwrite.
                        const float scale = FitScaleFor(bounds);
                        transform.scale = { scale, scale, scale };

                        // Put the mesh where the click landed rather than
                        // where its own origin happens to be, and rest its
                        // LOWEST point on the plane instead of half-burying
                        // it. Every offset goes through the same scale.
                        //
                        // Correct only because a freshly placed entity has no
                        // rotation. Re-seating an already rotated object is a
                        // different problem and not this one.
                        transform.position.x -= center.x * scale;
                        transform.position.z -= center.z * scale;
                        transform.position.y -= bounds.min.y * scale;
                    }
                }
                world.Add<Transform>(placed, transform);

                MeshRenderer renderer;
                renderer.mesh             = command.mesh;
                renderer.material.texture = command.texture;
                world.Add<MeshRenderer>(placed, renderer);

                session.selected = placed;
                break;
            }

            case Command::Kind::NewScene:
                // Replacing the world invalidates every handle into it.
                world = World{};
                BuildEmptyScene(world);
                session.OnWorldReplaced();
                session.scenePath.clear();
                session.sceneStatus = "new scene";
                return;

            case Command::Kind::LoadScene:
            {
                const std::filesystem::path path = ToWide(command.name);
                World       loaded;
                std::string error;
                if (LoadScene(path, resources, loaded, error))
                {
                    world         = std::move(loaded);
                    session.OnWorldReplaced();
                    session.scenePath   = path;
                    session.sceneStatus = "opened " + ToUtf8(path.filename().wstring());
                    return;
                }
                else
                {
                    // The current scene is untouched - LoadScene built into
                    // a temporary and never got as far as swapping it in.
                    session.sceneStatus = "open failed: " + error;
                }
                break;
            }

            case Command::Kind::AddComponent:
                kComponents[size_t(command.component)].add(world, command.target);
                break;

            case Command::Kind::RemoveComponent:
                kComponents[size_t(command.component)].remove(world, command.target);
                break;
            }
        }
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

    void DrawEntityList(World& world, EditorSession& session)
    {
        // FirstUseEver, not Always: this is only a starting layout. Once the
        // user drags a panel, imgui.ini remembers it and this stops applying.
        ImGui::SetNextWindowPos(ImVec2(20, 190), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(240, 260), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Entities"))
        {
            ImGui::End();
            return;
        }

        const bool hasSelection = world.IsAlive(session.selected);

        if (ImGui::Button("New"))
        {
            Queue(session, Command::Kind::Create, Entity{});
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection);
        if (ImGui::Button("Duplicate"))
        {
            Queue(session, Command::Kind::Duplicate, session.selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            Queue(session, Command::Kind::Destroy, session.selected);
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
                if (ImGui::Selectable(label, session.selected == entity))
                {
                    session.selected = entity;
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
    bool ComponentHeader(EditorSession& session, const char* label,
                         Comp type, Entity entity)
    {
        bool keep = true;
        const bool open = ImGui::CollapsingHeader(label, &keep,
                                                  ImGuiTreeNodeFlags_DefaultOpen);
        if (!keep)
        {
            Queue(session, Command::Kind::RemoveComponent, entity, type);
        }
        return open;
    }

    void DrawAddComponentMenu(World& world, EditorSession& session)
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
            if (kComponents[i].has(world, session.selected))
            {
                continue;
            }
            anyOffered = true;
            if (ImGui::Selectable(kComponents[i].name))
            {
                Queue(session, Command::Kind::AddComponent, session.selected, Comp(i));
            }
        }
        if (!anyOffered)
        {
            ImGui::TextDisabled("Nothing left to add.");
        }
        ImGui::EndPopup();
    }

    void DrawInspector(World& world, const ResourceManager& resources,
                       AssetBrowser& assets, EditorSession& session)
    {
        ImGui::SetNextWindowPos(ImVec2(1000, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }

        if (!world.IsAlive(session.selected))
        {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::End();
            return;
        }

        ImGui::Text("Entity #%u  (generation %u)",
                    session.selected.index, session.selected.generation);
        ImGui::Separator();

        // The inspector is written per component type. Adding a component to
        // the engine means adding a block here - no central switch to extend.
        if (Name* name = world.Get<Name>(session.selected))
        {
            if (ComponentHeader(session, "Name", Comp::Name, session.selected))
            {
                // Writes straight into the component's buffer. Safe because
                // structural edits are deferred - nothing can move this
                // storage while the widget holds a pointer into it.
                ImGui::InputText("##name", name->value, Name::kCapacity);
            }
        }

        if (Transform* transform = world.Get<Transform>(session.selected))
        {
            if (ComponentHeader(session, "Transform", Comp::Transform, session.selected))
            {
                ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &transform->rotation.x, 0.01f);
                // Scale spans four orders of magnitude now that placement
                // fits oversized models: laevat lands at 3.1e-4. The default
                // "%.3f" showed that as 0.000, which reads as a degenerate
                // object, and the old 0.01 minimum SNAPPED it up 32x the
                // moment anyone touched the field.
                //
                // Logarithmic makes the drag multiplicative, so one pixel is
                // a percentage rather than a fixed step - the only way one
                // control can edit both 3.1e-4 and 100 usefully.
                ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f,
                                  1e-5f, 1000.0f, "%.5g",
                                  ImGuiSliderFlags_Logarithmic);
            }
        }

        if (MeshRenderer* renderer = world.Get<MeshRenderer>(session.selected))
        {
            if (ComponentHeader(session, "Mesh Renderer", Comp::MeshRenderer,
                                session.selected))
            {
                // A freshly added MeshRenderer has no mesh and draws
                // nothing until one is assigned from the browser.
                if (renderer->mesh.IsValid())
                {
                    ImGui::LabelText("Mesh", "handle %u", renderer->mesh.index);
                }
                else
                {
                    ImGui::TextDisabled("No mesh - nothing is drawn.");
                }

                const MeshHandle    browserMesh    = assets.SelectedMesh();
                const TextureHandle browserTexture = assets.SelectedTexture();

                // Disabled until the browser has actually loaded something -
                // an asset that failed to load must not be assignable.
                ImGui::BeginDisabled(!browserMesh.IsValid());
                if (ImGui::Button("Assign mesh"))
                {
                    renderer->mesh = browserMesh;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", assets.SelectedMeshLabel());

                // Assigning a mesh deliberately does NOT touch the Transform:
                // overwriting a scale the user set - Backdrop's (6, 3, 0.5),
                // say - to fit new geometry would throw away their work.
                //
                // But that leaves the swap able to make an entity vanish, the
                // same way placement used to, since a 64,453-unit model at
                // scale 1 is nowhere near the camera. So the fit is offered
                // as its own button instead of happening behind the user's
                // back: placement means "make me a sane new object", this
                // means "I am asking for it, now".
                ImGui::BeginDisabled(!renderer->mesh.IsValid());
                if (ImGui::Button("Fit"))
                {
                    if (Transform* transform = world.Get<Transform>(session.selected))
                    {
                        const Aabb& bounds = resources.GetMesh(renderer->mesh).bounds;
                        if (!bounds.IsEmpty())
                        {
                            const float scale = FitScaleFor(bounds);
                            transform->scale = { scale, scale, scale };
                            // Rest it on y = 0 too. Scale alone would leave
                            // laevat's feet 3.9 units under the floor, which
                            // is not what anyone means by "fit".
                            //
                            // Horizontal position is left alone - that is
                            // where the user put it, and nothing about a size
                            // change implies it should move.
                            transform->position.y = -bounds.min.y * scale;
                        }
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("size to view, rest on the floor");

                ImGui::BeginDisabled(!browserTexture.IsValid());
                if (ImGui::Button("Assign texture"))
                {
                    renderer->material.texture = browserTexture;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", assets.SelectedTextureLabel());

                // The same browser selection, assigned to the other slot.
                // Normal maps ship beside the colour maps and are told apart
                // only by a naming convention (_n here), so the browser
                // cannot know which is which - the button says it.
                ImGui::BeginDisabled(!browserTexture.IsValid());
                if (ImGui::Button("Assign normal"))
                {
                    renderer->material.normalTexture = browserTexture;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!renderer->material.normalTexture.IsValid());
                if (ImGui::Button("Clear normal"))
                {
                    renderer->material.normalTexture = TextureHandle{};
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s",
                    renderer->material.normalTexture.IsValid()
                        ? ToUtf8(resources.TextureName(renderer->material.normalTexture)).c_str()
                        : "flat");

                // Above 1 exaggerates. Kept available on purpose: a subtle map
                // that does nothing visible and a map that is not bound at all
                // look identical until you can turn it up.
                ImGui::DragFloat("Normal strength", &renderer->material.normalStrength,
                                 0.01f, 0.0f, 4.0f);

                int blendMode = renderer->material.blendMode == Material::BlendMode::AlphaBlend
                              ? 1 : 0;
                const char* blendModes[] = { "Opaque", "Alpha blend" };
                if (ImGui::Combo("Blend mode", &blendMode, blendModes,
                                 int(std::size(blendModes))))
                {
                    renderer->material.blendMode = blendMode == 1
                                                 ? Material::BlendMode::AlphaBlend
                                                 : Material::BlendMode::Opaque;
                }
                ImGui::ColorEdit4("Albedo",   &renderer->material.diffuseAlbedo.x);
                ImGui::ColorEdit3("Specular", &renderer->material.specularColor.x);
                ImGui::DragFloat("Shininess", &renderer->material.shininess,
                                 1.0f, 1.0f, 512.0f);
            }
        }

        if (CameraComponent* lens = world.Get<CameraComponent>(session.selected))
        {
            if (ComponentHeader(session, "Camera", Comp::Camera, session.selected))
            {
                ImGui::SliderAngle("FOV", &lens->fovY, 10.0f, 150.0f);
                ImGui::DragFloat("Near", &lens->nearZ, 0.01f, 0.01f, 10.0f);
                ImGui::DragFloat("Far",  &lens->farZ,  1.0f,  1.0f,  5000.0f);
            }
        }

        if (Light* light = world.Get<Light>(session.selected))
        {
            if (ComponentHeader(session, "Light", Comp::Light, session.selected))
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

        if (Spin* spin = world.Get<Spin>(session.selected))
        {
            if (ComponentHeader(session, "Spin", Comp::Spin, session.selected))
            {
                ImGui::DragFloat("Speed", &spin->speed, 0.05f, -5.0f, 5.0f);
            }
        }

        if (world.Has<ActiveCamera>(session.selected))
        {
            if (ComponentHeader(session, "Active Camera", Comp::ActiveCamera,
                                session.selected))
            {
                ImGui::TextDisabled("A tag: no data, just a type.");
            }
        }

        if (Environment* environment = world.Get<Environment>(session.selected))
        {
            if (ComponentHeader(session, "Environment", Comp::Environment,
                                session.selected))
            {
                ImGui::ColorEdit3("Ambient", &environment->ambient.x);

                const CubeTextureHandle browserSkybox = assets.SelectedSkybox();
                ImGui::BeginDisabled(!browserSkybox.IsValid());
                if (ImGui::Button("Assign skybox"))
                {
                    environment->skybox = browserSkybox;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", assets.SelectedSkyboxLabel());

                ImGui::BeginDisabled(!environment->skybox.IsValid());
                if (ImGui::Button("Clear skybox"))
                {
                    environment->skybox = CubeTextureHandle{};
                }
                ImGui::EndDisabled();

                ImGui::SeparatorText("Directional shadows");
                ImGui::Checkbox("Enabled", &environment->shadowsEnabled);
                ImGui::BeginDisabled(!environment->shadowsEnabled);
                ImGui::DragFloat("Base bias", &environment->shadowBias, 0.00001f,
                                 Environment::kMinShadowBias,
                                 Environment::kMaxShadowBias, "%.6f");
                ImGui::TextDisabled("Grows with surface slope, up to 4x.");
                ImGui::SliderFloat("Strength", &environment->shadowStrength,
                                   0.0f, 1.0f, "%.2f");
                ImGui::EndDisabled();
            }
        }

        ImGui::Separator();
        DrawAddComponentMenu(world, session);

        ImGui::End();
    }

    // Saving is the one action that does NOT go through the command queue:
    // it only reads the world, so there is nothing to defer.
    void SaveTo(World& world, const ResourceManager& resources,
                const std::filesystem::path& path, EditorSession& session)
    {
        std::string error;
        if (SaveScene(world, resources, path, error))
        {
            session.scenePath   = path;
            session.sceneStatus = "saved " + ToUtf8(path.filename().wstring());
        }
        else
        {
            session.sceneStatus = "save failed: " + error;
        }
    }

    void DrawMainMenuBar(World& world, ResourceManager& resources,
                         EditorSession& session, DebugUIContext& ui)
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            const bool fileCommandsEnabled = ui.runMode == RunMode::Edit;
            ImGui::BeginDisabled(!fileCommandsEnabled);
            if (ImGui::MenuItem("New"))
            {
                QueueScene(session, Command::Kind::NewScene);
            }

            // Scanned while the menu is open rather than cached: a folder
            // with a handful of scenes costs nothing to re-read, and a stale
            // list is worse than none.
            if (ImGui::BeginMenu("Open"))
            {
                std::error_code error;
                std::filesystem::directory_iterator directory(
                    resources.Paths().SceneDir(), error);
                bool any = false;
                if (!error)
                {
                    for (const auto& item : directory)
                    {
                        std::error_code itemError;
                        if (!item.is_regular_file(itemError) || itemError ||
                            item.path().extension() != L".scene")
                        {
                            continue;
                        }
                        any = true;
                        const std::string label = ToUtf8(item.path().filename().wstring());
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            QueueScene(session, Command::Kind::LoadScene,
                                       item.path().wstring());
                        }
                    }
                }
                if (!any)
                {
                    ImGui::TextDisabled("(no scenes yet)");
                }
                ImGui::EndMenu();
            }

            // Save falls back to Save As when the scene has no file yet -
            // otherwise it would silently do nothing on a brand new scene.
            if (ImGui::MenuItem("Save", nullptr, false))
            {
                if (session.scenePath.empty()) { session.openSaveAs = true; }
                else { SaveTo(world, resources, session.scenePath, session); }
            }
            if (ImGui::MenuItem("Save As..."))
            {
                session.openSaveAs = true;
            }
            ImGui::EndDisabled();
            if (!fileCommandsEnabled)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Scene file commands are unavailable during Play");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            // The only way back from a layout that has been dragged into an
            // unusable state. Without it the fix is to quit, find imgui.ini
            // next to the exe and delete it - which requires knowing that the
            // file exists, where it went, and that it is safe to remove.
            if (ImGui::MenuItem("Reset layout"))
            {
                ui.resetLayoutRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Run"))
        {
            const bool editing = ui.runMode == RunMode::Edit;
            const bool playing = ui.runMode == RunMode::Play;
            if (ImGui::MenuItem("Edit", nullptr, editing, !editing))
            {
                ui.runModeChangeRequested = true;
                ui.requestedRunMode = RunMode::Edit;
            }
            if (ImGui::MenuItem("Play", nullptr, playing, !playing))
            {
                ui.runModeChangeRequested = true;
                ui.requestedRunMode = RunMode::Play;
            }
            ImGui::EndMenu();
        }

        if (!session.sceneStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", session.sceneStatus.c_str());
        }
        if (!session.runStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", session.runStatus.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    // A modal rather than a Win32 file dialog: the scenes folder is a known
    // place, so a name is all that is missing.
    void DrawSaveAsPopup(World& world, const ResourceManager& resources,
                         EditorSession& session, RunMode runMode)
    {
        if (session.openSaveAs)
        {
            ImGui::OpenPopup("Save Scene As");
            session.openSaveAs = false;
        }

        // Centred, because a modal that appears under the cursor is easy to
        // dismiss by accident.
        const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (!ImGui::BeginPopupModal("Save Scene As", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const bool saveAllowed = runMode == RunMode::Edit;
        if (!saveAllowed)
        {
            ImGui::TextDisabled("Saving is unavailable during Play.");
        }
        ImGui::BeginDisabled(!saveAllowed);

        ImGui::TextDisabled("Assets/Scenes/");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        char* name = session.saveAsName.data();
        const bool entered = ImGui::InputText("##scenename", name,
                                              session.saveAsName.size(),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::TextDisabled(".scene");

        // A FILE NAME, not a path. "sub/foo" would save somewhere the Open
        // menu never looks, and "../../foo" would escape the scenes folder
        // entirely - the text box is not a place to accept either.
        const char* problem = nullptr;
        if (name[0] == '\0')
        {
            problem = "enter a name";
        }
        else if (std::strpbrk(name, "/\\:*?\"<>|") != nullptr)
        {
            problem = "no path separators or wildcards";
        }
        else if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
        {
            problem = "not a usable name";
        }

        if (problem)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", problem);
        }

        const bool valid = problem == nullptr;
        ImGui::BeginDisabled(!valid);
        const bool confirmed = ImGui::Button("Save") || (entered && valid);
        ImGui::EndDisabled();

        if (confirmed)
        {
            SaveTo(world, resources,
                   resources.Paths().SceneDir() / (ToWide(name) + L".scene"), session);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Turns a click on the scene image into an entity on the floor.
    //
    // imageMin is the image's top-left in SCREEN coordinates and imageSize
    // its size - the panel's, not the window's and not the render target's.
    void HandleViewportClick(World& world, ResourceManager& resources,
                             AssetBrowser& assets, EditorSession& session,
                             const DebugUIContext& ui,
                             const ImVec2& imageMin, const ImVec2& imageSize)
    {
        // IsItemHovered, not IsWindowHovered: the title bar and the resize
        // grip belong to the window too, and a click on those is not a click
        // in the scene.
        if (!ImGui::IsItemHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            return;
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;

        // Pixels within the image -> clip space. Two conversions in one line,
        // and the Y flip is the one that silently produces a mirrored result
        // if forgotten: ImGui counts pixels DOWN from the top, clip space
        // counts UP from the centre.
        const float ndcX = ((mouse.x - imageMin.x) / imageSize.x) * 2.0f - 1.0f;
        const float ndcY = 1.0f - ((mouse.y - imageMin.y) / imageSize.y) * 2.0f;

        if (!ui.viewportCamera)
        {
            return;
        }
        const Ray ray = RayFromNdc(*ui.viewportCamera, ui.sceneAspect, ndcX, ndcY);

        // Two jobs on one button, split by the arming toggle. Without the
        // mode, placing would make the viewport unclickable for anything
        // else - which is exactly why 10.4 introduced it.
        if (!assets.PlaceOnClick())
        {
            // Selection is NOT a structural edit - it changes no array's
            // shape - so unlike Create/Destroy it can be applied right here.
            // The panels drawn after this one pick it up the same frame.
            Entity picked;
            session.selected =
                PickEntity(world, resources, ray, picked) ? picked : Entity{};
            return;
        }

        XMFLOAT3 hit;
        if (!RayPlaneY(ray, 0.0f, hit))
        {
            return; // clicked the sky
        }

        // "Sphere.obj" -> "Sphere". The extension is noise in an entity list.
        std::string name = assets.SelectedMeshLabel();
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            name.erase(dot);
        }

        QueuePlace(session, hit, assets.SelectedMesh(), assets.SelectedTexture(), name.c_str());
    }

    // The scene, as a texture inside a window. Everything about this panel is
    // driven by the size ImGui gives it - the renderer follows the panel, not
    // the other way round.
    void DrawSceneViewport(World& world, ResourceManager& resources,
                           AssetBrowser& assets, EditorSession& session,
                           DebugUIContext& ui)
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

            // Where the image lands on screen, taken BEFORE drawing it -
            // afterwards the cursor has already moved past.
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();

            // The texture is one frame old in the sense that it was rendered
            // at the PREVIOUS size if the panel was just resized. It is
            // stretched for that one frame, then matches again.
            ImGui::Image(ImTextureID(ui.sceneTexture), size);

            HandleViewportClick(world, resources, assets, session, ui, imageMin, size);
        }

        ui.viewportHovered = ImGui::IsWindowHovered();
        ImGui::End();
    }

    // The shadow pass produces nothing on screen, so without this panel the
    // only way to know it ran at all is a graphics debugger. Temporary in the
    // sense that 11.5 makes the shadows themselves visible - but it stays
    // useful the moment one looks wrong and the question is whether the map
    // or the lookup is at fault.
    void DrawShadowDebug(const DebugUIContext& ui)
    {
        ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Shadow map"))
        {
            ImGui::End();
            return;
        }

        if (ui.shadowTexture == 0)
        {
            ImGui::TextDisabled("no shadow map");
            ImGui::End();
            return;
        }

        ImGui::Text("%ux%u  D32_FLOAT", ui.shadowMapSize, ui.shadowMapSize);
        ImGui::Text("centre %.2f %.2f %.2f", ui.shadowCenter[0], ui.shadowCenter[1],
                    ui.shadowCenter[2]);
        ImGui::Text("radius %.3f", ui.shadowRadius);
        // Depth, not colour: 1.0 is the light's far plane and fills most of
        // the map, so this reads as near-white with faint darker casters.
        // Saying so here stops "it looks blank" from being mistaken for a bug.
        ImGui::TextDisabled("near-white is correct - 1.0 is the far plane");

        const float side = (std::min)(ImGui::GetContentRegionAvail().x,
                                      ImGui::GetContentRegionAvail().y);
        if (side > 0.0f)
        {
            ImGui::Image(ImTextureID(ui.shadowTexture), ImVec2(side, side));
        }
        ImGui::End();
    }

    void DrawStats(DebugUIContext& ui, int entityCount)
    {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(240, 160), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Frame"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("%d fps   %.3f ms", ui.fps, ui.dt * 1000.0f);
        ImGui::Text("Mode: %s", ui.runMode == RunMode::Edit ? "Edit" : "Play");
        if (ui.runMode == RunMode::Play)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f s", ui.playElapsed);
        }

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

        if (!ui.msaa4xSupported)
        {
            ImGui::BeginDisabled();
        }
        bool msaaValue = ui.msaaEnabled;
        if (ImGui::Checkbox("4x MSAA", &msaaValue))
        {
            ui.msaaToggled = true;
        }
        if (!ui.msaa4xSupported)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(unsupported; using 1x)");
        }

        // The scene is no longer the window size, and the difference matters
        // for anything that unprojects a click (10.4).
        ImGui::Separator();
        ImGui::Text("viewport %ux%u", ui.viewportWidth, ui.viewportHeight);
        // Draw items, not entities: placing by hand is what will eventually
        // hit the object constant buffer's ceiling.
        ImGui::Text("%d entities, %u drawn / %u",
                    entityCount, ui.drawItemCount, ui.maxDrawItems);

        // A D3D12 validation error neither throws nor changes the picture -
        // it just means another driver may do something else. Shown here so
        // it is noticed the frame it appears rather than months later.
        if (!ui.hasDebugLayer)
        {
            ImGui::TextDisabled("debug layer: off");
        }
        else if (ui.debugMessages == 0)
        {
            ImGui::TextDisabled("debug layer: 0 messages");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "debug layer: %llu messages",
                               (unsigned long long)ui.debugMessages);
        }

        ImGui::End();
    }
}

// Where each panel starts out, in code rather than in a shipped imgui.ini.
//
// The split fractions are of the node being split at that moment, not of the
// window, which is why the right-hand 0.24 is taken before the left is
// subdivided - reading them as "24% of the whole window" would give the wrong
// picture of the second and third calls.
void BuildDefaultLayout(ImGuiID dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // WorkSize, not Size: the work area is what is left after the main menu
    // bar. Sizing to the full viewport makes the dock space taller than the
    // space it actually occupies, and the panels ride up over the menu.
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);

    // Every split returns the node on the side it cut, and writes the LEFTOVER
    // through its last parameter. That leftover has to be captured - after a
    // split the original id names a parent with children, and docking into a
    // parent puts windows somewhere nobody asked for.
    ImGuiID centre = dockspaceId;
    const ImGuiID right = ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Right, 0.24f, nullptr, &centre);

    ImGuiID leftColumn = ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Left, 0.24f, nullptr, &centre);

    // The left column is three stacked rows rather than tabs, because these
    // three answer different questions and are read together: what the frame
    // costs, what exists in the scene, what can be added to it.
    ImGuiID belowFrame = 0;
    const ImGuiID frameRow = ImGui::DockBuilderSplitNode(
        leftColumn, ImGuiDir_Up, 0.20f, nullptr, &belowFrame);
    ImGuiID entitiesRow = 0;
    const ImGuiID assetsRow = ImGui::DockBuilderSplitNode(
        belowFrame, ImGuiDir_Down, 0.55f, nullptr, &entitiesRow);

    ImGui::DockBuilderDockWindow("Frame", frameRow);
    ImGui::DockBuilderDockWindow("Entities", entitiesRow);
    // Assets takes the largest share of the column - it is the only list long
    // enough to need scrolling.
    ImGui::DockBuilderDockWindow("Assets", assetsRow);

    // Right column: what the selection IS. Shadow map shares the node as a
    // tab rather than a second panel, since it is a diagnostic that should be
    // available without permanently costing screen space.
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Shadow map", right);

    // The Scene keeps the centre, which is also the PassthruCentralNode - so
    // the viewport image is what fills the window rather than a dock node's
    // background.
    ImGui::DockBuilderDockWindow("Scene", centre);

    ImGui::DockBuilderFinish(dockspaceId);
}

void DrawDebugUI(World& world, ResourceManager& resources, AssetBrowser& assets,
                 EditorSession& session, DebugUIContext& ui)
{
    // Before the dock space, which sizes itself around the menu bar.
    DrawMainMenuBar(world, resources, session, ui);
    DrawSaveAsPopup(world, resources, session, ui.runMode);

    // A full-window dock space so the panels can be rearranged and docked.
    const ImGuiID dockspaceId =
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);

    // The dock space alone only makes docking POSSIBLE. Without a default
    // arrangement every panel opens floating at the position its own
    // SetNextWindowPos guessed, and they overlap: the Shadow map panel lands
    // on top of the Inspector and the File menu, and the Inspector sits over
    // the Scene it is describing. Clicking the viewport then raises Scene and
    // hides the Inspector, so selecting an entity hides the thing that shows
    // what was selected.
    //
    // Applied only when there is no layout to respect - a first run, or an
    // explicit reset. Once the user has arranged panels, imgui.ini wins.
    if (ui.applyDefaultLayout)
    {
        BuildDefaultLayout(dockspaceId);
    }

    int entityCount = 0;
    world.ForEachEntity([&](Entity) { ++entityCount; });

    // Before DrawStats, which reports the size this panel just asked for.
    DrawSceneViewport(world, resources, assets, session, ui);
    DrawStats(ui, entityCount);
    DrawEntityList(world, session);
    DrawInspector(world, resources, assets, session);
    DrawShadowDebug(ui);
    assets.Draw();

    // Every panel has finished iterating, so it is finally safe to change
    // the shape of the world - or to replace it entirely.
    ApplyCommands(world, resources, session);
}
