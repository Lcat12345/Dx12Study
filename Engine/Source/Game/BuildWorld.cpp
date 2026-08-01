#include "Game/BuildWorld.h"

#include "Game/Components.h"

#include <cmath>

using namespace DirectX;

namespace
{
    // Adding an object is: create an entity, attach what it needs. That is
    // the whole API - there is no Object base class to inherit from and no
    // "kind" enum to extend.
    Entity SpawnMesh(World& world, MeshHandle mesh, const Material& material,
                     const XMFLOAT3& position,
                     const XMFLOAT3& scale = { 1.0f, 1.0f, 1.0f })
    {
        const Entity entity = world.Create();
        world.Add<Transform>(entity, { position, { 0, 0, 0 }, scale });
        world.Add<MeshRenderer>(entity, { mesh, material });
        return entity;
    }
}

void BuildWorld(ResourceManager& resources, World& world)
{
    // Procedural geometry is registered under a name; files are keyed by
    // their filename. Either way a repeat request returns the same handle
    // and does no work.
    //
    // A model authored elsewhere is rarely in our units - CAD and scan
    // exports routinely arrive tens of thousands of units across and far
    // from the origin, past the far plane. Those go through fitToSize:
    //     resources.LoadMesh(L"YourModel.obj", 8.0f);
    const MeshHandle floorMesh   = resources.AddMesh(L"#floor", MakeFloorMeshData(40.0f, 20.0f));
    const MeshHandle cubeMesh    = resources.AddMesh(L"#cube", MakeCubeMeshData());
    const MeshHandle pyramidMesh = resources.AddMesh(L"#pyramid", MakePyramidMeshData());
    const MeshHandle sphereMesh  = resources.LoadMesh(L"Sphere.obj");
    const MeshHandle torusMesh   = resources.LoadMesh(L"Torus.obj");

    // --- floor: rough and wide, barely any highlight ---
    Material floorMaterial;
    floorMaterial.texture       = resources.LoadTexture(L"Floor.png");
    floorMaterial.specularColor = { 0.05f, 0.05f, 0.05f };
    floorMaterial.shininess     = 8.0f;
    SpawnMesh(world, floorMesh, floorMaterial, { 0.0f, 0.0f, 0.0f });

    // --- a 3x3 grid of cubes with the middle left open, so you can walk
    //     through the gap and feel the parallax ---
    int index = 0;
    for (int z = -1; z <= 1; ++z)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && z == 0)
            {
                continue;
            }
            Material material;
            // Every iteration asks for the same file - 8 cache hits.
            material.texture       = resources.LoadTexture(L"Crate.png");
            // Shininess ramps 4 -> 256 across the grid: the low end is a
            // broad dull sheen, the high end a small sharp highlight.
            material.shininess     = 4.0f * std::pow(2.0f, float(index) * 0.75f);
            material.specularColor = { 0.6f, 0.6f, 0.6f };

            const Entity cube = SpawnMesh(world, cubeMesh, material,
                                          { x * 8.0f, 1.0f, z * 8.0f });
            // Rotation is a component, not a field on every object.
            world.Add<Spin>(cube, { 0.3f + 0.15f * float(x + z * 3) });
            ++index;
        }
    }

    // --- a wide flat billboard, just to have a big surface catching light ---
    Material wallMaterial;
    wallMaterial.texture       = resources.LoadTexture(L"Crate.png");
    wallMaterial.diffuseAlbedo = { 1.0f, 0.85f, 0.7f, 1.0f };
    wallMaterial.specularColor = { 0.4f, 0.4f, 0.4f };
    wallMaterial.shininess     = 64.0f;
    SpawnMesh(world, cubeMesh, wallMaterial, { 0.0f, 3.0f, 20.0f }, { 6.0f, 3.0f, 0.5f });

    // --- two pyramids: one untouched, one squashed flat on Y ---
    // The squashed one only shades correctly because its normals go through
    // the inverse transpose. Replace gWorldInvTranspose with gWorld in
    // Basic.hlsl and re-run (shaders compile at startup, so no rebuild
    // needed) - the squashed pyramid changes, the plain one and every cube
    // stay exactly the same.
    Material pyramidMaterial;
    pyramidMaterial.texture       = resources.LoadTexture(L"Crate.png");
    pyramidMaterial.specularColor = { 0.5f, 0.5f, 0.5f };
    pyramidMaterial.shininess     = 48.0f;
    SpawnMesh(world, pyramidMesh, pyramidMaterial,
              { -6.0f, 2.0f, -13.0f }, { 2.0f, 2.0f, 2.0f });
    SpawnMesh(world, pyramidMesh, pyramidMaterial,
              { 6.0f, 0.9f, -13.0f }, { 4.0f, 0.8f, 4.0f });

    // --- loaded models: normals come from the file, so the same shader
    //     that flat-shades the cube produces a continuous gradient ---
    Material sphereMaterial;
    sphereMaterial.texture       = resources.LoadTexture(L"Crate.png");
    sphereMaterial.specularColor = { 0.7f, 0.7f, 0.7f };
    sphereMaterial.shininess     = 96.0f; // tight, glossy highlight
    const Entity sphere = SpawnMesh(world, sphereMesh, sphereMaterial,
                                    { -7.0f, 3.0f, -4.0f }, { 3.0f, 3.0f, 3.0f });
    world.Add<Spin>(sphere, { 0.4f }); // the texture makes the spin visible

    // The torus model stands upright (its ring lies in the XY plane), so
    // spinning it around Y actually shows - a flat-lying torus would be
    // rotationally symmetric about that axis and look frozen.
    Material torusMaterial;
    torusMaterial.texture       = resources.LoadTexture(L"Crate.png");
    torusMaterial.diffuseAlbedo = { 0.9f, 0.95f, 1.0f, 1.0f };
    torusMaterial.specularColor = { 0.5f, 0.5f, 0.5f };
    torusMaterial.shininess     = 32.0f;
    const Entity torus = SpawnMesh(world, torusMesh, torusMaterial,
                                   { 7.0f, 3.0f, -4.0f }, { 2.6f, 2.6f, 2.6f });
    world.Add<Spin>(torus, { 0.6f });

    // --- the camera is an entity too ---
    const Entity camera = world.Create();
    world.Add<Transform>(camera, { { 0.0f, 3.5f, -22.0f }, { 0, 0, 0 }, { 1, 1, 1 } });
    world.Add<CameraComponent>(camera, { XM_PIDIV4, 0.1f, 200.0f });
    world.Add<ActiveCamera>(camera);

    // --- and so are the lights ---
    // Directional: models a source so far away (the sun) that all its rays
    // are parallel, so only the rotation matters.
    //
    // These angles reproduce the direction that used to be hardcoded in the
    // renderer, normalize(0.6, -0.75, 0.3) = (0.5963, -0.7454, 0.2981):
    //   pitch = asin(y)      = asin(-0.7454) = -0.8411
    //   yaw   = atan2(x, z)  = atan2(0.5963, 0.2981) = 1.1071
    const Entity sun = world.Create();
    world.Add<Transform>(sun, { { 0, 0, 0 }, { -0.8411f, 1.1071f, 0.0f }, { 1, 1, 1 } });
    world.Add<Light>(sun, { Light::Type::Directional, { 0.85f, 0.82f, 0.75f }, 0.0f });

    // Point: orbits the scene so the falloff is easy to see. The orbit is
    // LightOrbitSystem reading this Spin, not a sine inside the renderer.
    const Entity lamp = world.Create();
    world.Add<Transform>(lamp, { { 14.0f, 4.0f, 0.0f }, { 0, 0, 0 }, { 1, 1, 1 } });
    world.Add<Light>(lamp, { Light::Type::Point, { 1.0f, 0.55f, 0.2f }, 30.0f });
    world.Add<Spin>(lamp, { 0.7f });

    // Ambient stands in for all the bounced light we do not simulate.
    const Entity environment = world.Create();
    world.Add<Environment>(environment, { { 0.18f, 0.19f, 0.22f } });
}
