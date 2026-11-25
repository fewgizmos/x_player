#pragma once

#include "X/Engine/Applied/Render/Resources/Models.hpp"

#include "X/Engine/Applied/Scene/Volume/Volume.hpp"

namespace Engine::Applied::Scene::Volume
{

class SceneVolModelInstance final : public VolumeComponent
{
public:
    using Model       = ::Engine::Applied::Render::Resources::Models::Model;
    using ModelHandle = ::Engine::Util::Applied::ResourceHandle<Model>;

    explicit SceneVolModelInstance(
        Model::CreateInfo p_CreateInfo,
        ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform = {}
    );

    const ModelHandle& GetModel() const { return m_Model; }
    ModelHandle& GetModel() { return m_Model; }

private:
    ModelHandle m_Model;
};

}
