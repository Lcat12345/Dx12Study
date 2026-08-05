#include "Game/Arena.h"

#include "Core/ProcessLog.h"
#include "Graphics/ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <vector>

using namespace DirectX;

namespace
{
    constexpr std::string_view kArenaMarker = "Arena Runtime";
    constexpr float kPlayerMoveSpeed = 7.0f;
    constexpr float kArenaLimit = 18.0f;

    void SetRuntimeName(World& world, Entity entity, const char* text)
    {
        Name name;
        std::snprintf(name.value, Name::kCapacity, "%s", text);
        world.Add<Name>(entity, name);
    }

    Entity FindArenaEntity(World& world)
    {
        Entity found;
        world.ForEach<Name>([&](Entity entity, Name& name) {
            if (!found.IsValid() && std::string_view(name.value) == kArenaMarker)
            {
                found = entity;
            }
        });
        return found;
    }

    Entity FindArenaPlayer(World& world)
    {
        Entity found;
        world.ForEach<ArenaPlayer>([&](Entity entity, ArenaPlayer&) {
            if (!found.IsValid())
            {
                found = entity;
            }
        });
        return found;
    }

    Entity FindActiveCameraEntity(World& world)
    {
        Entity found;
        world.ForEach<ActiveCamera>([&](Entity entity, ActiveCamera&) {
            if (!found.IsValid())
            {
                found = entity;
            }
        });
        return found;
    }

    float DistanceSquaredXZ(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        const float x = a.x - b.x;
        const float z = a.z - b.z;
        return x * x + z * z;
    }

    float NextUnitFloat(ArenaState& state)
    {
        // Xorshift32 is small, deterministic on every target, and its state
        // fits directly in the runtime component/snapshot-independent loop.
        uint32_t value = state.randomState == 0 ? 1u : state.randomState;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state.randomState = value;
        return float(value & 0x00FFFFFFu) / float(0x01000000u);
    }

    Material ArenaMaterial(const ArenaState& state, const XMFLOAT4& albedo)
    {
        Material material;
        material.texture       = state.colorTexture;
        material.normalTexture = state.normalTexture;
        material.diffuseAlbedo = albedo;
        material.specularColor = { 0.25f, 0.25f, 0.25f };
        material.shininess     = 24.0f;
        return material;
    }

    Entity SpawnEnemy(World& world, ArenaState& state, const XMFLOAT3& position)
    {
        const Entity enemy = world.Create();
        SetRuntimeName(world, enemy, "Arena Enemy");
        world.Add<Transform>(enemy, { position, {}, { 1.2f, 1.2f, 1.2f } });
        world.Add<ArenaEnemy>(enemy);
        world.Add<Health>(enemy, { 40.0f, 40.0f });
        world.Add<ContactDamage>(enemy);
        world.Add<MeshRenderer>(enemy,
            { state.enemyMesh, ArenaMaterial(state, { 0.82f, 0.12f, 0.08f, 1.0f }) });
        ++state.currentEnemyCount;
        ++state.totalEnemyCount;
        return enemy;
    }

    void SpawnRingEnemy(World& world, ArenaState& state,
                        const XMFLOAT3& playerPosition)
    {
        const float angle = XM_2PI * NextUnitFloat(state);
        XMFLOAT3 position = playerPosition;
        position.x += std::cos(angle) * state.spawnRadius;
        position.y = 1.0f;
        position.z += std::sin(angle) * state.spawnRadius;
        SpawnEnemy(world, state, position);
    }

    void SpawnPickup(World& world, const ArenaState& state,
                     const XMFLOAT3& position)
    {
        const Entity pickup = world.Create();
        SetRuntimeName(world, pickup, "XP Pickup");
        Transform transform;
        transform.position = { position.x, 0.45f, position.z };
        transform.scale = { 0.28f, 0.28f, 0.28f };
        world.Add<Transform>(pickup, transform);
        world.Add<XpPickup>(pickup);
        world.Add<MeshRenderer>(pickup,
            { state.pickupMesh, ArenaMaterial(state, { 0.2f, 1.0f, 0.3f, 1.0f }) });
    }

