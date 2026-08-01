// World.h : the entity-component registry.
//
// Deliberately the smallest thing that works. No archetypes, no query
// language, no component bitmasks - a dense array per component type plus a
// map from entity to slot. With a few dozen entities that is not a
// compromise, it is the right size; the lesson here is composition over
// inheritance, not cache-optimal iteration.
#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// An entity is an ID, not an object. The generation is what makes a stale
// handle detectable: destroying entity 7 bumps generation 7, so a copy of
// the old Entity{7, 0} no longer matches Entity{7, 1} and IsAlive says no.
struct Entity
{
    static constexpr uint32_t kInvalidIndex = uint32_t(-1);

    uint32_t index      = kInvalidIndex;
    uint32_t generation = 0;

    bool IsValid() const { return index != kInvalidIndex; }
    bool operator==(const Entity& other) const
    {
        return index == other.index && generation == other.generation;
    }
};

namespace ecs_detail
{
    // Type-erased base so World can hold storages of different types.
    class IStorage
    {
    public:
        virtual ~IStorage() = default;
        virtual void RemoveEntity(uint32_t entityIndex) = 0;
    };

    template <typename T>
    class Storage final : public IStorage
    {
    public:
        T& Add(uint32_t entityIndex, const T& value)
        {
            auto it = m_lookup.find(entityIndex);
            if (it != m_lookup.end())
            {
                m_dense[it->second] = value; // adding twice overwrites
                return m_dense[it->second];
            }
            m_lookup.emplace(entityIndex, m_dense.size());
            m_owners.push_back(entityIndex);
            m_dense.push_back(value);
            return m_dense.back();
        }

        T* Get(uint32_t entityIndex)
        {
            auto it = m_lookup.find(entityIndex);
            return it == m_lookup.end() ? nullptr : &m_dense[it->second];
        }

        void RemoveEntity(uint32_t entityIndex) override
        {
            auto it = m_lookup.find(entityIndex);
            if (it == m_lookup.end())
            {
                return;
            }
            // Swap-and-pop: moving the last element into the hole keeps the
            // array dense, so iteration never has to skip gaps. The moved
            // element's lookup entry has to follow it.
            const size_t slot = it->second;
            const size_t last = m_dense.size() - 1;
            if (slot != last)
            {
                m_dense[slot]  = std::move(m_dense[last]);
                m_owners[slot] = m_owners[last];
                m_lookup[m_owners[slot]] = slot;
            }
            m_dense.pop_back();
            m_owners.pop_back();
            m_lookup.erase(entityIndex);
        }

        std::vector<T>&        Data()   { return m_dense; }
        std::vector<uint32_t>& Owners() { return m_owners; }

    private:
        std::vector<T>        m_dense;  // the components themselves
        std::vector<uint32_t> m_owners; // parallel: who owns m_dense[i]
        std::unordered_map<uint32_t, size_t> m_lookup;
    };
}

class World
{
public:
    Entity Create()
    {
        if (!m_freeIndices.empty())
        {
            const uint32_t index = m_freeIndices.back();
            m_freeIndices.pop_back();
            m_alive[index] = true;
            return Entity{ index, m_generations[index] };
        }
        const uint32_t index = uint32_t(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(true);
        return Entity{ index, 0 };
    }

    void Destroy(Entity entity)
    {
        if (!IsAlive(entity))
        {
            return;
        }
        for (auto& [type, storage] : m_storages)
        {
            storage->RemoveEntity(entity.index);
        }
        // Bumping the generation is what invalidates every copy of the old
        // handle, including ones held elsewhere.
        ++m_generations[entity.index];
        m_alive[entity.index] = false;
        m_freeIndices.push_back(entity.index);
    }

    bool IsAlive(Entity entity) const
    {
        return entity.IsValid()
            && entity.index < m_generations.size()
            && m_alive[entity.index]
            && m_generations[entity.index] == entity.generation;
    }

    template <typename T>
    T& Add(Entity entity, const T& value = T{})
    {
        return StorageFor<T>().Add(entity.index, value);
    }

    template <typename T>
    T* Get(Entity entity)
    {
        if (!IsAlive(entity))
        {
            return nullptr;
        }
        return StorageFor<T>().Get(entity.index);
    }

    template <typename T>
    bool Has(Entity entity) { return Get<T>(entity) != nullptr; }

    template <typename T>
    void Remove(Entity entity) { StorageFor<T>().RemoveEntity(entity.index); }

    // Visits every entity carrying T as fn(Entity, T&).
    //
    // Two-component queries are written as "iterate one, Get<> the other" -
    // see the systems. That is one line more at the call site and saves an
    // entire query layer nobody needs at this scale.
    template <typename T, typename Fn>
    void ForEach(Fn&& fn)
    {
        auto& storage = StorageFor<T>();
        auto& data    = storage.Data();
        auto& owners  = storage.Owners();
        for (size_t i = 0; i < data.size(); ++i)
        {
            const uint32_t index = owners[i];
            fn(Entity{ index, m_generations[index] }, data[i]);
        }
    }

private:
    template <typename T>
    ecs_detail::Storage<T>& StorageFor()
    {
        const std::type_index key(typeid(T));
        auto it = m_storages.find(key);
        if (it == m_storages.end())
        {
            it = m_storages.emplace(key, std::make_unique<ecs_detail::Storage<T>>()).first;
        }
        return *static_cast<ecs_detail::Storage<T>*>(it->second.get());
    }

    std::vector<uint32_t> m_generations; // per entity index
    std::vector<bool>     m_alive;
    std::vector<uint32_t> m_freeIndices; // destroyed indices, reused first

    std::unordered_map<std::type_index,
                       std::unique_ptr<ecs_detail::IStorage>> m_storages;
};
