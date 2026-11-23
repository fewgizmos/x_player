#include "X/Engine/Applied/Scene/SceneRenderProxy.hpp"

#include "X/Engine/Util/Applied/Scene/Transform/CameraApplier.hpp"
#include "X/Engine/Util/Applied/Scene/Transform/ModelApplier.hpp"

#include "X/Engine/Applied/Scene/Scene.hpp"

#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

namespace Engine::Applied::Scene::RenderProxy
{

namespace
{

using SceneVolume     = ::Engine::Applied::Scene::Volume::SceneVolume;
using VolumeComponent = ::Engine::Applied::Scene::Volume::VolumeComponent;
using CameraInstance  = ::Engine::Applied::Scene::Volume::SceneVolCameraInstance;
using ModelInstance   = ::Engine::Applied::Scene::Volume::SceneVolModelInstance;
using Camera          = ::Engine::Applied::Render::SceneRendering::Resources::Camera;
using Model           = ::Engine::Applied::Render::Resources::Models::Model;
using ModelEntry      = ::Engine::Applied::Render::SceneRendering::SceneList::ModelEntry;

Camera m_FrameCamera;

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh,
    "scene_render_proxy",
    CaptureFrame,
    100
);

void CaptureCamera()
{
    const ::Engine::Applied::Scene::SceneEntity* Player = ::Engine::Applied::Scene::It::GetPlayerEntity();

    if (Player == nullptr) { return; }

    const SceneVolume& Volume = Player->GetSceneVolume();

    for (const std::unique_ptr<VolumeComponent>& Component : Volume.GetComponents()) {
        const auto* CameraComponent = dynamic_cast<const CameraInstance*>(Component.get());

        if (CameraComponent == nullptr) { continue; }

        m_FrameCamera = CameraComponent->GetCamera();

        ::Engine::Util::Applied::Scene::Transform::CameraApplier::ApplyLocal(
            m_FrameCamera,
            CameraComponent->GetLocalTransform()
        );
        ::Engine::Util::Applied::Scene::Transform::CameraApplier::ApplyWorld(
            m_FrameCamera,
            Volume.GetWorldTransform()
        );

        ::Engine::Applied::Render::SceneRendering::SceneList::SetCamera(m_FrameCamera);
        return;
    }
}

void CaptureModels(const SceneVolume& p_Volume)
{
    for (const std::unique_ptr<VolumeComponent>& Component : p_Volume.GetComponents()) {
        const auto* ModelComponent = dynamic_cast<const ModelInstance*>(Component.get());

        if (ModelComponent == nullptr || !ModelComponent->GetModel().IsReady()) {
            continue;
        }

        const Model* ModelValue = ModelComponent->GetModel().Get();

        if (ModelValue == nullptr) { continue; }

        ModelEntry Entry;
        Entry.Target = ModelValue;

        ::Engine::Util::Applied::Scene::Transform::ModelApplier::ApplyLocal(
            Entry,
            ModelComponent->GetLocalTransform()
        );
        ::Engine::Util::Applied::Scene::Transform::ModelApplier::ApplyWorld(
            Entry,
            p_Volume.GetWorldTransform()
        );

        ::Engine::Applied::Render::SceneRendering::SceneList::PushModel(
            *Entry.Target,
            Entry.AppliedTransform
        );
    }
}

}

void CaptureFrame()
{
    const auto Lock = ::Engine::Applied::Render::SceneRendering::SceneList::LockFrame();
    ::Engine::Applied::Render::SceneRendering::SceneList::Clear();
    ::Engine::Applied::Scene::It::ApplyPendingOperations();

    CaptureCamera();

    for (const std::unique_ptr<::Engine::Applied::Scene::SceneEntity>& Entity
        : ::Engine::Applied::Scene::It::GetAllEntities()) {

        if (::Engine::Applied::Scene::It::Service::IsEntityPendingErase(Entity.get())) {
            continue;
        }

        CaptureModels(
            static_cast<const ::Engine::Applied::Scene::SceneEntity&>(*Entity)
                .GetSceneVolume()
        );
    }
}

}