    float CurrentSpawnInterval(const ArenaState& state)
    {
        const float duration = (std::max)(state.spawnCurveSeconds, 0.001f);
        const float t = std::clamp(state.survivalSeconds / duration, 0.0f, 1.0f);
        return state.spawnIntervalStart +
               (state.spawnIntervalEnd - state.spawnIntervalStart) * t;
    }

    void FollowPlayerWithCamera(World& world, const XMFLOAT3& playerPosition)
    {
        Transform* camera = world.Get<Transform>(FindActiveCameraEntity(world));
        if (!camera)
        {
            return;
        }
        camera->position = { playerPosition.x, playerPosition.y + 18.0f,
                             playerPosition.z - 16.0f };
        camera->rotation = { -0.72f, 0.0f, 0.0f };
    }

    void LogArenaSummary(const ArenaStatus& status, std::string_view event)
    {
        std::ostringstream row;
        row.imbue(std::locale::classic());
        row << std::fixed << std::setprecision(1)
            << event
            << " alive=" << (status.playerAlive ? 1 : 0)
            << " hp=" << status.health << '/' << status.maximumHealth
            << " xp=" << status.experience
            << " survival_s=" << status.survivalSeconds
            << " enemies=" << status.currentEnemyCount
            << " total_enemies=" << status.totalEnemyCount;
        ProcessLog::Info(row.str());
    }
}

bool IsArenaScene(World& world)
{
    return FindArenaEntity(world).IsValid();
}

ArenaRuntimeAssets ResolveArenaRuntimeAssets(ResourceManager& resources)
{
    ArenaRuntimeAssets assets;
    assets.capsuleMesh  = resources.ResolveMesh(L"#capsule");
    assets.pickupMesh   = resources.ResolveMesh(L"#sphere");
    assets.colorTexture = resources.DefaultTexture();
    assets.normalTexture = resources.DefaultNormalTexture();
    return assets;
}

bool InitializeArena(World& world, const ArenaRuntimeAssets& assets,
                     const ArenaConfig& config)
{
    const Entity arenaEntity = FindArenaEntity(world);
    if (!arenaEntity.IsValid() || world.Has<ArenaState>(arenaEntity))
    {
        return false;
    }

    ArenaState state;
    state.randomState  = config.seed == 0 ? 1u : config.seed;
    state.maxEnemies   = config.maxEnemies;
    state.spawnRadius  = config.spawnRadius;
    state.spawnCooldown = config.firstSpawnDelay;
    state.enemyMesh    = assets.capsuleMesh;
    state.pickupMesh   = assets.pickupMesh;
    state.colorTexture = assets.colorTexture;
    state.normalTexture = assets.normalTexture;
    ArenaState& liveState = world.Add<ArenaState>(arenaEntity, state);

    const Entity player = world.Create();
    SetRuntimeName(world, player, "Arena Player");
    world.Add<Transform>(player, { { 0.0f, 1.0f, 0.0f }, {}, { 1.25f, 1.25f, 1.25f } });
    world.Add<ArenaPlayer>(player);
    // A non-positive request would make the player start dead, which reads as
    // "the arena failed to initialize" rather than as a bad argument.
    const float playerHealth = config.playerHealth > 0.0f
                             ? config.playerHealth : 100.0f;
    world.Add<Health>(player, { playerHealth, playerHealth });
    world.Add<AttackCooldown>(player);
    world.Add<MeshRenderer>(player,
        { liveState.enemyMesh,
          ArenaMaterial(liveState, { 0.15f, 0.55f, 1.0f, 1.0f }) });

    const uint32_t initialCount = (std::min)(config.initialEnemyCount,
                                             config.maxEnemies);
    for (uint32_t i = 0; i < initialCount; ++i)
    {
        SpawnRingEnemy(world, liveState, world.Get<Transform>(player)->position);
    }
    FollowPlayerWithCamera(world, world.Get<Transform>(player)->position);
    LogArenaSummary(GetArenaStatus(world), "arena_start");
    return true;
}

bool InitializeArenaIfPresent(World& world, ResourceManager& resources,
                              const ArenaConfig& config)
{
    if (!IsArenaScene(world))
    {
        return false;
    }
    return InitializeArena(world, ResolveArenaRuntimeAssets(resources), config);
}

