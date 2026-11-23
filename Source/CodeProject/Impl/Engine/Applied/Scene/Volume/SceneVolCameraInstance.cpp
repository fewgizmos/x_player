#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"

#include <utility>

namespace Engine::Applied::Scene::Volume
{

SceneVolCameraInstance::SceneVolCameraInstance(
    Camera p_Camera,
    ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform
)
    : VolumeComponent(std::move(p_LocalTransform)),
      m_Camera(std::move(p_Camera))
{
}

}
