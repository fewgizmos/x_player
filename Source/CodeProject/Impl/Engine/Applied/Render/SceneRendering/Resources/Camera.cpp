#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/matrix.hpp>

namespace Engine::Applied::Render::SceneRendering::Resources
{

void Camera::Recalculate()
{
    ViewProjection        = Projection * View;
    InverseViewProjection = glm::inverse(ViewProjection);
}

Camera BuildPerspective(
    float p_Fov,
    float p_Aspect,
    float p_NearPlane,
    const glm::mat4& p_Local
)
{
    Camera Result;
    Result.Fov       = p_Fov;
    Result.Aspect    = p_Aspect;
    Result.NearPlane = p_NearPlane;

    Result.View       = glm::inverse(p_Local);
    Result.Projection = glm::perspective(
        Result.Fov,
        Result.Aspect,
        Result.NearPlane,
        Result.FarPlane
    );

    Result.Position = glm::vec3(p_Local[3]);
    Result.Recalculate();

    return Result;
}

Camera BuildOrthographic(
    float p_Size,
    float p_Aspect,
    float p_NearPlane,
    float p_FarPlane,
    const glm::mat4& p_Local
)
{
    Camera Result;
    Result.Aspect           = p_Aspect;
    Result.NearPlane        = p_NearPlane;
    Result.FarPlane         = p_FarPlane;
    Result.OrthographicSize = p_Size;

    const float HalfHeight = p_Size     * 0.5f;
    const float HalfWidth  = HalfHeight * p_Aspect;

    Result.View = glm::inverse(p_Local);
    Result.Projection = glm::ortho(
        -HalfWidth, HalfWidth,
        -HalfHeight, HalfHeight,
        p_NearPlane, p_FarPlane
    );

    Result.Position = glm::vec3(p_Local[3]);
    Result.Recalculate();

    return Result;
}

}
