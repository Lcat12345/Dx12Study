#include "Game/BuildWorld.h"

#include "Game/Components.h"

#include <cmath>
#include <cstdio>

using namespace DirectX;

namespace
{
    // Every entity gets one so the editor's list reads as a scene rather
    // than a row of indices.
    void SetName(World& world, Entity entity, const char* name)
    {
        Name value;
        std::snprintf(value.value, Name::kCapacity, "%s", name);
        world.Add<Name>(entity, value);
    }

    // Adding an object is: create an entity, attach what it needs. That is
    // the whole API - there is no Object base class to inherit from and no
    // "kind" enum to extend.
    //
    // Takes a whole Transform rather than position and scale. The earlier
    // version had no way to express rotation, and slipping a parameter in
    // between the two would have silently reinterpreted every existing scale
    // argument - changing the TYPE makes each call site a compile error
    // instead. Fields left out fall back to Transform's own defaults.
    Entity SpawnMesh(World& world, const char* name, MeshHandle mesh,
                     const Material& material, const Transform& transform)
    {
        const Entity entity = world.Create();
        SetName(world, entity, name);
        world.Add<Transform>(entity, transform);
        world.Add<MeshRenderer>(entity, { mesh, material });
        return entity;
    }
}

void BuildEmptyScene(World& world)
{
    const Entity camera = world.Create();
    SetName(world, camera, "Camera");
    world.Add<Transform>(camera, { { 0.0f, 3.5f, -22.0f }, { 0, 0, 0 }, { 1, 1, 1 } });
    world.Add<CameraComponent>(camera, { XM_PIDIV4, 0.1f, 200.0f });
    world.Add<ActiveCamera>(camera);

    const Entity sun = world.Create();
    SetName(world, sun, "Sun");
    world.Add<Transform>(sun, { { 0, 0, 0 }, { -0.8411f, 1.1071f, 0.0f }, { 1, 1, 1 } });
    world.Add<Light>(sun, { Light::Type::Directional, { 0.85f, 0.82f, 0.75f }, 0.0f });

    const Entity environment = world.Create();
    SetName(world, environment, "Environment");
    world.Add<Environment>(environment, { { 0.18f, 0.19f, 0.22f } });
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
    // ResolveMesh, not AddMesh: the recipe behind "#floor" now lives in the
    // ResourceManager so that a saved scene naming it rebuilds the SAME
    // geometry. Two definitions of "#floor" would round-trip to two shapes.
    const MeshHandle floorMesh   = resources.ResolveMesh(L"#floor");
    const MeshHandle cubeMesh    = resources.ResolveMesh(L"#cube");
    const MeshHandle pyramidMesh = resources.ResolveMesh(L"#pyramid");
    const MeshHandle sphereMesh  = resources.LoadMesh(L"Sphere.obj");
    const MeshHandle torusMesh   = resources.LoadMesh(L"Torus.obj");
	// The model now ships as its whole download: the .obj, its .mtl, and the
	// textures both reference, in one folder. A path under Assets/ rather
	// than a bare filename, because the pieces only make sense together.
	//
	// Spelled exactly as it is on disk. Windows would open "Laevat" just as
	// happily, but the resource cache keys on the STRING - and the asset
	// browser asks for the on-disk spelling, so a mismatch would load 24 MB
	// of geometry a second time into a second GPU buffer.
	const MeshHandle laevatMesh  = resources.LoadMesh( L"laevat/laevat.obj", 8.0f );

    // --- floor: rough and wide, barely any highlight ---
    Material floorMaterial;
    floorMaterial.texture       = resources.LoadTexture(L"Floor.png");
    floorMaterial.specularColor = { 0.05f, 0.05f, 0.05f };
    floorMaterial.shininess     = 8.0f;
    SpawnMesh(world, "Floor", floorMesh, floorMaterial, Transform{});

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

            char name[Name::kCapacity];
            std::snprintf(name, sizeof(name), "Crate_%02d", index);

            const Entity cube = SpawnMesh(world, name, cubeMesh, material,
                                          { { x * 8.0f, 1.0f, z * 8.0f } });
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
    SpawnMesh(world, "Backdrop", cubeMesh, wallMaterial,
              { { 0.0f, 3.0f, 20.0f }, { 0, 0, 0 }, { 6.0f, 3.0f, 0.5f } });

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
    SpawnMesh(world, "Pyramid", pyramidMesh, pyramidMaterial,
              { { -6.0f, 2.0f, -13.0f }, { 0, 0, 0 }, { 2.0f, 2.0f, 2.0f } });
    SpawnMesh(world, "Pyramid_squashed", pyramidMesh, pyramidMaterial,
              { { 6.0f, 0.9f, -13.0f }, { 0, 0, 0 }, { 4.0f, 0.8f, 4.0f } });

    // --- loaded models: normals come from the file, so the same shader
    //     that flat-shades the cube produces a continuous gradient ---
    Material sphereMaterial;
    sphereMaterial.texture       = resources.LoadTexture(L"Crate.png");
    sphereMaterial.specularColor = { 0.7f, 0.7f, 0.7f };
    sphereMaterial.shininess     = 96.0f; // tight, glossy highlight
    const Entity sphere = SpawnMesh(world, "Sphere", sphereMesh, sphereMaterial,
                                    { { -7.0f, 3.0f, -4.0f }, { 0, 0, 0 }, { 3.0f, 3.0f, 3.0f } });
    world.Add<Spin>(sphere, { 0.4f }); // the texture makes the spin visible

    // The torus model stands upright (its ring lies in the XY plane), so
    // spinning it around Y actually shows - a flat-lying torus would be
    // rotationally symmetric about that axis and look frozen.
    Material torusMaterial;
    torusMaterial.texture       = resources.LoadTexture(L"Crate.png");
    torusMaterial.diffuseAlbedo = { 0.9f, 0.95f, 1.0f, 1.0f };
    torusMaterial.specularColor = { 0.5f, 0.5f, 0.5f };
    torusMaterial.shininess     = 32.0f;
    const Entity torus = SpawnMesh(world, "Torus", torusMesh, torusMaterial,
                                   { { 7.0f, 3.0f, -4.0f }, { 0, 0, 0 }, { 2.6f, 2.6f, 2.6f } });
    world.Add<Spin>(torus, { 0.6f });

	Material laevatMaterial;
	laevatMaterial.texture = resources.LoadTexture( L"Crate.png" );
	laevatMaterial.diffuseAlbedo = { 0.9f, 0.95f, 1.0f, 1.0f };
	laevatMaterial.specularColor = { 0.5f, 0.5f, 0.5f };
	laevatMaterial.shininess = 32.0f;
	SpawnMesh( world, "Laevat", laevatMesh, laevatMaterial,
		{ { 0.0f, 3.0f, -4.0f }, { 0, 0, 0 }, { 2.6f, 2.6f, 2.6f } } );


    // --- the camera is an entity too ---
    const Entity camera = world.Create();
    SetName(world, camera, "Camera");
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
    SetName(world, sun, "Sun");
    world.Add<Transform>(sun, { { 0, 0, 0 }, { -0.8411f, 1.1071f, 0.0f }, { 1, 1, 1 } });
    world.Add<Light>(sun, { Light::Type::Directional, { 0.85f, 0.82f, 0.75f }, 0.0f });

    // Point: orbits the scene so the falloff is easy to see. The orbit is
    // LightOrbitSystem reading this Spin, not a sine inside the renderer.
    const Entity lamp = world.Create();
    SetName(world, lamp, "Lamp");
    world.Add<Transform>(lamp, { { 14.0f, 4.0f, 0.0f }, { 0, 0, 0 }, { 1, 1, 1 } });
    world.Add<Light>(lamp, { Light::Type::Point, { 1.0f, 0.55f, 0.2f }, 30.0f });
    world.Add<Spin>(lamp, { 0.7f });

    // Ambient stands in for all the bounced light we do not simulate.
    const Entity environment = world.Create();
    SetName(world, environment, "Environment");
    world.Add<Environment>(environment, { { 0.18f, 0.19f, 0.22f } });
}
