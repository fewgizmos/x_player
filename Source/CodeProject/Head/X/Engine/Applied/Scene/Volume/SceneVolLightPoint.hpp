#pragma once

#include "X/Engine/Applied/Render/SceneRendering/Resources/LightPoint.hpp"
#include "X/Engine/Applied/Scene/Volume/Volume.hpp"

namespace Engine::Applied::Scene::Volume
{

class SceneVolLightPoint final : public VolumeComponent
{
public:
    using LightPoint = ::Engine::Applied::Render::SceneRendering::Resources::LightPoint;

    explicit SceneVolLightPoint(
        LightPoint p_Light = {},
        ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform = {}
    );

    const LightPoint& GetLight() const { return m_Light; }
    LightPoint& GetLight() { return m_Light; }

private:
    LightPoint m_Light;
};

}
