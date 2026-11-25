#include "X/Engine/Applied/Scene/SceneRenderProxy.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Applied/Scene/Transform/CameraApplier.hpp"
#include "X/Engine/Util/Applied/Scene/Transform/ModelApplier.hpp"

#include "X/Engine/Applied/Scene/Scene.hpp"

#include "X/Engine/Applied/Scene/Volume/SceneVolCameraInstance.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolLightPoint.hpp"
#include "X/Engine/Applied/Scene/Volume/SceneVolModelInstance.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include <glm/ext/matrix_transform.hpp>

namespace Engine::Applied::Scene::RenderProxy
{

namespace
{

namespace SceneList = ::Engine::Applied::Render::SceneRendering::SceneList;

using SceneVolume     = ::Engine::Applied::Scene::Volume::SceneVolume;
using VolumeComponent = ::Engine::Applied::Scene::Volume::VolumeComponent;
using CameraInstance  = ::Engine::Applied::Scene::Volume::SceneVolCameraInstance;
using LightComponent  = ::Engine::Applied::Scene::Volume::SceneVolLightPoint;
using ModelInstance   = ::Engine::Applied::Scene::Volume::SceneVolModelInstance;

using Camera          = ::Engine::Applied::Render::SceneRendering::Resources::Camera;
using ModelEntry      = ::Engine::Applied::Render::SceneRendering::SceneList::ModelEntry;
using PunctualLight   = ::Engine::Applied::Render::Resources::Models::PunctualLight;

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh,
    "scene_render_proxy",
    CaptureFrame,
    60
);

void CaptureCamera(SceneList::Frame& p_Frame)
{
    const ::Engine::Applied::Scene::SceneEntity* Player = ::Engine::Applied::Scene::It::GetPlayerEntity();

    if (Player == nullptr) { return; }

    const SceneVolume& Volume = Player->GetSceneVolume();

    for (const std::unique_ptr<VolumeComponent>& Component : Volume.GetComponents()) {
        const auto* CameraComponent = dynamic_cast<const CameraInstance*>(Component.get());

        if (CameraComponent == nullptr) { continue; }

        Camera FrameCamera = CameraComponent->GetCamera();

        ::Engine::Util::Applied::Scene::Transform::CameraApplier::ApplyLocal(
            FrameCamera,
            CameraComponent->GetLocalTransform()
        );

        ::Engine::Util::Applied::Scene::Transform::CameraApplier::ApplyWorld(
            FrameCamera,
            Volume.GetWorldTransform()
        );

        p_Frame.ActiveCamera = FrameCamera;

        return;
    }
}

void CaptureModelLights(SceneList::Frame& p_Frame, const ModelEntry& p_Model)
{
    for (const PunctualLight& Source : p_Model.Snapshot->Lights) {
        p_Frame.Lights.push_back({Source.Light, p_Model.AppliedTransform * Source.NodeTransform});
    }
}

void CaptureVolume(SceneList::Frame& p_Frame, const SceneVolume& p_Volume)
{
    for (const std::unique_ptr<VolumeComponent>& Component : p_Volume.GetComponents()) {
        if (const auto* Light = dynamic_cast<const LightComponent*>(Component.get())) {
            if (Light->GetLight().IsReady() && Light->GetLight()->GetInfo().Enabled) {
                p_Frame.Lights.push_back({
                    Light->GetLight(), p_Volume.GetWorldTransform().ToMatrix() * Light->GetLocalTransform().ToMatrix()
                });
            }

            continue;
        }

        const auto* ModelComponent = dynamic_cast<const ModelInstance*>(Component.get());

        if (ModelComponent == nullptr || !ModelComponent->GetModel().IsReady()) { continue; }

        ModelEntry Entry;
        Entry.Target   = ModelComponent->GetModel();
        Entry.Snapshot = Entry.Target->GetSnapshot();

        if (!Entry.Snapshot) { continue; }

        ::Engine::Util::Applied::Scene::Transform::ModelApplier::ApplyLocal(
            Entry,
            ModelComponent->GetLocalTransform()
        );

        ::Engine::Util::Applied::Scene::Transform::ModelApplier::ApplyWorld(
            Entry,
            p_Volume.GetWorldTransform()
        );

        Entry.AppliedTransform = glm::scale(Entry.AppliedTransform, glm::vec3(Configs::Static::Scene::UnitsPerMeter));
        CaptureModelLights(p_Frame, Entry);

        if (!Entry.Snapshot->Parts.empty()) { p_Frame.Models.push_back(std::move(Entry)); }
    }
}

}

void CaptureFrame()
{
    std::shared_ptr<SceneList::Frame> Frame = std::make_shared<SceneList::Frame>();

    ::Engine::Applied::Scene::It::ApplyPendingOperations();

    CaptureCamera(*Frame);

    for (const std::unique_ptr<::Engine::Applied::Scene::SceneEntity>& Entity
        : ::Engine::Applied::Scene::It::GetAllEntities()) {

        if (::Engine::Applied::Scene::It::Service::IsEntityPendingErase(Entity.get())) { continue; }

        CaptureVolume(
            *Frame,
            static_cast<const ::Engine::Applied::Scene::SceneEntity&>(*Entity)
                .GetSceneVolume()
        );
    }

    SceneList::PublishFrame(std::move(Frame));
}

}
