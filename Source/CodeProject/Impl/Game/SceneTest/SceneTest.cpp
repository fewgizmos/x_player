#include "X/Engine/Applied/Scene/Scene.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"
#include "X/Engine/Base/Viewport/Surface.hpp"
#include "X/Project/Configs/Static.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <array>
#include <chrono>
#include <cstddef>

namespace Engine::Game::SceneTest
{

namespace
{

namespace Scene     = Applied::Scene;
namespace Resources = Applied::Render::Resources;

using ModelInfo = Resources::Models::ModelCreateInfo;
using CameraComponent = Scene::Volume::SceneVolCameraInstance;

using Clock = std::chrono::steady_clock;

class Rock final : public Scene::SceneEntity
{
public:
    Rock(const ModelInfo& p_Model, float p_X)
    {
        auto& Volume = GetSceneVolume();

        Volume.GetWorldTransform().SetPosition({p_X, 0, 0});
        Volume.EmplaceComponent<Scene::Volume::SceneVolModelInstance>(p_Model);
    }
};

class CameraEntity final : public Scene::SceneEntity
{
public:
    explicit CameraEntity(glm::vec3 p_Eye) : m_Component(GetSceneVolume().EmplaceComponent<CameraComponent>())
    {
        auto& Camera = m_Component.GetCamera();
        Camera.Position = p_Eye;
        Camera.View = glm::lookAtRH(p_Eye, glm::vec3(0, 0.13f, 0), glm::vec3(0, 1, 0));
    }

    void SetAspect(float p_Aspect)
    {
        auto& Camera = m_Component.GetCamera();
        Camera.Aspect = p_Aspect;
        Camera.NearPlane = 0.01f;
        Camera.FarPlane = 10.0f;
        Camera.Projection = glm::perspectiveRH_ZO(Camera.Fov, p_Aspect, Camera.FarPlane, Camera.NearPlane);
        Camera.Projection[1][1] *= -1.0f;
        Camera.Recalculate();
    }

private:
    CameraComponent& m_Component;
};

std::array<Rock*, 2>         m_Rocks{};
std::array<CameraEntity*, 2> m_Cameras{};

Clock::time_point m_Start;

void SelectCamera()
{
    const auto Seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - m_Start);
    auto* Camera = m_Cameras[(Seconds.count() / 2) % m_Cameras.size()];
    const auto Extent = Base::Viewport::Surface::GetExtent();

    if (Extent.x > 0 && Extent.y > 0) {
        Camera->SetAspect(static_cast<float>(Extent.x) / Extent.y);
    }

    Scene::It::SetPlayerEntity(Camera);
}

void Init()
{
    using Vertex = Resources::Meshes::Vertex;

    Resources::Shaders::ShaderCreateInfo Shader;
    Shader.VertexFile = "Materials/BaseModel.vert";
    Shader.FragmentFile = "Materials/Rock.frag";

    auto& Pipeline = Shader.Graphics;
    Pipeline.ColorFormats = {VK_FORMAT_R8G8B8A8_SRGB};
    Pipeline.DepthFormat = VK_FORMAT_D32_SFLOAT;
    Pipeline.DepthCompare = VK_COMPARE_OP_GREATER;
    Pipeline.CullMode = VK_CULL_MODE_NONE;
    Pipeline.PushConstantSize = 128;
    Pipeline.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    Pipeline.SetLayouts = {Resources::Textures::GetBindlessSetLayout()};
    Pipeline.VertexBindings = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
    Pipeline.VertexAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position)},
        {1, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvX)},
        {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvY)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)}
    };

    ModelInfo Model{Configs::Static::Contents::GLTF_MODELS_DIR + "Rock/Rock.gltf", Util::Applied::ResourceHandle<Resources::Shaders::Shader>(std::move(Shader))};
    m_Rocks[0] = Scene::It::CreateEntity<Rock>(Model, -0.32f);
    m_Rocks[1] = Scene::It::CreateEntity<Rock>(Model, 0.32f);
    m_Cameras[0] = Scene::It::CreateEntity<CameraEntity>(glm::vec3(0.65f, 1.05f, 1.15f));
    m_Cameras[1] = Scene::It::CreateEntity<CameraEntity>(glm::vec3(0, 0.13f, 1.35f));

    m_Start = Clock::now();

    SelectCamera();
}

void Cleanup()
{
    for (auto* Camera : m_Cameras) { Scene::It::EraseEntity(Camera); }

    for (auto* RockEntity : m_Rocks) { Scene::It::EraseEntity(RockEntity); }

    Scene::It::ApplyPendingOperations();
}

const Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_test_init", Init, 15);

const Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterCleanup("scene_test_cleanup", Cleanup, 81);

const Base::JobSystem::DeferredTaskRegistration::Registrar RegisterCameraSwitch(
    Base::Runtime::EngineThreadID::GameTh, "scene_test_select_camera", SelectCamera, 101);
}

}
