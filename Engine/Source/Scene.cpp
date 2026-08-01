#include "Scene.h"

#include "Common.h"
#include "ObjLoader.h"

#include <cmath>

using namespace DirectX;

void BuildScene(ID3D12Device* device, Scene& outScene)
{
    outScene.meshes.clear();
    outScene.objects.clear();

    // Mesh slots, referenced by index from here on. The first three are
    // built from hardcoded vertices; the last two come off disk.
    //
    // These two .obj files are authored in our units already. A model from
    // anywhere else usually is not - CAD and scan exports routinely arrive
    // tens of thousands of units across and far from the origin, which puts
    // them past the 200-unit far plane so they never appear. Run those
    // through FitMeshToSize first:
    //
    //     MeshData m = LoadObj(GetAssetDir() / L"YourModel.obj");
    //     FitMeshToSize(m, 8.0f);
    //     outScene.meshes.push_back(CreateMesh(device, m));
    outScene.meshes.push_back(CreateFloorMesh(device, 40.0f, 20.0f));
    outScene.meshes.push_back(CreateCubeMesh(device));
    outScene.meshes.push_back(CreatePyramidMesh(device));
    outScene.meshes.push_back(CreateMesh(device, LoadObj(GetAssetDir() / L"Sphere.obj")));
    outScene.meshes.push_back(CreateMesh(device, LoadObj(GetAssetDir() / L"Torus.obj")));

    constexpr size_t kFloor   = 0;
    constexpr size_t kCube    = 1;
    constexpr size_t kPyramid = 2;
    constexpr size_t kSphere  = 3;
    constexpr size_t kTorus   = 4;

    // Floor: rough and wide, barely any highlight.
    SceneObject floor;
    floor.meshIndex              = kFloor;
    floor.material.specularColor = { 0.05f, 0.05f, 0.05f };
    floor.material.shininess     = 8.0f;
    outScene.objects.push_back(floor);

    // A 3x3 grid of cubes with the middle left open, so you can walk
    // through the gap and feel the parallax. Materials deliberately differ
    // so the same lights produce visibly different surfaces.
    int index = 0;
    for (int z = -1; z <= 1; ++z)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && z == 0)
            {
                continue;
            }
            SceneObject cube;
            cube.meshIndex = kCube;
            cube.position  = { x * 8.0f, 1.0f, z * 8.0f };
            cube.spinSpeed = 0.3f + 0.15f * float(x + z * 3);

            // Shininess ramps 4 -> 256 across the grid: the low end is a
            // broad dull sheen, the high end a small sharp highlight.
            cube.material.shininess     = 4.0f * std::pow(2.0f, float(index) * 0.75f);
            cube.material.specularColor = { 0.6f, 0.6f, 0.6f };
            outScene.objects.push_back(cube);
            ++index;
        }
    }

    // A wide flat billboard, just to have a big surface catching light.
    SceneObject wall;
    wall.meshIndex              = kCube;
    wall.position               = { 0.0f, 3.0f, 20.0f };
    wall.scale                  = { 6.0f, 3.0f, 0.5f };
    wall.material.diffuseAlbedo = { 1.0f, 0.85f, 0.7f, 1.0f };
    wall.material.specularColor = { 0.4f, 0.4f, 0.4f };
    wall.material.shininess     = 64.0f;
    outScene.objects.push_back(wall);

    // Two pyramids side by side: one untouched, one squashed flat on Y.
    // The squashed one only shades correctly because its normals go through
    // the inverse transpose. Replace gWorldInvTranspose with gWorld in
    // Basic.hlsl and re-run (shaders compile at startup, so no rebuild
    // needed) - the squashed pyramid changes, the plain one and every cube
    // stay exactly the same.
    SceneObject pyramid;
    pyramid.meshIndex              = kPyramid;
    pyramid.position               = { -6.0f, 2.0f, -13.0f };
    pyramid.scale                  = { 2.0f, 2.0f, 2.0f }; // uniform
    pyramid.material.specularColor = { 0.5f, 0.5f, 0.5f };
    pyramid.material.shininess     = 48.0f;
    outScene.objects.push_back(pyramid);

    // Squashed flat: its slanted faces are now nearly horizontal, so their
    // normals must swing to nearly straight up. Using gWorld instead swings
    // them the opposite way and the pyramid shades as if it were still steep.
    SceneObject squashed = pyramid;
    squashed.position = { 6.0f, 0.9f, -13.0f };
    squashed.scale    = { 4.0f, 0.8f, 4.0f };
    outScene.objects.push_back(squashed);

    // --- loaded models ---
    // Normals come from the file, so the same Blinn-Phong shader that
    // flat-shades the cube produces a continuous gradient here.
    // scale is 1: FitMeshToSize already normalized the geometry itself,
    // so the object transform does not need to compensate.
    SceneObject sphere;
    sphere.meshIndex              = kSphere;
    sphere.position               = { -7.0f, 3.0f, -4.0f }; // resting on the floor
    sphere.scale                  = { 3.0f, 3.0f, 3.0f };
    sphere.spinSpeed              = 0.4f; // the texture makes the spin visible
    sphere.material.specularColor = { 0.7f, 0.7f, 0.7f };
    sphere.material.shininess     = 96.0f; // tight, glossy highlight
    outScene.objects.push_back(sphere);

    // The torus model stands upright (its ring lies in the XY plane), so
    // spinning it around Y actually shows - a flat-lying torus would be
    // rotationally symmetric about that axis and look frozen.
    SceneObject torus;
    torus.meshIndex              = kTorus;
    torus.position               = { 7.0f, 3.0f, -4.0f };
    torus.scale                  = { 2.6f, 2.6f, 2.6f };
    torus.spinSpeed              = 0.6f;
    torus.material.diffuseAlbedo = { 0.9f, 0.95f, 1.0f, 1.0f };
    torus.material.specularColor = { 0.5f, 0.5f, 0.5f };
    torus.material.shininess     = 32.0f;
    outScene.objects.push_back(torus);
}
