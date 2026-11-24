#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Timer.hpp"

#include "X/Engine/Applied/Scene/Scene.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolLightPoint.hpp"

#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Applied/Render/SceneRendering/Passes/ModelPass.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"
#include "X/Engine/Base/Viewport/Inputs.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace Engine::Game::SceneTest
{

namespace
{

namespace Scene     = Applied::Scene;
namespace Resources = Applied::Render::Resources;

using ModelInfo       = Resources::Models::ModelCreateInfo;
using CameraComponent = Scene::Volume::SceneVolCameraInstance;
using LightPoint      = Applied::Render::SceneRendering::Resources::LightPoint;
using LightType       = Applied::Render::SceneRendering::Resources::LightType;

namespace Inputs = Base::Viewport::Inputs;
using Key        = Inputs::KeyboardInputCodes;

class ModeledEntity final : public Scene::SceneEntity
{
public:
    ModeledEntity(const ModelInfo& p_Model, glm::vec3 p_Position, glm::vec3 p_Rotation, float p_Scale)
    {
        auto& Volume = GetSceneVolume();
        auto& Transform = Volume.GetWorldTransform();
        Transform.SetPosition(p_Position);
        Transform.SetRotation(p_Rotation);
        Transform.SetScale(glm::vec3(p_Scale));

        Volume.EmplaceComponent<Scene::Volume::SceneVolModelInstance>(p_Model);
    }
};

class LightEntity final : public Scene::SceneEntity
{
public:
    LightEntity(const LightPoint& p_Light, glm::vec3 p_Position, glm::vec3 p_Rotation)
    {
        auto& Volume = GetSceneVolume();
        auto& Transform = Volume.GetWorldTransform();
        Transform.SetPosition(p_Position);
        Transform.SetRotation(p_Rotation);

        Volume.EmplaceComponent<Scene::Volume::SceneVolLightPoint>(p_Light);
    }
};

class CameraEntity final : public Scene::SceneEntity
{
public:
    explicit CameraEntity(glm::vec3 p_Eye) : m_Component(GetSceneVolume().EmplaceComponent<CameraComponent>())
    {
        auto& Camera = m_Component.GetCamera();
        Camera.Position  = p_Eye;
        Camera.Fov       = glm::radians(50.0f);
        Camera.NearPlane = 0.01f;
        Camera.FarPlane  = 10.0f;

        const glm::vec3 Forward = glm::normalize(glm::vec3(0, 0.13f, 0) - p_Eye);
        m_Yaw = std::atan2(Forward.z, Forward.x);
        m_Pitch = std::asin(Forward.y);

        Update();
    }

    void Update()
    {
        auto& Camera = m_Component.GetCamera();
        const auto& Mouse = Inputs::GetMouse();

        if (Mouse.ButtonsInputCodes.IsStateSet(Inputs::MouseInputCodes::Right)) {
            constexpr float Sensitivity = 0.003f;

            m_Yaw += Mouse.DeltaX * Sensitivity;
            m_Pitch = glm::clamp(m_Pitch - Mouse.DeltaY * Sensitivity, -glm::radians(89.0f), glm::radians(89.0f));
        }

        const glm::vec3 Up(0, 1, 0);
        const glm::vec3 Forward(
            std::cos(m_Pitch) * std::cos(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::sin(m_Yaw)
        );

        const glm::vec3 Right = glm::normalize(glm::cross(Forward, Up));

        const glm::vec3 Movement = Forward * (KeyDown(Key::W) - KeyDown(Key::S))
            + Right * (KeyDown(Key::D) - KeyDown(Key::A))
            + Up * (KeyDown(Key::E) - KeyDown(Key::Q));

        if (glm::dot(Movement, Movement) > 0.0f) {
            constexpr float Speed = 1.0f;

            Camera.Position += glm::normalize(Movement) * Speed * Util::Base::Timer::Delta;
        }

        Camera.View = glm::lookAtRH(Camera.Position, Camera.Position + Forward, Up);

        const auto Extent = Base::Viewport::Surface::GetExtent();

        if (Extent.x > 0 && Extent.y > 0) {
            Camera.Aspect = static_cast<float>(Extent.x) / Extent.y;
        }

        Camera.UpdatePerspectiveProjection();
    }

private:
    static float KeyDown(Key p_Key)
    {
        return Inputs::KeyIsPressed(p_Key) || Inputs::KeyIsHeld(p_Key) ? 1.0f : 0.0f;
    }

    CameraComponent& m_Component;
    float m_Yaw;
    float m_Pitch;
};

std::array<ModeledEntity*, 4> m_ModeledEntities{};
std::array<LightEntity*, 3>   m_LightEntities{};
CameraEntity*                 m_Camera = nullptr;

void UpdateCamera()
{
    m_Camera->Update();
}

void Init()
{
    using Vertex = Resources::Meshes::Vertex;

    Resources::Shaders::ShaderCreateInfo Shader;
    Shader.VertexFile   = "Materials/BaseModel.vert";
    Shader.FragmentFile = "Materials/BaseModel.frag";

    auto& Pipeline = Shader.Graphics;
    Applied::Render::SceneRendering::Passes::ModelPass::ConfigureGeometry(Pipeline);
    Pipeline.DepthCompare            = VK_COMPARE_OP_GREATER;
    Pipeline.DynamicFaces            = true;
    Pipeline.PushConstantSize        = 128;
    Pipeline.PushConstantStages      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    Pipeline.SetLayouts              = {Resources::Textures::GetBindlessSetLayout()};
    Pipeline.ModelInstanceTransforms = true;

    Pipeline.VertexBindings = {
        {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, 7 * sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_INSTANCE}
    };
    Pipeline.VertexAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position)},
        {1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, UvX)},
        {2, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Vertex, UvY)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)}
    };

    for (uint32_t I = 0; I < 7; ++I) {
        Pipeline.VertexAttributes.push_back({
            4 + I, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(I * sizeof(glm::vec4))
        });
    }

    Resources::Shaders::ShaderCreateInfo Blended = Shader;
    Blended.Queue = Resources::Shaders::RenderQueue::Blended;
    Blended.Graphics.AlphaBlend = true;
    Blended.Graphics.DepthWrite = false;
    Applied::Render::SceneRendering::Passes::ModelPass::ConfigureTransparency(Blended.Graphics);

    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> ModelShader(std::move(Shader));
    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> BlendedShader(std::move(Blended));

    const std::string& ModelsDir = Configs::Static::Contents::GLTF_MODELS_DIR;

    const ModelInfo CoastalCliff{ModelsDir + "nordic_coastal_cliff_ulejebpfa_mid.glb",                ModelShader, BlendedShader};
    const ModelInfo ForestCliff{ ModelsDir + "nordic_forest_cliff_large_xibsff1_mid.glb",             ModelShader, BlendedShader};
    const ModelInfo Pistol{      ModelsDir + "service_pistol/service_pistol_1k.gltf",                 ModelShader, BlendedShader};
    const ModelInfo Dagger{      ModelsDir + "ornate_medieval_dagger/ornate_medieval_dagger_1k.gltf", ModelShader, BlendedShader};

    m_ModeledEntities = {
        Scene::It::CreateEntity<ModeledEntity>(
            CoastalCliff, glm::vec3(-0.32f, 0.22f, -0.46f), glm::vec3(0.0f), 0.11f
        ),
        Scene::It::CreateEntity<ModeledEntity>(
            ForestCliff, glm::vec3(0.25f, 0.20f, -0.56f), glm::vec3(0.0f), 0.045f
        ),
        Scene::It::CreateEntity<ModeledEntity>(
            Pistol, glm::vec3(-0.165f, 0.050f, 0.32f), glm::vec3(-35.0f, 8.0f, 0.0f), 1.0f
        ),
        Scene::It::CreateEntity<ModeledEntity>(
            Dagger, glm::vec3(0.17f, 0.071f, 0.34f), glm::vec3(-35.0f, -8.0f, -12.0f), 1.0f
        )
    };

    LightPoint KeyLight;
    KeyLight.Type      = LightType::Directional;
    KeyLight.Color     = {1.0f, 0.94f, 0.85f};
    KeyLight.Intensity = 2.8f;

    LightPoint FillLight;
    FillLight.Type      = LightType::Point;
    FillLight.Color     = {0.52f, 0.72f, 1.0f};
    FillLight.Intensity = 0.42f;
    FillLight.Range     = 1.3f;

    LightPoint SkyLight;
    SkyLight.Type           = LightType::Sky;
    SkyLight.Intensity      = 0.24f;
    SkyLight.ShowBackground = false;

    m_LightEntities = {
        Scene::It::CreateEntity<LightEntity>(KeyLight,  glm::vec3(0.0f), glm::vec3(-42.0f, -23.0f, 0.0f)),
        Scene::It::CreateEntity<LightEntity>(FillLight, glm::vec3(0.55f, 0.38f, 0.48f), glm::vec3(0.0f)),
        Scene::It::CreateEntity<LightEntity>(SkyLight,  glm::vec3(0.0f), glm::vec3(0.0f))
    };

    m_Camera = Scene::It::CreateEntity<CameraEntity>(glm::vec3(0.04f, 0.59f, 1.04f));

    Scene::It::SetPlayerEntity(m_Camera);
}

void Cleanup()
{
    Scene::It::SetPlayerEntity(nullptr);
    Scene::It::EraseEntity(m_Camera);

    m_Camera = nullptr;

    for (auto* Entity : m_ModeledEntities) { Scene::It::EraseEntity(Entity); }
    for (auto* Entity : m_LightEntities)     { Scene::It::EraseEntity(Entity); }

    Scene::It::ApplyPendingOperations();
}

const Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_test_init", Init, 15);

const Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterCleanup("scene_test_cleanup", Cleanup, 81);

const Base::JobSystem::DeferredTaskRegistration::Registrar RegisterCameraUpdate(
    Base::Runtime::EngineThreadID::GameTh, "scene_test_update_camera", UpdateCamera, 65);
}

}
