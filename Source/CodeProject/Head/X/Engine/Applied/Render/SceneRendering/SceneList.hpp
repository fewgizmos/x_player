#pragma once

#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"
#include "X/Engine/Applied/Render/SceneRendering/Resources/LightPoint.hpp"

#include <glm/mat4x4.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

using Model         = ::Engine::Applied::Render::Resources::Models::Model;
using ModelSnapshot = ::Engine::Applied::Render::Resources::Models::ModelSnapshot;
using Camera        = ::Engine::Applied::Render::SceneRendering::Resources::Camera;
using LightInstance = ::Engine::Applied::Render::SceneRendering::Resources::LightInstance;

struct ModelEntry
{
    ::Engine::Util::Applied::ResourceHandle<Model> Target;

    std::shared_ptr<const ModelSnapshot> Snapshot;

    glm::mat4 AppliedTransform{1.0f};
};

struct Frame
{
    std::optional<Camera> ActiveCamera;

    std::vector<ModelEntry> Models;

    std::vector<LightInstance> Lights;
};

void PublishFrame(std::shared_ptr<const Frame> p_Frame);
std::shared_ptr<const Frame> Acquire();

}
