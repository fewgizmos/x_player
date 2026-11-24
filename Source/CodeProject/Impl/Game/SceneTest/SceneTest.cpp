#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Timer.hpp"

#include "X/Engine/Applied/Scene/Scene.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

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

using ModelInfo = Resources::Models::ModelCreateInfo;
using CameraComponent = Scene::Volume::SceneVolCameraInstance;

namespace Inputs = Base::Viewport::Inputs;
using Key = Inputs::KeyboardInputCodes;

class ModeledEntity final : public Scene::SceneEntity
{
public:
    ModeledEntity(const ModelInfo& p_Model, float p_X, float p_Scale = 1.0f)
    {
        auto& Volume = GetSceneVolume();

        Volume.GetWorldTransform().SetPosition({p_X, 0, 0});

        auto& Model = Volume.EmplaceComponent<Scene::Volume::SceneVolModelInstance>(p_Model);

        Model.GetLocalTransform().SetScale(glm::vec3(p_Scale));
    }
};

class CameraEntity final : public Scene::SceneEntity
{
public:
    explicit CameraEntity(glm::vec3 p_Eye) : m_Component(GetSceneVolume().EmplaceComponent<CameraComponent>())
    {
        auto& Camera = m_Component.GetCamera();
        Camera.Position = p_Eye;
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

std::array<ModeledEntity*, 2> m_ModeledEntitis{};
CameraEntity* m_Camera = nullptr;

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
    Pipeline.ColorFormats       = {VK_FORMAT_R8G8B8A8_SRGB};
    Pipeline.DepthFormat        = VK_FORMAT_D32_SFLOAT;
    Pipeline.DepthCompare       = VK_COMPARE_OP_GREATER;
    Pipeline.DynamicFaces       = true;
    Pipeline.PushConstantSize   = 128;
    Pipeline.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    Pipeline.SetLayouts         = {Resources::Textures::GetBindlessSetLayout()};
    Pipeline.ModelInstanceTransforms = true;

    Pipeline.VertexBindings = {
        {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, 7 * sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_INSTANCE}
    };
    Pipeline.VertexAttributes   = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position)},
        {1, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvX)},
        {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvY)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal)}
    };

    for (uint32_t I = 0; I < 7; ++I) {
        Pipeline.VertexAttributes.push_back({
            4 + I, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(I * sizeof(glm::vec4))
        });
    }

    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> ModelShader(std::move(Shader));
    ModelInfo RockModel{Configs::Static::Contents::GLTF_MODELS_DIR + "Rock/Rock.gltf", ModelShader};
    ModelInfo CliffModel{Configs::Static::Contents::GLTF_MODELS_DIR + "nordic_forest_cliff_large_xibsff1_mid.glb", ModelShader};

    m_ModeledEntitis[0] = Scene::It::CreateEntity<ModeledEntity>(RockModel, -0.32f);
    m_ModeledEntitis[1] = Scene::It::CreateEntity<ModeledEntity>(CliffModel, 0.32f, 0.04f);

    m_Camera = Scene::It::CreateEntity<CameraEntity>(glm::vec3(0.65f, 1.05f, 1.15f));

    Scene::It::SetPlayerEntity(m_Camera);
}

void Cleanup()
{
    Scene::It::SetPlayerEntity(nullptr);
    Scene::It::EraseEntity(m_Camera);

    m_Camera = nullptr;

    for (auto* ModeledEntityEntity : m_ModeledEntitis) { Scene::It::EraseEntity(ModeledEntityEntity); }

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
