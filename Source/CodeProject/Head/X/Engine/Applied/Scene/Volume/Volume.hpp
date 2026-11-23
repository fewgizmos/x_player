#pragma once

#include "X/Engine/Util/Applied/Scene/Type/Transform.hpp"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine::Applied::Scene::Volume
{

class VolumeComponent
{
public:
    explicit VolumeComponent(
        ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform = {}
    );

    virtual ~VolumeComponent() = default;

    const ::Engine::Util::Applied::Scene::Type::Transform& GetLocalTransform() const;
    ::Engine::Util::Applied::Scene::Type::Transform& GetLocalTransform();

protected:
    ::Engine::Util::Applied::Scene::Type::Transform d_LocalTransform;
};

class SceneVolume
{
public:
    using Component = VolumeComponent;

    const ::Engine::Util::Applied::Scene::Type::Transform& GetWorldTransform() const;
    ::Engine::Util::Applied::Scene::Type::Transform& GetWorldTransform();

    const std::vector<std::unique_ptr<VolumeComponent>>& GetComponents() const;
    std::vector<std::unique_ptr<VolumeComponent>>& GetComponents();

    void AddComponent(std::unique_ptr<VolumeComponent> p_Component);

    template<typename ComponentType, typename... ArgumentTypes>
    ComponentType& EmplaceComponent(ArgumentTypes&&... p_Arguments)
    {
        static_assert(std::is_base_of_v<VolumeComponent, ComponentType>);

        auto Component = std::make_unique<ComponentType>(
            std::forward<ArgumentTypes>(p_Arguments)...
        );

        ComponentType& Result = *Component;
        m_Components.push_back(std::move(Component));

        return Result;
    }

private:
    ::Engine::Util::Applied::Scene::Type::Transform m_WorldTransform;
    std::vector<std::unique_ptr<VolumeComponent>> m_Components;
};

}
