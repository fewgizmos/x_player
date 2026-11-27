#pragma once

#include "X/Engine/Applied/Scene/Volume/Volume.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine::Applied::Scene
{

class SceneEntity
{
public:
    virtual ~SceneEntity() = default;

    uint64_t GetID() const { return d_ID; }

    const ::Engine::Applied::Scene::Volume::SceneVolume& GetSceneVolume() const
    {
        return d_SceneVolume;
    }

protected:
    ::Engine::Applied::Scene::Volume::SceneVolume& GetSceneVolume()
    {
        return d_SceneVolume;
    }

private:
    inline static std::atomic<uint64_t> d_NextID = 0;

    uint64_t d_ID = d_NextID.fetch_add(1, std::memory_order_relaxed) + 1;

    ::Engine::Applied::Scene::Volume::SceneVolume d_SceneVolume;
};

namespace It
{

namespace Service
{

SceneEntity* PushBuiltEntity(std::unique_ptr<SceneEntity>&& p_Entity);
bool IsEntityPendingErase(const SceneEntity* p_Entity);
void EraseEntitiesMatching(bool (*p_Matches)(const SceneEntity&));

}

template<typename EntityType, typename... ArgumentTypes>
EntityType* CreateEntity(ArgumentTypes&&... p_Arguments)
{
    static_assert(std::is_base_of_v<SceneEntity, EntityType>);

    return static_cast<EntityType*>(Service::PushBuiltEntity(
        std::make_unique<EntityType>(std::forward<ArgumentTypes>(p_Arguments)...)
    ));
}

void EraseEntity(SceneEntity* p_Entity);
void EraseAllEntities();

template<typename EntityType>
void EraseAllEntitiesTyped()
{
    static_assert(std::is_base_of_v<SceneEntity, EntityType>);

    Service::EraseEntitiesMatching([](const SceneEntity& p_Entity) {
        return dynamic_cast<const EntityType*>(&p_Entity) != nullptr;
    });
}

const std::vector<std::unique_ptr<SceneEntity>>& GetAllEntities();

template<typename EntityType>
std::vector<EntityType*> GetAllEntitiesTyped()
{
    static_assert(std::is_base_of_v<SceneEntity, EntityType>);

    std::vector<EntityType*> Result;

    for (const std::unique_ptr<SceneEntity>& Entity : GetAllEntities()) {
        if (Service::IsEntityPendingErase(Entity.get())) { continue; }

        if (auto* TypedEntity = dynamic_cast<EntityType*>(Entity.get())) {
            Result.push_back(TypedEntity);
        }
    }

    return Result;
}

std::size_t GetEntitiesCount();
void ApplyPendingOperations();

void SetPlayerEntity(SceneEntity* p_Entity);
SceneEntity* GetPlayerEntity();

}

}
