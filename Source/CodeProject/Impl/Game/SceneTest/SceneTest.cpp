#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Timer.hpp"

#include "X/Engine/Applied/Scene/Scene.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolLightPoint.hpp"

#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"
#include "X/Engine/Base/Viewport/Inputs.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <utility>

namespace Engine::Game::SceneTest
{

namespace
{

namespace Scene     = Applied::Scene;
namespace Resources = Applied::Render::Resources;
namespace Inputs    = Base::Viewport::Inputs;

using ModelInfo       = Resources::Models::ModelCreateInfo;
using CameraComponent = Scene::Volume::SceneVolCameraInstance;
using LightPoint      = Applied::Render::Resources::LightPointCreateInfo;
using LightType       = Applied::Render::Resources::LightType;
using Key             = Inputs::KeyboardInputCodes;

using Project::Configs::Static::Scene::UnitsPerMeter;

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

        Volume.EmplaceComponent<Scene::Volume::SceneVolLightPoint>(Util::Applied::ResourceHandle<Resources::LightPoint>(p_Light));
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
    using DrawInstance = Resources::Models::DrawInstance;

    Resources::Shaders::ShaderCreateInfo Shader;
    Shader.VertexFile   = "Materials/StdModel/Geometry.vert";
    Shader.FragmentFile = "Materials/StdModel/Geometry.frag";

    auto& Pipeline = Shader.Graphics;
    Pipeline.ColorFormats = {Project::Configs::Static::Graphics::GBUFFER_ALBEDO_FORMAT, Project::Configs::Static::Graphics::GBUFFER_NORMAL_FORMAT};
    Pipeline.DepthFormat  = Project::Configs::Static::Graphics::DEPTH_FORMAT;
    Pipeline.DynamicFaces = true;

    Pipeline.PushConstantSize   = sizeof(Resources::Models::DrawConstants);
    Pipeline.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    Pipeline.SetLayouts = {Resources::Textures::GetBindlessSetLayout()};

    Pipeline.ModelInstanceTransforms = true;
    Pipeline.VertexBindings = {{1, sizeof(DrawInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};

    for (uint32_t I = 0; I < sizeof(DrawInstance) / sizeof(glm::vec4); ++I) {
        Pipeline.VertexAttributes.push_back({
            4 + I, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(I * sizeof(glm::vec4))
        });
    }

    Resources::Shaders::ShaderCreateInfo Transparent = Shader;
    Transparent.FragmentFile = "Materials/StdModel/Service/Transparent.frag";
    Transparent.Graphics.ColorFormats = {Project::Configs::Static::Graphics::SCENE_COLOR_FORMAT};
    Transparent.Graphics.SetLayouts.push_back(Resources::GetLightSetLayout());
    Transparent.Graphics.AlphaBlend = true;
    Transparent.Graphics.DepthWrite = false;
    Transparent.Queue = Resources::Shaders::RenderQueue::Blended;

    Resources::Shaders::ShaderCreateInfo Lighting;
    Lighting.VertexFile   = "Materials/StdModel/Light.vert";
    Lighting.FragmentFile = "Materials/StdModel/Light.frag";

    auto& LightPipeline = Lighting.Graphics;
    LightPipeline.ColorFormats = {Project::Configs::Static::Graphics::SCENE_COLOR_FORMAT};
    LightPipeline.CullMode     = VK_CULL_MODE_NONE;
    LightPipeline.DepthWrite   = false;
    LightPipeline.SetLayouts   = {
        Resources::Textures::GetBindlessSetLayout(), Resources::GetLightSetLayout(), Resources::GetGeometrySetLayout()
    };
    LightPipeline.AlphaBlend = true;

    LightPipeline.SrcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    LightPipeline.DstColorBlendFactor = VK_BLEND_FACTOR_ONE;

    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> ModelShader(      std::move(Shader));
    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> TransparentShader(std::move(Transparent));
    const Util::Applied::ResourceHandle<Resources::Shaders::Shader> LightShader(      std::move(Lighting));

    const std::string& ModelsDir = Project::Configs::Static::Contents::GLTF_MODELS_DIR;

    const ModelInfo Sponza{ModelsDir + "Sponza/Sponza.gltf",                                    ModelShader, TransparentShader, LightShader};
    const ModelInfo Dagger{ModelsDir + "ornate_medieval_dagger/ornate_medieval_dagger_1k.gltf", ModelShader, TransparentShader, LightShader};

    m_ModeledEntities = {
        Scene::It::CreateEntity<ModeledEntity>(
            Sponza, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f
        ),
        Scene::It::CreateEntity<ModeledEntity>(
            Dagger, glm::vec3(0.17f, 0.071f, 0.34f), glm::vec3(-35.0f, -8.0f, -12.0f), 1.0f
        )
    };

    LightPoint KeyLight;
    KeyLight.Shader    = LightShader;
    KeyLight.Type      = LightType::Directional;
    KeyLight.Color     = {1.0f, 0.94f, 0.85f};
    KeyLight.Intensity = 2.8f;

    LightPoint FillLight;
    FillLight.Shader    = LightShader;
    FillLight.Type      = LightType::Point;
    FillLight.Color     = {0.08f, 0.225f, 1.0f};
    FillLight.Intensity = 0.85f;
    FillLight.Range     = 1.3f;

    LightPoint SkyLight;
    SkyLight.Shader         = LightShader;
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
