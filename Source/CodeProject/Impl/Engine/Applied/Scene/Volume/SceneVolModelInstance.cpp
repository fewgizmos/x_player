#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"

#include <utility>

namespace Engine::Applied::Scene::Volume
{

SceneVolModelInstance::SceneVolModelInstance(
    Model::CreateInfo p_CreateInfo,
    ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform
)
    : VolumeComponent(std::move(p_LocalTransform)),
      m_Model(std::move(p_CreateInfo))
{
}

}
