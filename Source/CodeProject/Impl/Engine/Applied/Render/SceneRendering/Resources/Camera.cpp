#include "X/Engine/Applied/Render/SceneRendering/Resources/Camera.hpp"

#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/matrix.hpp>

namespace Engine::Applied::Render::SceneRendering::Resources
{

void Camera::Recalculate()
{
    ViewProjection        = Projection * View;
    InverseViewProjection = glm::inverse(ViewProjection);
}

void Camera::UpdatePerspectiveProjection()
{
    const float FocalLength = 1.0f / std::tan(Fov * 0.5f);

    Projection = glm::mat4(0.0f);
    Projection[0][0] = FocalLength / Aspect;
    Projection[1][1] = -FocalLength;
    Projection[2][3] = -1.0f;
    Projection[3][2] = NearPlane;

    Recalculate();
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

    Result.View     = glm::inverse(p_Local);
    Result.Position = glm::vec3(p_Local[3]);
    Result.UpdatePerspectiveProjection();

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
    Result.Projection = glm::orthoRH_ZO(
        -HalfWidth, HalfWidth,
        -HalfHeight, HalfHeight,
        p_FarPlane, p_NearPlane
    );

    Result.Projection[1][1] *= -1.0f;

    Result.Position = glm::vec3(p_Local[3]);
    Result.Recalculate();

    return Result;
}

}