void ArenaGameSystem(World& world, const InputContext& input, float dt)
{
    const Entity arenaEntity = FindArenaEntity(world);
    ArenaState* state = world.Get<ArenaState>(arenaEntity);
    const Entity playerEntity = FindArenaPlayer(world);
    Transform* playerTransform = world.Get<Transform>(playerEntity);
    Health* playerHealth = world.Get<Health>(playerEntity);
    AttackCooldown* attack = world.Get<AttackCooldown>(playerEntity);
    if (!state || !playerTransform || !playerHealth || !attack)
    {
        return;
    }

    dt = (std::max)(dt, 0.0f);
    if (!state->playerAlive)
    {
        FollowPlayerWithCamera(world, playerTransform->position);
        return;
    }

    state->survivalSeconds += dt;
    state->summaryCooldown -= dt;

    XMFLOAT3 move = {};
    if (input.IsDown('W')) move.z += 1.0f;
    if (input.IsDown('S')) move.z -= 1.0f;
    if (input.IsDown('D')) move.x += 1.0f;
    if (input.IsDown('A')) move.x -= 1.0f;
    const float moveLengthSquared = move.x * move.x + move.z * move.z;
    if (moveLengthSquared > 0.0f)
    {
        const float scale = kPlayerMoveSpeed * dt / std::sqrt(moveLengthSquared);
        playerTransform->position.x = std::clamp(
            playerTransform->position.x + move.x * scale, -kArenaLimit, kArenaLimit);
        playerTransform->position.z = std::clamp(
            playerTransform->position.z + move.z * scale, -kArenaLimit, kArenaLimit);
    }
    FollowPlayerWithCamera(world, playerTransform->position);

    state->spawnCooldown -= dt;
    const XMFLOAT3 spawnCenter = playerTransform->position;
    while (state->spawnCooldown <= 0.0f &&
           state->currentEnemyCount < state->maxEnemies)
    {
        SpawnRingEnemy(world, *state, spawnCenter);
        state->spawnCooldown += CurrentSpawnInterval(*state);
    }
    if (state->currentEnemyCount >= state->maxEnemies &&
        state->spawnCooldown <= 0.0f)
    {
        state->spawnCooldown = CurrentSpawnInterval(*state);
    }

    // Spawning can grow the dense Transform/Health storages, so component
    // pointers acquired before it must be resolved again.
    playerTransform = world.Get<Transform>(playerEntity);
    playerHealth    = world.Get<Health>(playerEntity);
    attack          = world.Get<AttackCooldown>(playerEntity);

    // One linear enemy pass performs tracking and contact damage.
    world.ForEach<ArenaEnemy>([&](Entity entity, ArenaEnemy& enemy) {
        Transform* transform = world.Get<Transform>(entity);
        ContactDamage* contact = world.Get<ContactDamage>(entity);
        if (!transform || !contact)
        {
            return;
        }
        const float dx = playerTransform->position.x - transform->position.x;
        const float dz = playerTransform->position.z - transform->position.z;
        const float distanceSquared = dx * dx + dz * dz;
        if (distanceSquared > 0.01f)
        {
            const float distance = std::sqrt(distanceSquared);
            const float step = (std::min)(enemy.moveSpeed * dt, distance);
            transform->position.x += dx / distance * step;
            transform->position.z += dz / distance * step;
        }

        contact->remainingCooldown -= dt;
        if (distanceSquared <= contact->radius * contact->radius &&
            contact->remainingCooldown <= 0.0f)
        {
            playerHealth->current -= contact->amount;
            contact->remainingCooldown = contact->cooldownSeconds;
        }
    });

    // Nearest-enemy search occurs only on an attack pulse, never every frame.
    attack->remainingCooldown -= dt;
    if (attack->remainingCooldown <= 0.0f)
    {
        Entity nearest;
        float nearestDistanceSquared = attack->range * attack->range;
        world.ForEach<ArenaEnemy>([&](Entity entity, ArenaEnemy&) {
            const Transform* transform = world.Get<Transform>(entity);
            const Health* health = world.Get<Health>(entity);
            if (!transform || !health || health->current <= 0.0f)
            {
                return;
            }
            const float distanceSquared =
                DistanceSquaredXZ(transform->position, playerTransform->position);
            if (distanceSquared <= nearestDistanceSquared)
            {
                nearestDistanceSquared = distanceSquared;
                nearest = entity;
            }
        });
        if (Health* targetHealth = world.Get<Health>(nearest))
        {
            targetHealth->current -= attack->damage;
        }
        attack->remainingCooldown = attack->intervalSeconds;
    }

    // Death is explicitly two-phase because World storage is swap-and-pop.
    struct FallenEnemy { Entity entity; XMFLOAT3 position; };
    std::vector<FallenEnemy> fallen;
    world.ForEach<ArenaEnemy>([&](Entity entity, ArenaEnemy&) {
        const Health* health = world.Get<Health>(entity);
        const Transform* transform = world.Get<Transform>(entity);
        if (health && transform && health->current <= 0.0f)
        {
            fallen.push_back({ entity, transform->position });
        }
    });
    const XMFLOAT3 playerPosition = playerTransform->position;
    for (const FallenEnemy& enemy : fallen)
    {
        world.Destroy(enemy.entity);
        SpawnPickup(world, *state, enemy.position);
        if (state->currentEnemyCount > 0)
        {
            --state->currentEnemyCount;
        }
    }

    // Pickup collection and expiry are another single pass plus deferred
    // deletion, so neither swap-and-pop storage can invalidate iteration.
    std::vector<Entity> consumedPickups;
    world.ForEach<XpPickup>([&](Entity entity, XpPickup& pickup) {
        const Transform* transform = world.Get<Transform>(entity);
        if (!transform)
        {
            consumedPickups.push_back(entity);
            return;
        }
        pickup.remainingSeconds -= dt;
        if (DistanceSquaredXZ(transform->position, playerPosition) <=
            pickup.collectRadius * pickup.collectRadius)
        {
            state->experience += pickup.amount;
            consumedPickups.push_back(entity);
        }
        else if (pickup.remainingSeconds <= 0.0f)
        {
            consumedPickups.push_back(entity);
        }
    });
    for (Entity pickup : consumedPickups)
    {
        world.Destroy(pickup);
    }

    // Destroying enemies/pickups uses swap-and-pop in every affected storage.
    // Resolve the player's Health after those structural changes.
    playerHealth = world.Get<Health>(playerEntity);
    if (playerHealth && playerHealth->current <= 0.0f)
    {
        playerHealth->current = 0.0f;
        state->playerAlive = false;
        LogArenaSummary(GetArenaStatus(world), "arena_death");
    }
    else if (state->summaryCooldown <= 0.0f)
    {
        state->summaryCooldown += 1.0f;
        LogArenaSummary(GetArenaStatus(world), "arena_summary");
    }
}

