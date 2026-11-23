#include "X/Engine/Util/Applied/Scene/Type/Transform.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace Engine::Util::Applied::Scene::Type
{

glm::mat4 Transform::ToMatrix() const
{
    glm::mat4 Result = glm::translate(glm::mat4(1.0f), m_Position);

    Result = glm::rotate(Result, glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    Result = glm::rotate(Result, glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    Result = glm::rotate(Result, glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

    return glm::scale(Result, m_Scale);
}

}
