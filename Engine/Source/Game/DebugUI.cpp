#include "Game/DebugUI.h"

#include "Game/AssetBrowser.h"
#include "Game/BuildWorld.h"
#include "Game/Components.h"
#include "Game/Picking.h"
#include "Game/Scene.h"
#include "Game/Systems.h"

#include "Core/Common.h"
#include "Core/TextEncoding.h"

#include "imgui.h"

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
    // Which entity the inspector is showing. Persisting it across frames is
    // editor state, NOT scene data - it deliberately does not live in a
    // component, because saving the scene should not save what was selected.
    Entity g_selected;

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

    // --- scene file state (editor state, never saved with the scene) ---
    std::filesystem::path g_scenePath;   // empty until saved or opened once
    std::string           g_sceneStatus; // last save/load result, shown in the bar
    bool                  g_openSaveAs = false;

    struct Command
    {
        enum class Kind { Create, Destroy, Duplicate, AddComponent, RemoveComponent, Place,
                          NewScene, LoadScene };

        Kind   kind;
        Entity target;
        Comp   component = Comp::Count; // only for Add/RemoveComponent

        // Place only.
        DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
        MeshHandle        mesh;
        TextureHandle     texture;
        std::string       name;
    };

    std::vector<Command> g_commands;

    void Queue(Command::Kind kind, Entity target, Comp component = Comp::Count)
    {
        Command command;
        command.kind      = kind;
        command.target    = target;
        command.component = component;
        g_commands.push_back(std::move(command));
    }

    void QueuePlace(const XMFLOAT3& position, MeshHandle mesh, TextureHandle texture,
                    const char* name)
    {
        Command command;
        command.kind     = Command::Kind::Place;
        command.position = position;
        command.mesh     = mesh;
        command.texture  = texture;
        command.name     = name;
        g_commands.push_back(std::move(command));
    }

    void QueueScene(Command::Kind kind, const std::wstring& path = {})
    {
        Command command;
        command.kind = kind;
        command.name = ToUtf8(path);
        g_commands.push_back(std::move(command));
    }

    void ApplyCommands(World& world, ResourceManager& resources)
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

                g_selected = placed;
                break;
            }

            case Command::Kind::NewScene:
                // Replacing the world invalidates every handle into it.
                world = World{};
                BuildEmptyScene(world);
                g_selected    = Entity{};
                g_scenePath.clear();
                g_sceneStatus = "new scene";
                break;

            case Command::Kind::LoadScene:
            {
                const std::filesystem::path path = ToWide(command.name);
                World       loaded;
                std::string error;
                if (LoadScene(path, resources, loaded, error))
                {
                    world         = std::move(loaded);
                    g_selected    = Entity{};
                    g_scenePath   = path;
                    g_sceneStatus = "opened " + ToUtf8(path.filename().wstring());
                }
                else
                {
                    // The current scene is untouched - LoadScene built into
                    // a temporary and never got as far as swapping it in.
                    g_sceneStatus = "open failed: " + error;
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
        ImGui::SetNextWindowSize(ImVec2(240, 260), ImGuiCond_FirstUseEver);
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

    void DrawInspector(World& world, const ResourceManager& resources, AssetBrowser& assets)
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

        if (MeshRenderer* renderer = world.Get<MeshRenderer>(g_selected))
        {
            if (ComponentHeader("Mesh Renderer", Comp::MeshRenderer, g_selected))
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
                    if (Transform* transform = world.Get<Transform>(g_selected))
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
                ImGui::DragFloat("Bias", &environment->shadowBias, 0.00001f,
                                 Environment::kMinShadowBias,
                                 Environment::kMaxShadowBias, "%.6f");
                ImGui::SliderFloat("Strength", &environment->shadowStrength,
                                   0.0f, 1.0f, "%.2f");
                ImGui::EndDisabled();
            }
        }

        ImGui::Separator();
        DrawAddComponentMenu(world);

        ImGui::End();
    }

    // Saving is the one action that does NOT go through the command queue:
    // it only reads the world, so there is nothing to defer.
    void SaveTo(World& world, const ResourceManager& resources,
                const std::filesystem::path& path)
    {
        std::string error;
        if (SaveScene(world, resources, path, error))
        {
            g_scenePath   = path;
            g_sceneStatus = "saved " + ToUtf8(path.filename().wstring());
        }
        else
        {
            g_sceneStatus = "save failed: " + error;
        }
    }

    void DrawMainMenuBar(World& world, ResourceManager& resources)
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New"))
            {
                QueueScene(Command::Kind::NewScene);
            }

            // Scanned while the menu is open rather than cached: a folder
            // with a handful of scenes costs nothing to re-read, and a stale
            // list is worse than none.
            if (ImGui::BeginMenu("Open"))
            {
                std::error_code error;
                std::filesystem::directory_iterator directory(GetSceneDir(), error);
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
                            QueueScene(Command::Kind::LoadScene, item.path().wstring());
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
                if (g_scenePath.empty()) { g_openSaveAs = true; }
                else                     { SaveTo(world, resources, g_scenePath); }
            }
            if (ImGui::MenuItem("Save As..."))
            {
                g_openSaveAs = true;
            }
            ImGui::EndMenu();
        }

        if (!g_sceneStatus.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", g_sceneStatus.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    // A modal rather than a Win32 file dialog: the scenes folder is a known
    // place, so a name is all that is missing.
    void DrawSaveAsPopup(World& world, const ResourceManager& resources)
    {
        if (g_openSaveAs)
        {
            ImGui::OpenPopup("Save Scene As");
            g_openSaveAs = false;
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

        static char name[64] = "MyScene";
        ImGui::TextDisabled("Assets/Scenes/");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        const bool entered = ImGui::InputText("##scenename", name, sizeof(name),
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
            SaveTo(world, resources, GetSceneDir() / (ToWide(name) + L".scene"));
            ImGui::CloseCurrentPopup();
        }
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
                             AssetBrowser& assets, const DebugUIContext& ui,
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

        CameraView camera;
        if (!GetActiveCameraView(world, camera))
        {
            return;
        }
        const Ray ray = RayFromNdc(camera, ui.sceneAspect, ndcX, ndcY);

        // Two jobs on one button, split by the arming toggle. Without the
        // mode, placing would make the viewport unclickable for anything
        // else - which is exactly why 10.4 introduced it.
        if (!assets.PlaceOnClick())
        {
            // Selection is NOT a structural edit - it changes no array's
            // shape - so unlike Create/Destroy it can be applied right here.
            // The panels drawn after this one pick it up the same frame.
            Entity picked;
            g_selected = PickEntity(world, resources, ray, picked) ? picked : Entity{};
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

        QueuePlace(hit, assets.SelectedMesh(), assets.SelectedTexture(), name.c_str());
    }

    // The scene, as a texture inside a window. Everything about this panel is
    // driven by the size ImGui gives it - the renderer follows the panel, not
    // the other way round.
    void DrawSceneViewport(World& world, ResourceManager& resources,
                           AssetBrowser& assets, DebugUIContext& ui)
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

            HandleViewportClick(world, resources, assets, ui, imageMin, size);
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

void DrawDebugUI(World& world, ResourceManager& resources, AssetBrowser& assets,
                 DebugUIContext& ui)
{
    // Before the dock space, which sizes itself around the menu bar.
    DrawMainMenuBar(world, resources);
    DrawSaveAsPopup(world, resources);

    // A full-window dock space so the panels can be rearranged and docked.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    int entityCount = 0;
    world.ForEachEntity([&](Entity) { ++entityCount; });

    // Before DrawStats, which reports the size this panel just asked for.
    DrawSceneViewport(world, resources, assets, ui);
    DrawStats(ui, entityCount);
    DrawEntityList(world);
    DrawInspector(world, resources, assets);
    DrawShadowDebug(ui);
    assets.Draw();

    // Every panel has finished iterating, so it is finally safe to change
    // the shape of the world - or to replace it entirely.
    ApplyCommands(world, resources);
}
