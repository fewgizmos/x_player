#include "X/Engine/Util/Applied/Scene/Transform/ModelApplier.hpp"

#include <glm/vec4.hpp>

namespace Engine::Util::Applied::Scene::Transform::ModelApplier
{

void ApplyLocal(ModelEntry& p_Target, const SceneTransform& p_LocalTransform)
{
    p_Target.AppliedTransform = p_LocalTransform;
}

void ApplyWorld(ModelEntry& p_Target, const SceneTransform& p_WorldTransform)
{
    const SceneTransform Local = p_Target.AppliedTransform;
    SceneTransform Result;

    Result.SetPosition(glm::vec3(
        p_WorldTransform.ToMatrix()
            * glm::vec4(Local.GetPosition(), 1.0f)
    ));
    Result.SetRotation(p_WorldTransform.GetRotation() + Local.GetRotation());
    Result.SetScale(p_WorldTransform.GetScale() * Local.GetScale());

    p_Target.AppliedTransform = Result;
}

}
