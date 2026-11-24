#include "X/Engine/Applied/Scene/Volume/SceneVolLightPoint.hpp"

#include <utility>

namespace Engine::Applied::Scene::Volume
{

SceneVolLightPoint::SceneVolLightPoint(
    LightPoint p_Light,
    ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform
)
    : VolumeComponent(std::move(p_LocalTransform)),
      m_Light(std::move(p_Light))
{
}

}
