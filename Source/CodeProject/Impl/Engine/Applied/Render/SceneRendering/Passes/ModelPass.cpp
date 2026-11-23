#include "X/Engine/Applied/Render/SceneRendering/Passes/ModelPass.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"

#include "X/Engine/Applied/Render/Resources/Materials.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"
#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <cstdint>
#include <memory>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace Engine::Applied::Render::SceneRendering::Passes::ModelPass
{

namespace
{

using Model            = ::Engine::Applied::Render::Resources::Models::Model;
using ModelPart        = ::Engine::Applied::Render::Resources::Models::ModelPart;
using ModelSnapshot    = ::Engine::Applied::Render::Resources::Models::ModelSnapshot;
using MeshSnapshot     = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using MaterialSnapshot = ::Engine::Applied::Render::Resources::Materials::MaterialSnapshot;
using ShaderSnapshot   = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;

struct alignas(16) PushConstants
{
    glm::mat4 Transform;
 
    glm::vec4 BaseColor;

    uint32_t TextureIndex;
    uint32_t SamplerIndex;

    uint32_t Padding[2];

    glm::vec4 UvTransformRow0;
    glm::vec4 UvTransformRow1;
};

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_model_pass_init", Init, 30);

bool ModelPartIsReady(const ModelPart& p_Part)
{
    if (!p_Part.Mesh || !p_Part.Material || !p_Part.Mesh.IsReady() || !p_Part.Material.IsReady()) { return false; }

    if (p_Part.Skeleton && !p_Part.Skeleton.IsReady()) { return false; }

    if (p_Part.MorphTargets.size() != p_Part.MorphWeights.size()) { return false; }

    for (const auto& MorphTarget : p_Part.MorphTargets) {
        if (!MorphTarget || !MorphTarget.IsReady() || !MorphTarget->GetSnapshot()) { return false; }
    }

    return static_cast<bool>(p_Part.Mesh->GetSnapshot()) && static_cast<bool>(p_Part.Material->GetSnapshot());
}

void DrawModel(
    const Model& p_Model,
    const glm::mat4& p_View,
    const glm::mat4& p_Projection,
    const glm::mat4& p_ModelTransform,
    VkCommandBuffer p_CommandBuffer,
    const vkb::DispatchTable& p_Disp
)
{
    const std::shared_ptr<const ModelSnapshot> ModelSnapshot = p_Model.GetSnapshot();

    if (!p_Model.GetShader().IsReady()) { return; }

    const std::shared_ptr<const ShaderSnapshot> ShaderValue = p_Model.GetShader()->GetSnapshot();

    if (!ModelSnapshot || !ShaderValue || ShaderValue->Graphics.Handle == VK_NULL_HANDLE) { return; }

    p_Disp.cmdBindPipeline(
        p_CommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        ShaderValue->Graphics.Handle
    );

    for (const ModelPart& Part : ModelSnapshot->Parts) {
        if (!ModelPartIsReady(Part)) { continue; }

        const std::shared_ptr<const MeshSnapshot> MeshValue = Part.Mesh->GetSnapshot();
        const std::shared_ptr<const MaterialSnapshot> MaterialValue = Part.Material->GetSnapshot();

        if (!MeshValue || !MaterialValue) { continue; }

        const PushConstants Constants{
            p_Projection * p_View * p_ModelTransform * Part.PrivateTransform,
            MaterialValue->Data.BaseColor,
            MaterialValue->Data.BaseColorTexture,
            MaterialValue->Data.BaseColorSampler,
            { 0u, 0u },
            MaterialValue->Data.UvTransforms[0],
            MaterialValue->Data.UvTransforms[1]
        };

        const VkDeviceSize Offset = 0;

        p_Disp.cmdBindVertexBuffers(
            p_CommandBuffer,
            0,
            1,
            &MeshValue->VertexBuffer.Handle,
            &Offset
        );

        p_Disp.cmdBindIndexBuffer(
            p_CommandBuffer,
            MeshValue->IndexBuffer.Handle,
            0,
            VK_INDEX_TYPE_UINT32
        );

        const VkDescriptorSet DescriptorSet = ::Engine::Applied::Render::Resources::Textures::GetBindlessSet();

        p_Disp.cmdBindDescriptorSets(
            p_CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            ShaderValue->Graphics.Layout,
            0,
            1,
            &DescriptorSet,
            0,
            nullptr
        );

        ::Engine::Base::Render::Vk::Commands::PushConstants(
            p_CommandBuffer,
            ShaderValue->Graphics.Layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(PushConstants),
            &Constants
        );

        p_Disp.cmdDrawIndexed(
            p_CommandBuffer,
            MeshValue->IndexCount,
            1,
            0,
            0,
            0
        );
    }
}

}

void Init()
{
    ::Engine::Applied::Render::SceneRendering::SceneRenderPassPool::Register(
        "model",
        Render,
        900
    );
}

void Render()
{
    const auto* Camera = ::Engine::Applied::Render::SceneRendering::SceneList::GetCamera();

    if (Camera == nullptr) { return; }

    const auto& Frame          = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Disp  = ::Engine::Base::Render::Vk::Device::GetDisp();

    for (const auto& Entry :
        ::Engine::Applied::Render::SceneRendering::SceneList::GetModels()) {

        if (Entry.Target == nullptr || !Entry.Target->IsReady()) { continue; }

        DrawModel(
            *Entry.Target,
            Camera->View,
            Camera->Projection,
            Entry.AppliedTransform.ToMatrix(),
            Frame.CommandBuffer,
            Disp
        );
    }
}

}