ArenaStatus GetArenaStatus(World& world)
{
    ArenaStatus status;
    const Entity arenaEntity = FindArenaEntity(world);
    const ArenaState* state = world.Get<ArenaState>(arenaEntity);
    if (!state)
    {
        return status;
    }

    status.active            = true;
    status.playerAlive       = state->playerAlive;
    status.survivalSeconds   = state->survivalSeconds;
    status.experience        = state->experience;
    status.currentEnemyCount = state->currentEnemyCount;
    status.totalEnemyCount   = state->totalEnemyCount;
    if (const Health* health = world.Get<Health>(FindArenaPlayer(world)))
    {
        status.health        = health->current;
        status.maximumHealth = health->maximum;
    }
    return status;
}

std::wstring ArenaTitleStatus(World& world)
{
    const ArenaStatus status = GetArenaStatus(world);
    if (!status.active)
    {
        return {};
    }
    wchar_t text[192];
    std::swprintf(text, _countof(text),
                  L"   | Arena %ls  HP %.0f/%.0f  XP %u  %.1fs  enemies %u/%u",
                  status.playerAlive ? L"ALIVE" : L"DEAD",
                  status.health, status.maximumHealth, status.experience,
                  status.survivalSeconds, status.currentEnemyCount,
                  status.totalEnemyCount);
    return text;
}
