// Scene.h : the World, on disk.
//
// The format is our own, line-based text - not JSON from a vendored library.
// The scene is a list of entities with a fixed set of component types, which
// is simple enough that writing the serializer by hand is the point: it is
// where "why does a format need a version field" and "how do you store a
// float without losing it" stop being abstract.
//
// A line is a keyword and its arguments; indentation is decoration. Strings
// are quoted and UTF-8. Blank lines and lines starting with '#' are ignored.
//
//     scene 1
//     entity
//       name "Crate_01"
//       transform 3 1 -4  0 0 0  1 1 1
//       meshrenderer mesh "#cube" texture "crate.png" ...
//
// The thing that makes it work across runs: a MeshHandle is an index into
// this run's ResourceManager and means nothing in the next one, so what gets
// stored is the NAME the handle was registered under.
#pragma once

#include "Core/World.h"
#include "Graphics/ResourceManager.h"

#include <filesystem>
#include <string>

// Bumped whenever the format changes. Old files stay readable only if
// something can tell them apart, and this is that something.
constexpr int kSceneVersion = 1;

// Writes every live entity, in index order so that saving the same scene
// twice produces the same bytes.
bool SaveScene(World& world, const ResourceManager& resources,
               const std::filesystem::path& path, std::string& outError);

// Parses into a TEMPORARY world and only replaces outWorld once the whole
// file has been read. A file that goes bad halfway must not leave the editor
// holding half a scene.
bool LoadScene(const std::filesystem::path& path, ResourceManager& resources,
               World& outWorld, std::string& outError);
