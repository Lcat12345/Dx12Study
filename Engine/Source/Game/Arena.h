// Arena.h : the runtime-only low-density survival loop.
#pragma once

#include "Core/World.h"
#include "Game/Components.h"
#include "Game/ExecutionContext.h"

#include <cstdint>
#include <string>

class ResourceManager;

struct ArenaConfig
{
    uint32_t seed              = 0x00C0FFEEu;
    uint32_t initialEnemyCount = 8;
    uint32_t maxEnemies        = 100;
    float    spawnRadius       = 24.0f;
    float    firstSpawnDelay   = 0.85f;
    // The player's starting and maximum health. Raising it is what makes the
    // loop OBSERVABLE at high enemy counts: at 1000 enemies contact damage
    // takes the default 100 down inside a single second, so the once-a-second
    // arena summary never samples a partial health bar and the run ends
    // before any XP is collected.
    float    playerHealth      = 100.0f;
};

struct ArenaRuntimeAssets
{
    MeshHandle    capsuleMesh;
    MeshHandle    pickupMesh;
    TextureHandle colorTexture;
    TextureHandle normalTexture;
};

struct ArenaStatus
{
    bool     active             = false;
    bool     playerAlive        = false;
    float    health             = 0.0f;
    float    maximumHealth      = 0.0f;
    float    survivalSeconds    = 0.0f;
    uint32_t experience         = 0;
    uint32_t currentEnemyCount  = 0;
    uint32_t totalEnemyCount    = 0;
};

// Arena.scene carries only a name marker. This keeps runtime rules out of the
// Scene schema while making both Player and Editor Play discover the mode.
bool IsArenaScene(World& world);
ArenaRuntimeAssets ResolveArenaRuntimeAssets(ResourceManager& resources);
bool InitializeArena(World& world, const ArenaRuntimeAssets& assets,
                     const ArenaConfig& config = {});
bool InitializeArenaIfPresent(World& world, ResourceManager& resources,
                              const ArenaConfig& config = {});

void ArenaGameSystem(World& world, const InputContext& input, float dt);
ArenaStatus GetArenaStatus(World& world);
std::wstring ArenaTitleStatus(World& world);
