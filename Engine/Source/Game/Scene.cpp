#include "Game/Scene.h"

#include "Core/Common.h"
#include "Game/Components.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace DirectX;

namespace
{
    void WriteQuoted(std::ostream& out, const std::string& text)
    {
        out << '"';
        for (const char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out << '\\';
            }
            out << c;
        }
        out << '"';
    }

    bool ReadQuoted(std::istringstream& in, std::string& out)
    {
        out.clear();
        in >> std::ws;

        char c = 0;
        if (!in.get(c) || c != '"')
        {
            return false;
        }
        while (in.get(c))
        {
            if (c == '\\')
            {
                if (!in.get(c))
                {
                    return false;
                }
                out.push_back(c);
            }
            else if (c == '"')
            {
                return true;
            }
            else
            {
                out.push_back(c);
            }
        }
        return false; // ran off the end of the line with the quote open
    }

    void WriteFloat3(std::ostream& out, const XMFLOAT3& v)
    {
        out << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    }

    bool ReadFloat3(std::istringstream& in, XMFLOAT3& v)
    {
        return bool(in >> v.x >> v.y >> v.z);
    }

    // A keyword the parser recognises but whose arguments are malformed is a
    // hard error, not something to skip: silently dropping half a component
    // is how a "successful" load quietly loses data.
    std::string Where(int lineNumber, const std::string& message)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "line %d: %s", lineNumber, message.c_str());
        return buffer;
    }
}

bool SaveScene(World& world, const ResourceManager& resources,
               const std::filesystem::path& path, std::string& outError)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        outError = "could not open the file for writing";
        return false;
    }

    // 9 significant digits is the shortest that survives a float round trip
    // exactly. Fewer and a position drifts every time the scene is re-saved.
    file.precision(9);

    file << "scene " << kSceneVersion << "\n";

    // ForEachEntity walks indices in order, so two saves of one scene are
    // byte-identical - an unordered_map's order is not.
    world.ForEachEntity([&](Entity entity) {
        file << "entity\n";

        if (const Name* name = world.Get<Name>(entity))
        {
            file << "  name ";
            WriteQuoted(file, name->value);
            file << "\n";
        }

        if (const Transform* transform = world.Get<Transform>(entity))
        {
            file << "  transform";
            WriteFloat3(file, transform->position);
            WriteFloat3(file, transform->rotation);
            WriteFloat3(file, transform->scale);
            file << "\n";
        }

        if (const MeshRenderer* renderer = world.Get<MeshRenderer>(entity))
        {
            const Material& material = renderer->material;
            file << "  meshrenderer mesh ";
            // The NAME, not the handle. Index 3 means nothing next run.
            WriteQuoted(file, ToUtf8(resources.MeshName(renderer->mesh)));
            file << " texture ";
            WriteQuoted(file, ToUtf8(resources.TextureName(material.texture)));
            file << " albedo " << material.diffuseAlbedo.x << ' ' << material.diffuseAlbedo.y
                 << ' ' << material.diffuseAlbedo.z << ' ' << material.diffuseAlbedo.w
                 << " specular";
            WriteFloat3(file, material.specularColor);
            file << " shininess " << material.shininess << "\n";
        }

        if (const CameraComponent* lens = world.Get<CameraComponent>(entity))
        {
            file << "  camera " << lens->fovY << ' ' << lens->nearZ << ' '
                 << lens->farZ << "\n";
        }

        if (const Light* light = world.Get<Light>(entity))
        {
            file << "  light "
                 << (light->type == Light::Type::Directional ? "directional" : "point");
            WriteFloat3(file, light->color);
            file << ' ' << light->range << "\n";
        }

        if (const Spin* spin = world.Get<Spin>(entity))
        {
            file << "  spin " << spin->speed << "\n";
        }

        if (world.Has<ActiveCamera>(entity))
        {
            file << "  activecamera\n";
        }

        if (const Environment* environment = world.Get<Environment>(entity))
        {
            file << "  environment";
            WriteFloat3(file, environment->ambient);
            file << "\n";
        }
    });

    if (!file)
    {
        outError = "the file could not be written completely";
        return false;
    }
    return true;
}

