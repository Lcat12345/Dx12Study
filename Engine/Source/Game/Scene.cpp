#include "Game/Scene.h"

#include "Core/Common.h"
#include "Core/TextEncoding.h"
#include "Game/Components.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <locale>
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

static bool SerializeSceneImpl(std::ostream& out, World& world,
                               const ResourceManager& resources, std::string& outError)
{
    // 9 significant digits is the shortest that survives a float round trip
    // exactly. The classic locale keeps file and string streams byte-identical
    // even when the editor is running under a locale with decimal commas.
    out.imbue(std::locale::classic());
    out.precision(9);

    out << "scene " << kSceneVersion << "\n";
    bool namesResolved = true;

    // ForEachEntity walks indices in order, so two saves of one scene are
    // byte-identical - an unordered_map's order is not.
    world.ForEachEntity([&](Entity entity) {
        if (!namesResolved)
        {
            return;
        }
        out << "entity\n";

        if (const Name* name = world.Get<Name>(entity))
        {
            out << "  name ";
            WriteQuoted(out, name->value);
            out << "\n";
        }

        if (const Transform* transform = world.Get<Transform>(entity))
        {
            out << "  transform";
            WriteFloat3(out, transform->position);
            WriteFloat3(out, transform->rotation);
            WriteFloat3(out, transform->scale);
            out << "\n";
        }

        if (const MeshRenderer* renderer = world.Get<MeshRenderer>(entity))
        {
            const Material& material = renderer->material;
            const std::wstring& meshName = resources.MeshName(renderer->mesh);
            const std::wstring& textureName = resources.TextureName(material.texture);
            const std::wstring& normalName = resources.TextureName(material.normalTexture);
            if ((renderer->mesh.IsValid() && meshName.empty()) ||
                (material.texture.IsValid() && textureName.empty()) ||
                (material.normalTexture.IsValid() && normalName.empty()))
            {
                outError = "entity " + std::to_string(entity.index) +
                           " contains an asset handle with no registered name";
                namesResolved = false;
                return;
            }
            out << "  meshrenderer mesh ";
            // The NAME, not the handle. Index 3 means nothing next run.
            WriteQuoted(out, ToUtf8(meshName));
            out << " texture ";
            WriteQuoted(out, ToUtf8(textureName));
            out << " albedo " << material.diffuseAlbedo.x << ' ' << material.diffuseAlbedo.y
                 << ' ' << material.diffuseAlbedo.z << ' ' << material.diffuseAlbedo.w
                 << " specular";
            WriteFloat3(out, material.specularColor);
            out << " shininess " << material.shininess;
            // Appended rather than inserted, and read back as OPTIONAL, so a
            // version 2 file - every scene saved before 11.3 - still loads
            // and simply keeps the flat default.
            out << " normal ";
            WriteQuoted(out, ToUtf8(normalName));
            out << " strength " << material.normalStrength
                 << " blend "
                 << (material.blendMode == Material::BlendMode::AlphaBlend
                         ? "alpha" : "opaque")
                 << "\n";
        }

        if (const CameraComponent* lens = world.Get<CameraComponent>(entity))
        {
            out << "  camera " << lens->fovY << ' ' << lens->nearZ << ' '
                 << lens->farZ << "\n";
        }

        if (const Light* light = world.Get<Light>(entity))
        {
            out << "  light "
                 << (light->type == Light::Type::Directional ? "directional" : "point");
            WriteFloat3(out, light->color);
            out << ' ' << light->range << "\n";
        }

        if (const Spin* spin = world.Get<Spin>(entity))
        {
            out << "  spin " << spin->speed << "\n";
        }

        if (world.Has<ActiveCamera>(entity))
        {
            out << "  activecamera\n";
        }

        if (const Environment* environment = world.Get<Environment>(entity))
        {
            const std::wstring& skyboxName = resources.CubeTextureName(environment->skybox);
            if (environment->skybox.IsValid() && skyboxName.empty())
            {
                outError = "entity " + std::to_string(entity.index) +
                           " contains a skybox handle with no registered name";
                namesResolved = false;
                return;
            }
            out << "  environment";
            WriteFloat3(out, environment->ambient);
            // Appended, not a new line: a reader that stops after three
            // numbers still gets a valid Environment, which is exactly what
            // makes this an additive change.
            if (environment->skybox.IsValid())
            {
                out << " skybox ";
                WriteQuoted(out, ToUtf8(skyboxName));
            }
            out << " shadow " << (environment->shadowsEnabled ? 1 : 0)
                 << ' ' << environment->shadowBias
                 << ' ' << environment->shadowStrength;
            out << "\n";
        }
    });

    if (!namesResolved)
    {
        return false;
    }

    if (!out)
    {
        outError = "the scene could not be written completely";
        return false;
    }
    return true;
}

