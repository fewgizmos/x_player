#pragma once

#include "X/Engine/Util/Applied/Scene/Type/Transform.hpp"

#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"

namespace Engine::Util::Applied::Scene::Transform::CameraApplier
{

using Camera = ::Engine::Applied::Render::SceneRendering::Resources::Camera;
using SceneTransform = ::Engine::Util::Applied::Scene::Type::Transform;

void ApplyLocal(Camera& p_Target, const SceneTransform& p_LocalTransform);
void ApplyWorld(Camera& p_Target, const SceneTransform& p_WorldTransform);

}
