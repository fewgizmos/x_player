#include "X/Engine/Applied/Scene/Volume/Volume.hpp"

#include <utility>

namespace Engine::Applied::Scene::Volume
{

VolumeComponent::VolumeComponent(
    ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform
)
    : d_LocalTransform(std::move(p_LocalTransform))
{
}

const ::Engine::Util::Applied::Scene::Type::Transform&
VolumeComponent::GetLocalTransform() const
{
    return d_LocalTransform;
}

::Engine::Util::Applied::Scene::Type::Transform&
VolumeComponent::GetLocalTransform()
{
    return d_LocalTransform;
}

const ::Engine::Util::Applied::Scene::Type::Transform&
SceneVolume::GetWorldTransform() const
{
    return m_WorldTransform;
}

::Engine::Util::Applied::Scene::Type::Transform&
SceneVolume::GetWorldTransform()
{
    return m_WorldTransform;
}

const std::vector<std::unique_ptr<VolumeComponent>>&
SceneVolume::GetComponents() const
{
    return m_Components;
}

std::vector<std::unique_ptr<VolumeComponent>>&
SceneVolume::GetComponents()
{
    return m_Components;
}

void SceneVolume::AddComponent(std::unique_ptr<VolumeComponent> p_Component)
{
    m_Components.push_back(std::move(p_Component));
}

}