bool SerializeScene(std::ostream& out, World& world,
                    const ResourceManager& resources, std::string& outError)
{
    outError.clear();
    try
    {
        return SerializeSceneImpl(out, world, resources, outError);
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

static bool DeserializeSceneImpl(std::istream& input, ResourceManager& resources,
                                 World& outWorld, std::string& outError)
{
    input.imbue(std::locale::classic());

    // Everything lands here first. outWorld is only touched once the last
    // line has parsed.
    World  loaded;
    Entity current;
    bool   sawVersion = false;
    int    lineNumber = 0;

    std::string line;
    while (std::getline(input, line))
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
            std::string tag, meshName, textureName, normalName;
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

            // Tagged optional tail: v2 ends at shininess, v3/v4 may add the
            // normal map, and v5 adds blend mode. Order is deliberately not
            // significant for hand-edited files, but duplicates are errors.
            bool sawNormal = false;
            bool sawBlend  = false;
            while (in >> tag)
            {
                if (tag == "normal")
                {
                    if (sawNormal)
                    {
                        outError = Where(lineNumber, "duplicate meshrenderer normal map");
                        return false;
                    }
                    sawNormal = true;
                    if (!ReadQuoted(in, normalName) ||
                        !(in >> tag) || tag != "strength" ||
                        !(in >> renderer.material.normalStrength))
                    {
                        outError = Where(lineNumber, "malformed meshrenderer normal map");
                        return false;
                    }
                }
                else if (tag == "blend")
                {
                    if (sawBlend)
                    {
                        outError = Where(lineNumber, "duplicate meshrenderer blend mode");
                        return false;
                    }
                    sawBlend = true;

                    std::string blendMode;
                    if (!(in >> blendMode) || (blendMode != "opaque" && blendMode != "alpha"))
                    {
                        outError = Where(lineNumber,
                                         "meshrenderer blend expects opaque or alpha");
                        return false;
                    }
                    renderer.material.blendMode = blendMode == "alpha"
                                                ? Material::BlendMode::AlphaBlend
                                                : Material::BlendMode::Opaque;
                }
                else
                {
                    outError = Where(lineNumber,
                                     "unexpected '" + tag + "' after meshrenderer");
                    return false;
                }
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
                if (!normalName.empty())
                {
                    renderer.material.normalTexture =
                        resources.LoadTexture(ToWide(normalName));
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

            // Optional tagged tail. v1 has none, v2/v3 may have only skybox,
            // and v4 adds shadow controls. Defaults above are therefore the
            // complete backward-compatibility policy.
            std::string tag;
            bool sawSkybox = false;
            bool sawShadow = false;
            while (in >> tag)
            {
                if (tag == "skybox")
                {
                    if (sawSkybox)
                    {
                        outError = Where(lineNumber, "duplicate 'skybox' after environment");
                        return false;
                    }
                    sawSkybox = true;

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
                else if (tag == "shadow")
                {
                    if (sawShadow)
                    {
                        outError = Where(lineNumber, "duplicate 'shadow' after environment");
                        return false;
                    }
                    sawShadow = true;

                    int enabled = 0;
                    if (!(in >> enabled >> environment.shadowBias >> environment.shadowStrength) ||
                        (enabled != 0 && enabled != 1) ||
                        !std::isfinite(environment.shadowBias) ||
                        environment.shadowBias < Environment::kMinShadowBias ||
                        environment.shadowBias > Environment::kMaxShadowBias ||
                        !std::isfinite(environment.shadowStrength) ||
                        environment.shadowStrength < 0.0f || environment.shadowStrength > 1.0f)
                    {
                        outError = Where(lineNumber,
                                         "shadow expects enabled(0/1), bias in [0,0.02], "
                                         "and strength in [0,1]");
                        return false;
                    }
                    environment.shadowsEnabled = enabled != 0;
                }
                else
                {
                    outError = Where(lineNumber, "unexpected '" + tag + "' after environment");
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
    if (input.bad())
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

bool DeserializeScene(std::istream& input, ResourceManager& resources,
                      World& outWorld, std::string& outError)
{
    outError.clear();
    // The backstop. Opening a file the user picked must never be able to
    // end the process, whatever the file turns out to contain - the parser
    // above converts what it anticipates, this catches what it does not.
    try
    {
        return DeserializeSceneImpl(input, resources, outWorld, outError);
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

bool CaptureSceneSnapshot(World& world, const ResourceManager& resources,
                          std::string& outSnapshot, std::string& outError)
{
    std::ostringstream stream(std::ios::out | std::ios::binary);
    if (!SerializeScene(stream, world, resources, outError))
    {
        return false;
    }
    outSnapshot = stream.str();
    return true;
}

bool RestoreSceneSnapshot(const std::string& snapshot, ResourceManager& resources,
                          World& outWorld, std::string& outError)
{
    std::istringstream stream(snapshot, std::ios::in | std::ios::binary);
    return DeserializeScene(stream, resources, outWorld, outError);
}

bool SaveScene(World& world, const ResourceManager& resources,
               const std::filesystem::path& path, std::string& outError)
{
    outError.clear();
    try
    {
        std::error_code error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                outError = "could not create the scene directory: " + error.message();
                return false;
            }
        }

        // Opening the destination directly would destroy the last good save
        // before the new bytes are complete. Write a sibling and replace only
        // after SerializeScene and close have both succeeded.
        std::filesystem::path tempPath = path;
        tempPath += L".tmp";

        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            outError = "could not open the file for writing";
            return false;
        }
        if (!SerializeScene(file, world, resources, outError))
        {
            file.close();
            std::filesystem::remove(tempPath, error);
            return false;
        }
        file.close();
        if (!file)
        {
            outError = "the file could not be written completely";
            std::filesystem::remove(tempPath, error);
            return false;
        }

        // std::filesystem::rename does not replace an existing target on
        // Windows. MoveFileEx gives the intended same-volume atomic swap.
        if (!MoveFileExW(tempPath.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            outError = "could not replace the existing file (Win32 error " +
                       std::to_string(GetLastError()) + ")";
            std::filesystem::remove(tempPath, error);
            return false;
        }
        return true;
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

bool LoadScene(const std::filesystem::path& path, ResourceManager& resources,
               World& outWorld, std::string& outError)
{
    outError.clear();
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            outError = "could not open the file";
            return false;
        }
        return DeserializeScene(file, resources, outWorld, outError);
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
