#pragma once

#include "X/Engine/Util/Applied/Scene/Type/Transform.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

namespace Engine::Util::Applied::Scene::Transform::ModelApplier
{

using ModelEntry = ::Engine::Applied::Render::SceneRendering::SceneList::ModelEntry;
using SceneTransform = ::Engine::Util::Applied::Scene::Type::Transform;

void ApplyLocal(ModelEntry& p_Target, const SceneTransform& p_LocalTransform);
void ApplyWorld(ModelEntry& p_Target, const SceneTransform& p_WorldTransform);

}
