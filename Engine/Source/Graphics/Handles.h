// Handles.h : opaque references to assets the ResourceManager owns.
//
// Their own header because Mesh needs them (a submesh names a texture) and
// ResourceManager needs Mesh. Leaving them in ResourceManager.h made that a
// cycle.
//
// Indices, not pointers. An index survives the storage vector reallocating,
// and it costs 4 bytes in a Material instead of 8-plus-lifetime-questions.
//
// No generation counter yet: nothing is ever unloaded, so a handle cannot go
// stale. When something starts freeing assets, add a generation field here
// and bump it on release - that is what turns a dangling handle into a
// detectable error instead of a silent wrong-mesh.
#pragma once

#include <cstdint>

struct MeshHandle
{
    static constexpr uint32_t kInvalid = uint32_t(-1);
    uint32_t index = kInvalid;
    bool IsValid() const { return index != kInvalid; }
};

struct TextureHandle
{
    static constexpr uint32_t kInvalid = uint32_t(-1);
    uint32_t index = kInvalid;
    bool IsValid() const { return index != kInvalid; }
};

// A separate type from TextureHandle on purpose. A cube SRV and a 2D SRV are
// different view dimensions, and binding one where the shader declares the
// other is a class of bug the compiler can catch here instead.
struct CubeTextureHandle
{
    static constexpr uint32_t kInvalid = uint32_t(-1);
    uint32_t index = kInvalid;
    bool IsValid() const { return index != kInvalid; }
};