bool LoadScene(const std::filesystem::path& path, ResourceManager& resources,
               World& outWorld, std::string& outError)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        outError = "could not open the file";
        return false;
    }

    // Everything lands here first. outWorld is only touched once the last
    // line has parsed.
    World  loaded;
    Entity current;
    bool   sawVersion = false;
    int    lineNumber = 0;

    std::string line;
    while (std::getline(file, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back(); // written on Windows, possibly read anywhere
        }
        // Notepad and PowerShell both put a UTF-8 BOM at the front. We never
        // write one, but a scene file is meant to be hand-editable, and
        // three invisible bytes turning "scene" into an unknown keyword is a
        // baffling way to fail.
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.erase(0, 3);
        }

        std::istringstream in(line);
        std::string keyword;
        if (!(in >> keyword) || keyword.empty() || keyword[0] == '#')
        {
            continue; // blank or comment
        }

        if (!sawVersion)
        {
            int version = 0;
            if (keyword != "scene" || !(in >> version))
            {
                outError = Where(lineNumber, "expected 'scene <version>' first");
                return false;
            }
            if (version != kSceneVersion)
            {
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer),
                              "scene version %d, but this build reads %d",
                              version, kSceneVersion);
                outError = Where(lineNumber, buffer);
                return false;
            }
            sawVersion = true;
            continue;
        }

        if (keyword == "entity")
        {
            current = loaded.Create();
            continue;
        }

        if (!current.IsValid())
        {
            outError = Where(lineNumber, "component line before any 'entity'");
            return false;
        }

        if (keyword == "name")
        {
            std::string value;
            if (!ReadQuoted(in, value))
            {
                outError = Where(lineNumber, "name expects a quoted string");
                return false;
            }
            Name name;
            std::snprintf(name.value, Name::kCapacity, "%s", value.c_str());
            loaded.Add<Name>(current, name);
        }
        else if (keyword == "transform")
        {
            Transform transform;
            if (!ReadFloat3(in, transform.position) ||
                !ReadFloat3(in, transform.rotation) ||
                !ReadFloat3(in, transform.scale))
            {
                outError = Where(lineNumber, "transform expects 9 numbers");
                return false;
            }
            loaded.Add<Transform>(current, transform);
        }
        else if (keyword == "meshrenderer")
        {
            std::string tag, meshName, textureName;
            MeshRenderer renderer;

            if (!(in >> tag) || tag != "mesh" || !ReadQuoted(in, meshName) ||
                !(in >> tag) || tag != "texture" || !ReadQuoted(in, textureName) ||
                !(in >> tag) || tag != "albedo" ||
                !(in >> renderer.material.diffuseAlbedo.x
                     >> renderer.material.diffuseAlbedo.y
                     >> renderer.material.diffuseAlbedo.z
                     >> renderer.material.diffuseAlbedo.w) ||
                !(in >> tag) || tag != "specular" ||
                !ReadFloat3(in, renderer.material.specularColor) ||
                !(in >> tag) || tag != "shininess" ||
                !(in >> renderer.material.shininess))
            {
                outError = Where(lineNumber, "malformed meshrenderer");
                return false;
            }

            // Name -> handle. This is the whole reason names are stored:
            // ResolveMesh either finds it in the cache, loads the file, or
            // rebuilds the procedural recipe.
            if (!meshName.empty())
            {
                renderer.mesh = resources.ResolveMesh(ToWide(meshName));
                if (!renderer.mesh.IsValid())
                {
                    outError = Where(lineNumber, "unknown mesh '" + meshName + "'");
                    return false;
                }
            }
            if (!textureName.empty())
            {
                renderer.material.texture = resources.LoadTexture(ToWide(textureName));
            }
            loaded.Add<MeshRenderer>(current, renderer);
        }
        else if (keyword == "camera")
        {
            CameraComponent lens;
            if (!(in >> lens.fovY >> lens.nearZ >> lens.farZ))
            {
                outError = Where(lineNumber, "camera expects fov, near, far");
                return false;
            }
            loaded.Add<CameraComponent>(current, lens);
        }
        else if (keyword == "light")
        {
            std::string type;
            Light light;
            if (!(in >> type) || (type != "directional" && type != "point") ||
                !ReadFloat3(in, light.color) || !(in >> light.range))
            {
                outError = Where(lineNumber, "malformed light");
                return false;
            }
            light.type = (type == "directional") ? Light::Type::Directional
                                                 : Light::Type::Point;
            loaded.Add<Light>(current, light);
        }
        else if (keyword == "spin")
        {
            Spin spin;
            if (!(in >> spin.speed))
            {
                outError = Where(lineNumber, "spin expects a speed");
                return false;
            }
            loaded.Add<Spin>(current, spin);
        }
        else if (keyword == "activecamera")
        {
            loaded.Add<ActiveCamera>(current);
        }
        else if (keyword == "environment")
        {
            Environment environment;
            if (!ReadFloat3(in, environment.ambient))
            {
                outError = Where(lineNumber, "environment expects 3 numbers");
                return false;
            }
            loaded.Add<Environment>(current, environment);
        }
        else
        {
            outError = Where(lineNumber, "unknown keyword '" + keyword + "'");
            return false;
        }
    }

    if (!sawVersion)
    {
        outError = "empty file - no 'scene' line";
        return false;
    }

    // Only now, with nothing left that can fail.
    outWorld = std::move(loaded);
    return true;
}
