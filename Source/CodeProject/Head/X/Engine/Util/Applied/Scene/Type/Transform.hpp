#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Engine::Util::Applied::Scene::Type
{

class Transform
{
public:
    Transform() = default;

    Transform(glm::vec3 p_Position, glm::vec3 p_Rotation, glm::vec3 p_Scale)
        : m_Position(p_Position), m_Rotation(p_Rotation), m_Scale(p_Scale)
    {
    }

    const glm::vec3& GetPosition() const { return m_Position; }
    const glm::vec3& GetRotation() const { return m_Rotation; }
    const glm::vec3& GetScale() const { return m_Scale; }

    void SetPosition(glm::vec3 p_Position) { m_Position = p_Position; }
    void SetRotation(glm::vec3 p_Rotation) { m_Rotation = p_Rotation; }
    void SetScale(glm::vec3 p_Scale) { m_Scale = p_Scale; }

    void Translate(glm::vec3 p_Translation) { m_Position += p_Translation; }
    void Rotate(glm::vec3 p_Rotation) { m_Rotation += p_Rotation; }
    void Scale(glm::vec3 p_Scale) { m_Scale *= p_Scale; }

    glm::mat4 ToMatrix() const;

private:
    glm::vec3 m_Position = glm::vec3(0.0f);
    glm::vec3 m_Rotation = glm::vec3(0.0f);
    glm::vec3 m_Scale = glm::vec3(1.0f);
};

}
