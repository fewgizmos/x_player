#pragma once

#include "X/Engine/Util/Applied/Scene/Type/Transform.hpp"

#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"

#include <vector>
#include <mutex>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

using Model     = ::Engine::Applied::Render::Resources::Models::Model;
using Camera    = ::Engine::Applied::Render::SceneRendering::Resources::Camera;
using Transform = ::Engine::Util::Applied::Scene::Type::Transform;

struct ModelEntry
{
    const Model* Target = nullptr;
    Transform AppliedTransform;
};

// Hold across a complete capture or draw so camera and models belong to the same frame.
std::unique_lock<std::mutex> LockFrame();
void Clear();

void SetCamera(Camera& p_Camera);
void PushModel(const Model& p_Model, const Transform& p_AppliedTransform);

Camera* GetCamera();
const std::vector<ModelEntry>& GetModels();

}
