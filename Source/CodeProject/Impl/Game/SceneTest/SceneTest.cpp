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
namespace Inputs    = Base::Viewport::Inputs;

using ModelInfo       = Resources::Models::ModelCreateInfo;
using CameraComponent = Scene::Volume::SceneVolCameraInstance;
using LightPoint      = Applied::Render::SceneRendering::Resources::LightPoint;
using LightType       = Applied::Render::SceneRendering::Resources::LightType;
using Key             = Inputs::KeyboardInputCodes;

using Configs::Static::Scene::UnitsPerMeter;

class ModeledEntity final : public Scene::SceneEntity
{
public:
    ModeledEntity(const ModelInfo& p_Model, glm::vec3 p_Position, glm::vec3 p_Rotation, float p_Scale)
    {
        auto& Volume = GetSceneVolume();

        auto& Transform = Volume.GetWorldTransform();
        Transform.SetPosition(p_Position * UnitsPerMeter);
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
        Transform.SetPosition(p_Position * UnitsPerMeter);
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
        Camera.Position  = p_Eye * UnitsPerMeter;
        Camera.Fov       = glm::radians(50.0f);
        Camera.NearPlane = 0.01f * UnitsPerMeter;

        const glm::vec3 Forward = glm::normalize(glm::vec3(0, 0.13f, 0) - p_Eye);
        m_Yaw = std::atan2(Forward.z, Forward.x);
        m_Pitch = std::asin(Forward.y);

        Update();
    }

    void Update()
    {
        auto& Camera = m_Component.GetCamera();
        const auto& Mouse = Inputs::GetMouse();

        if (Mouse.ScrollY != 0.0f) {
            m_MoveSpeed = glm::clamp(m_MoveSpeed * std::pow(1.25f, Mouse.ScrollY), 1.0f / 3.0f, 200.0f);
        }

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
            Camera.Position += glm::normalize(Movement) * (m_MoveSpeed * UnitsPerMeter) * Util::Base::Timer::Delta;
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
    float m_MoveSpeed = 2.0f;
    float m_Yaw;
    float m_Pitch;
};

std::array<ModeledEntity*, 2> m_ModeledEntities{};
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

    const ModelInfo Sponza{ModelsDir + "Sponza/Sponza.gltf",                                    ModelShader, BlendedShader};
    const ModelInfo Dagger{ModelsDir + "ornate_medieval_dagger/ornate_medieval_dagger_1k.gltf", ModelShader, BlendedShader};

    m_ModeledEntities = {
        Scene::It::CreateEntity<ModeledEntity>(
            Sponza, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f
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
    FillLight.Color     = {0.08f, 0.225f, 1.0f};
    FillLight.Intensity = 0.85f;
    FillLight.Range     = 1.3f;

    LightPoint SkyLight;
    SkyLight.Type           = LightType::Sky;
    SkyLight.Intensity      = 0.24f;
    SkyLight.ShowBackground = false;

    m_LightEntities = {
        Scene::It::CreateEntity<LightEntity>(KeyLight,  glm::vec3(0.0f), glm::vec3(-42.0f, -23.0f, 0.0f)),
        Scene::It::CreateEntity<LightEntity>(FillLight, glm::vec3(0.47f, 0.32f, 0.57f), glm::vec3(0.0f)),
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
