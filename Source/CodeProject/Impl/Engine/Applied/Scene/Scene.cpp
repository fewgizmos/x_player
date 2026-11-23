#include "X/Engine/Applied/Scene/Scene.hpp"

#include <algorithm>

namespace Engine::Applied::Scene::It
{

namespace
{

std::vector<std::unique_ptr<SceneEntity>> m_Entities;

std::vector<std::unique_ptr<SceneEntity>> m_PendingCreate;
std::vector<SceneEntity*> m_PendingErase;

SceneEntity* m_PlayerEntity = nullptr;

void CheckPlayerEntityAlive()
{
    if (m_PlayerEntity == nullptr) {
        return;
    }

    const auto IsPlayer = [](const std::unique_ptr<SceneEntity>& p_Entity) {
        return p_Entity.get() == m_PlayerEntity;
    };

    const bool NotErased = std::find_if(m_Entities.begin(), m_Entities.end(), IsPlayer) != m_Entities.end()
        || std::find_if(m_PendingCreate.begin(), m_PendingCreate.end(), IsPlayer) != m_PendingCreate.end();

    if (!NotErased) { m_PlayerEntity = nullptr; }
}

}

namespace Service
{

SceneEntity* PushBuiltEntity(std::unique_ptr<SceneEntity>&& p_Entity)
{
    SceneEntity* Result = p_Entity.get();
    m_PendingCreate.push_back(std::move(p_Entity));

    return Result;
}

bool IsEntityPendingErase(const SceneEntity* p_Entity)
{
    return std::find(m_PendingErase.begin(), m_PendingErase.end(), p_Entity) != m_PendingErase.end();
}

}

void EraseEntity(SceneEntity* p_Entity)
{
    const auto Pending = std::find_if(
        m_PendingCreate.begin(), m_PendingCreate.end(),
        [p_Entity](const std::unique_ptr<SceneEntity>& p_Item) {
            return p_Item.get() == p_Entity;
        }
    );

    if (Pending != m_PendingCreate.end()) {
        m_PendingCreate.erase(Pending);
        CheckPlayerEntityAlive();

        return;
    }

    const bool Exists = std::find_if(
        m_Entities.begin(), m_Entities.end(),
        [p_Entity](const std::unique_ptr<SceneEntity>& p_Item) {
            return p_Item.get() == p_Entity;
        }
    ) != m_Entities.end();

    if (Exists && !Service::IsEntityPendingErase(p_Entity)) { m_PendingErase.push_back(p_Entity); }
}

void EraseAllEntities()
{
    m_PendingCreate.clear();

    for (const std::unique_ptr<SceneEntity>& Entity : m_Entities) {
        if (!Service::IsEntityPendingErase(Entity.get())) { m_PendingErase.push_back(Entity.get()); }
    }

    CheckPlayerEntityAlive();
}

const std::vector<std::unique_ptr<SceneEntity>>& GetAllEntities()
{
    return m_Entities;
}

std::size_t GetEntitiesCount()
{
    return m_Entities.size();
}

void ApplyPendingOperations()
{
    std::erase_if(m_Entities, [](const std::unique_ptr<SceneEntity>& p_Entity) {
        return Service::IsEntityPendingErase(p_Entity.get());
    });

    m_PendingErase.clear();

    for (std::unique_ptr<SceneEntity>& Entity : m_PendingCreate) {
        m_Entities.push_back(std::move(Entity));
    }

    m_PendingCreate.clear();
    CheckPlayerEntityAlive();
}

void SetPlayerEntity(SceneEntity* p_Entity)
{
    m_PlayerEntity = p_Entity;
}

SceneEntity* GetPlayerEntity()
{
    return m_PlayerEntity;
}

}
