#include "X/Engine/Util/Applied/Scene/Transform/ModelApplier.hpp"

namespace Engine::Util::Applied::Scene::Transform::ModelApplier
{

void ApplyLocal(ModelEntry& p_Target, const SceneTransform& p_LocalTransform)
{
    p_Target.AppliedTransform = p_LocalTransform.ToMatrix();
}

void ApplyWorld(ModelEntry& p_Target, const SceneTransform& p_WorldTransform)
{
    p_Target.AppliedTransform = p_WorldTransform.ToMatrix() * p_Target.AppliedTransform;
}

}
