#pragma once

#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"
#include "X/Engine/Applied/Scene/Volume/Volume.hpp"

namespace Engine::Applied::Scene::Volume
{

class SceneVolCameraInstance final : public VolumeComponent
{
public:
    using Camera = ::Engine::Applied::Render::SceneRendering::Resources::Camera;

    explicit SceneVolCameraInstance(
        Camera p_Camera = {},
        ::Engine::Util::Applied::Scene::Type::Transform p_LocalTransform = {}
    );

    const Camera& GetCamera() const { return m_Camera; }
    Camera& GetCamera() { return m_Camera; }

private:
    Camera m_Camera;
};

}
