#pragma once

#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"

#include <glm/mat4x4.hpp>
#include <vector>
#include <mutex>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

using Model     = ::Engine::Applied::Render::Resources::Models::Model;
using Camera    = ::Engine::Applied::Render::SceneRendering::Resources::Camera;

struct ModelEntry
{
    const Model* Target = nullptr;
    glm::mat4 AppliedTransform{1.0f};
};

// Hold across a complete capture or draw so camera and models belong to the same frame.
std::unique_lock<std::mutex> LockFrame();
void Clear();

void SetCamera(Camera& p_Camera);
void PushModel(const Model& p_Model, const glm::mat4& p_AppliedTransform);

Camera* GetCamera();
const std::vector<ModelEntry>& GetModels();

}
