#include "Game/Scene.h"

#include "Core/Common.h"
#include "Core/TextEncoding.h"
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

static bool SaveSceneImpl(World& world, const ResourceManager& resources,
                          const std::filesystem::path& path, std::string& outError)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    // Written to a sibling temporary and moved into place at the end.
    // Opening the real file directly truncates it FIRST, so a disk full, a
    // write error or a crash halfway through would destroy the last good
    // save - the one thing a save must never do. The temporary lives in the
    // same directory so the move is a rename within one volume, not a copy.
    //
    // Built by APPENDING to the path, never through path::string(): that
    // converts the native UTF-16 to the ANSI code page and throws on
    // anything it cannot represent - a Korean scene name on an English
    // Windows would have taken the process down here, in the one function
    // whose whole job is not to lose the file.
    std::filesystem::path tempPath = path;
    tempPath += L".tmp";

    std::ofstream file(tempPath, std::ios::binary);
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
            // Appended, not a new line: a reader that stops after three
            // numbers still gets a valid Environment, which is exactly what
            // makes this an additive change.
            if (environment->skybox.IsValid())
            {
                file << " skybox ";
                WriteQuoted(file, ToUtf8(resources.CubeTextureName(environment->skybox)));
            }
            file << "\n";
        }
    });

    file.close(); // flush before asking whether it all landed
    if (!file)
    {
        outError = "the file could not be written completely";
        std::filesystem::remove(tempPath, error);
        return false;
    }

    // The swap. rename replaces an existing file on Windows, so the old
    // scene is only gone once the new one is complete on disk.
    std::filesystem::rename(tempPath, path, error);
    if (error)
    {
        outError = "could not replace the existing file: " + error.message();
        std::filesystem::remove(tempPath, error);
        return false;
    }
    return true;
}

bool SaveScene(World& world, const ResourceManager& resources,
               const std::filesystem::path& path, std::string& outError)
{
    // The same backstop LoadScene has. Losing a save is bad; losing the
    // whole session because a save failed is worse.
    try
    {
        return SaveSceneImpl(world, resources, path, outError);
    }
    catch (const std::exception& e)
    {
        outError = e.what();
        return false;
    }
    catch (...)
    {
        outError = "unknown error while saving";
        return false;
    }
}

static bool LoadSceneImpl(const std::filesystem::path& path, ResourceManager& resources,
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
            // Only a FUTURE version is refused. An older file is missing
            // lines this build knows about, and the components it does not
            // mention keep their defaults - which is exactly right.
            if (version > kSceneVersion)
            {
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer),
                              "scene version %d is newer than this build reads (%d)",
                              version, kSceneVersion);
                outError = Where(lineNumber, buffer);
                return false;
            }
            if (version < 1)
            {
                outError = Where(lineNumber, "scene version must be at least 1");
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
            //
            // Both of these THROW when the file is missing or malformed, and
            // a scene naming an asset that has since been renamed is an
            // ordinary thing to open - not a reason to take the editor down.
            // Turned into an error string here, where the line number is
            // still known.
            try
            {
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
            }
            catch (const std::exception& e)
            {
                outError = Where(lineNumber, "could not load asset: " + std::string(e.what()));
                return false;
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

            // Optional tail, added in scene version 2. A v1 line simply ends
            // here and the skybox stays invalid - the right default.
            std::string tag;
            if (in >> tag)
            {
                if (tag != "skybox")
                {
                    outError = Where(lineNumber, "unexpected '" + tag + "' after environment");
                    return false;
                }
                std::string skyName;
                if (!ReadQuoted(in, skyName))
                {
                    outError = Where(lineNumber, "skybox expects a quoted name");
                    return false;
                }
                try
                {
                    environment.skybox = resources.LoadCubeTexture(ToWide(skyName));
                }
                catch (const std::exception& e)
                {
                    outError = Where(lineNumber,
                                     "could not load skybox: " + std::string(e.what()));
                    return false;
                }
            }
            loaded.Add<Environment>(current, environment);
        }
        else
        {
            outError = Where(lineNumber, "unknown keyword '" + keyword + "'");
            return false;
        }
    }

    // getline stops on end of file AND on a read error, and the two are not
    // the same thing: bad() means the stream broke partway, so what parsed
    // is only part of the scene. Replacing the live world with it would be
    // silent data loss dressed up as a successful load.
    if (file.bad())
    {
        outError = "the file could not be read completely";
        return false;
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

bool LoadScene(const std::filesystem::path& path, ResourceManager& resources,
               World& outWorld, std::string& outError)
{
    // The backstop. Opening a file the user picked must never be able to
    // end the process, whatever the file turns out to contain - the parser
    // above converts what it anticipates, this catches what it does not.
    try
    {
        return LoadSceneImpl(path, resources, outWorld, outError);
    }
    catch (const std::exception& e)
    {
        outError = e.what();
        return false;
    }
    catch (...)
    {
        outError = "unknown error while loading";
        return false;
    }
}
