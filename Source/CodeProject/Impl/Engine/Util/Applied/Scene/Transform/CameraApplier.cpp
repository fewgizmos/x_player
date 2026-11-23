#include "X/Engine/Util/Applied/Scene/Transform/CameraApplier.hpp"

#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

namespace Engine::Util::Applied::Scene::Transform::CameraApplier
{

namespace
{

void Apply(Camera& p_Target, const SceneTransform& p_Transform)
{
    const glm::mat4 World = p_Transform.ToMatrix();

    p_Target.View = p_Target.View * glm::inverse(World);
    p_Target.Position = glm::vec3(World * glm::vec4(p_Target.Position, 1.0f));
    p_Target.Recalculate();
}

}

void ApplyLocal(Camera& p_Target, const SceneTransform& p_LocalTransform)
{
    Apply(p_Target, p_LocalTransform);
}

void ApplyWorld(Camera& p_Target, const SceneTransform& p_WorldTransform)
{
    Apply(p_Target, p_WorldTransform);
}

}
