#pragma once

#include "X/Project/Configs/Static.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Engine::Applied::Render::SceneRendering::Resources
{

struct Camera
{
    glm::mat4 View                  = glm::mat4(1.0f);
    glm::mat4 Projection            = glm::mat4(1.0f);
    glm::mat4 ViewProjection        = glm::mat4(1.0f);
    glm::mat4 InverseViewProjection = glm::mat4(1.0f);

    glm::vec3 Position = glm::vec3(0.0f);

    float Fov              = 1.04719755f;
    float Aspect           = 1.0f;
    float NearPlane        = 0.1f * Configs::Static::Scene::UnitsPerMeter;
    float FarPlane         = 10000.0f * Configs::Static::Scene::UnitsPerMeter;
    float OrthographicSize = 10.0f * Configs::Static::Scene::UnitsPerMeter;

    void Recalculate();
    void UpdatePerspectiveProjection();
};

Camera BuildPerspective(
    float p_Fov,
    float p_Aspect,
    float p_NearPlane,
    const glm::mat4& p_Local
);

Camera BuildOrthographic(
    float p_Size,
    float p_Aspect,
    float p_NearPlane,
    float p_FarPlane,
    const glm::mat4& p_Local
);

}
