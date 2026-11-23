#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

namespace Engine::Applied::Render::SceneRendering::SceneList
{

namespace
{

std::mutex m_FrameMutex;
Camera* m_Camera = nullptr;
std::vector<ModelEntry> m_Models;

}

std::unique_lock<std::mutex> LockFrame()
{
    return std::unique_lock(m_FrameMutex);
}

void Clear()
{
    m_Camera = nullptr;
    m_Models.clear();
}

void SetCamera(Camera& p_Camera)
{
    m_Camera = &p_Camera;
}

void PushModel(const Model& p_Model, const glm::mat4& p_AppliedTransform)
{
    m_Models.push_back({ &p_Model, p_AppliedTransform });
}

Camera* GetCamera()
{
    return m_Camera;
}

const std::vector<ModelEntry>& GetModels()
{
    return m_Models;
}

}
