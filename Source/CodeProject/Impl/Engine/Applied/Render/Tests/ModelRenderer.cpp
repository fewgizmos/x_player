#include "X/Engine/Applied/Render/Tests/ModelRenderer.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace Engine::Applied::Render::Tests::ModelRenderer
{

namespace
{

using Model          = ::Engine::Applied::Render::Resources::Models::Model;
using ModelSnapshot  = ::Engine::Applied::Render::Resources::Models::ModelSnapshot;
using Mesh           = ::Engine::Applied::Render::Resources::Meshes::Mesh;
using MeshSnapshot   = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using Shader         = ::Engine::Applied::Render::Resources::Shaders::Shader;
using ShaderSnapshot = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;
using Vertex         = ::Engine::Applied::Render::Resources::Meshes::Vertex;

::Engine::Base::Render::Image m_DepthImage;

VkImageView m_DepthView = VK_NULL_HANDLE;
VkExtent2D m_DepthExtent = {};

bool m_DepthLayoutReady = false;

void ExpandBounds(glm::vec3& p_Min, glm::vec3& p_Max, const MeshSnapshot& p_Mesh, const glm::mat4& p_Transform)
{
    const glm::vec3 Corners[] = {
        { p_Mesh.BoundsMin.x, p_Mesh.BoundsMin.y, p_Mesh.BoundsMin.z },
        { p_Mesh.BoundsMax.x, p_Mesh.BoundsMin.y, p_Mesh.BoundsMin.z },
        { p_Mesh.BoundsMin.x, p_Mesh.BoundsMax.y, p_Mesh.BoundsMin.z },
        { p_Mesh.BoundsMax.x, p_Mesh.BoundsMax.y, p_Mesh.BoundsMin.z },
        { p_Mesh.BoundsMin.x, p_Mesh.BoundsMin.y, p_Mesh.BoundsMax.z },
        { p_Mesh.BoundsMax.x, p_Mesh.BoundsMin.y, p_Mesh.BoundsMax.z },
        { p_Mesh.BoundsMin.x, p_Mesh.BoundsMax.y, p_Mesh.BoundsMax.z },
        { p_Mesh.BoundsMax.x, p_Mesh.BoundsMax.y, p_Mesh.BoundsMax.z }
    };

    for (const glm::vec3& Corner : Corners) {
        const glm::vec3 Transformed = glm::vec3(p_Transform * glm::vec4(Corner, 1.0f));

        p_Min = glm::min(p_Min, Transformed);
        p_Max = glm::max(p_Max, Transformed);
    }
}

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

static_assert(sizeof(PushConstants) == 128, "BaseModel push constants must match GLSL std430 layout");
static_assert(offsetof(PushConstants, UvTransformRow0) == 96);
static_assert(offsetof(PushConstants, UvTransformRow1) == 112);

::Engine::Util::Applied::ResourceHandle<Shader> m_ModelShader;
::Engine::Util::Applied::ResourceHandle<Model> m_Model;

std::shared_ptr<const ModelSnapshot> m_FrameModel;
std::shared_ptr<const ShaderSnapshot> m_FrameShader;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("model_test_renderer_init", Init, 20);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("model_test_renderer_cleanup", Cleanup, 80);

void CreateShaders()
{
    Engine::Base::Render::GraphicsPipelineCreateInfo PipelineInfo;
    PipelineInfo.ColorFormats = {
        ::Engine::Base::Render::Vk::Frame::GetSwapchain().image_format
    };
    PipelineInfo.DepthFormat = VK_FORMAT_D32_SFLOAT;
    PipelineInfo.DepthCompare = VK_COMPARE_OP_GREATER;
    PipelineInfo.CullMode = VK_CULL_MODE_NONE;
    PipelineInfo.DepthWrite = true;
    PipelineInfo.PushConstantSize = sizeof(PushConstants);
    PipelineInfo.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PipelineInfo.VertexBindings = {
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
    };
    PipelineInfo.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position) },
        { 1, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvX) },
        { 2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvY) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal) }
    };

    ::Engine::Applied::Render::Resources::Shaders::ShaderCreateInfo ModelInfo;
    ModelInfo.VertexFile = "Materials/BaseModel.vert";
    ModelInfo.FragmentFile = "Materials/BaseModel.frag";
    ModelInfo.Graphics = PipelineInfo;
    ModelInfo.Graphics.SetLayouts = {
        ::Engine::Applied::Render::Resources::Textures::GetBindlessSetLayout()
    };
    ModelInfo.Graphics.PushConstantSize = sizeof(PushConstants);
    ModelInfo.Graphics.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position) },
        { 1, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvX) },
        { 2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvY) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Normal) }
    };
    m_ModelShader = ::Engine::Util::Applied::ResourceHandle<Shader>(std::move(ModelInfo));

}

void CreateModel()
{
    if (std::string(::Engine::Configs::Static::Tests::MODEL_RENDERER_PATH).empty()) {
        return;
    }

    ::Engine::Applied::Render::Resources::Models::ModelCreateInfo Info;

    Info.GltfPath = ::Engine::Configs::Static::Tests::MODEL_RENDERER_PATH;
    Info.Shader = m_ModelShader;

    m_Model = ::Engine::Util::Applied::ResourceHandle<Model>(std::move(Info));
}

void CaptureFrameSnapshot()
{
    m_FrameModel.reset();
    m_FrameShader.reset();

    if (m_Model.IsReady() && m_ModelShader.IsReady()) {
        m_FrameModel = m_Model->GetSnapshot();
        m_FrameShader = m_ModelShader->GetSnapshot();
    }
}

void EnsureDepthTarget(const VkExtent2D& p_Extent)
{
    if (m_DepthImage.Handle != VK_NULL_HANDLE
        && m_DepthExtent.width == p_Extent.width
        && m_DepthExtent.height == p_Extent.height) {

        return;
    }

    if (m_DepthView != VK_NULL_HANDLE) {
        m_DepthView = VK_NULL_HANDLE;
    }

    if (m_DepthImage.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::ResourceLoader::DestroyImage(m_DepthImage);
    }

    m_DepthImage = ::Engine::Base::Render::ResourceLoader::CreateImage(
        { p_Extent.width, p_Extent.height, 1 },
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    m_DepthView = m_DepthImage.View;
    m_DepthExtent = p_Extent;
    m_DepthLayoutReady = false;
}

}

void Init()
{
    if constexpr (!::Engine::Configs::Static::Tests::MODEL_RENDERER_ENABLED) {
        return;
    }

    CreateShaders();
    CreateModel();

    ::Engine::Base::Render::RenderingPool::Register("model_test", Render, 900);
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot(
        "model_test_snapshot", CaptureFrameSnapshot, 900
    );
}

void Cleanup()
{
    m_FrameModel.reset();
    m_FrameShader.reset();

    m_Model = {};
    m_ModelShader = {};

    if (m_DepthView != VK_NULL_HANDLE) {
        m_DepthView = VK_NULL_HANDLE;
    }

    if (m_DepthImage.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::ResourceLoader::DestroyImage(m_DepthImage);
    }

    m_DepthExtent = {};
    m_DepthLayoutReady = false;
}

void Render()
{
    if (!m_FrameModel || !m_FrameShader) {
        return;
    }

    const auto& Frame = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Image = ::Engine::Base::Render::Vk::Frame::GetCurrentImage();
    const auto& Swapchain = ::Engine::Base::Render::Vk::Frame::GetSwapchain();
    const auto& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    EnsureDepthTarget(Swapchain.extent);

    if (!m_DepthLayoutReady) {
        ::Engine::Base::Render::Vk::Commands::TransitionImage(
            Frame.CommandBuffer,
            m_DepthImage.Handle,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE,
            0,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            1,
            1
        );

        m_DepthLayoutReady = true;
    }

    glm::vec3 BoundsMin(std::numeric_limits<float>::max());
    glm::vec3 BoundsMax(std::numeric_limits<float>::lowest());

    for (const ::Engine::Applied::Render::Resources::Models::ModelPart& Part : m_FrameModel->Parts) {
        if (!Part.Mesh || !Part.Material || !Part.Mesh.IsReady() || !Part.Material.IsReady()) {
            return;
        }

        if (Part.Skeleton && !Part.Skeleton.IsReady()) {
            return;
        }

        if (Part.MorphTargets.size() != Part.MorphWeights.size()) {
            return;
        }

        for (const ::Engine::Util::Applied::ResourceHandle<
            ::Engine::Applied::Render::Resources::Meshes::MeshMorphTarget>& MorphTarget
            : Part.MorphTargets) {

            if (!MorphTarget || !MorphTarget.IsReady() || !MorphTarget->GetSnapshot()) {
                return;
            }
        }

        const std::shared_ptr<const MeshSnapshot> MeshValue = Part.Mesh->GetSnapshot();

        if (!MeshValue) {
            return;
        }

        ExpandBounds(BoundsMin, BoundsMax, *MeshValue, Part.PrivateTransform);
    }

    if (m_FrameModel->Parts.empty()) {
        return;
    }

    const glm::vec3 Center = (BoundsMin + BoundsMax) * 0.5f;
    const float Radius = glm::max(glm::length(BoundsMax - BoundsMin) * 0.5f, 0.001f);

    const glm::mat4 Normalization = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(1.0f / Radius)
    ) * glm::translate(glm::mat4(1.0f), -Center);

    const glm::mat4 View = glm::lookAt(
        glm::vec3(1.0f, 1.41421356f, 1.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    const float Aspect = static_cast<float>(Swapchain.extent.width)
        / static_cast<float>(Swapchain.extent.height);

    const glm::mat4 Projection = glm::ortho(
        -1.1f * Aspect, 1.1f * Aspect, -1.1f, 1.1f, -8.0f, 8.0f
    );

    VkRenderingAttachmentInfo Attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    Attachment.imageView = Image.ImageView;
    Attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo DepthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    DepthAttachment.imageView = m_DepthView;
    DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthAttachment.clearValue.depthStencil = { 0.0f, 0 };

    VkRenderingInfo RenderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    RenderingInfo.renderArea.extent = Swapchain.extent;
    RenderingInfo.layerCount = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments = &Attachment;
    RenderingInfo.pDepthAttachment = &DepthAttachment;
    Disp.cmdBeginRendering(Frame.CommandBuffer, &RenderingInfo);

    VkViewport Viewport{};
    Viewport.width = static_cast<float>(Swapchain.extent.width);
    Viewport.height = static_cast<float>(Swapchain.extent.height);
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = Swapchain.extent;
    Disp.cmdSetViewport(Frame.CommandBuffer, 0, 1, &Viewport);
    Disp.cmdSetScissor(Frame.CommandBuffer, 0, 1, &Scissor);
    Disp.cmdBindPipeline(
        Frame.CommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_FrameShader->Graphics.Handle
    );

    for (const ::Engine::Applied::Render::Resources::Models::ModelPart& Part : m_FrameModel->Parts) {
        if (!Part.Mesh || !Part.Material || !Part.Mesh.IsReady() || !Part.Material.IsReady()) {
            continue;
        }

        if (Part.Skeleton && !Part.Skeleton.IsReady()) {
            continue;
        }

        if (Part.MorphTargets.size() != Part.MorphWeights.size()) {
            continue;
        }

        const std::shared_ptr<const MeshSnapshot> MeshValue = Part.Mesh->GetSnapshot();
        const std::shared_ptr<const ::Engine::Applied::Render::Resources::Materials::MaterialSnapshot> MaterialValue = Part.Material->GetSnapshot();

        if (!MeshValue || !MaterialValue) {
            continue;
        }


        const PushConstants Constants{
            Projection * View * Normalization * Part.PrivateTransform,
            MaterialValue->Data.BaseColor,
            MaterialValue->Data.BaseColorTexture,
            MaterialValue->Data.BaseColorSampler,
            { 0u, 0u },
            MaterialValue->Data.UvTransforms[0],
            MaterialValue->Data.UvTransforms[1]
        };

        const VkDeviceSize Offset = 0;

        Disp.cmdBindVertexBuffers(Frame.CommandBuffer, 0, 1, &MeshValue->VertexBuffer.Handle, &Offset);
        Disp.cmdBindIndexBuffer(Frame.CommandBuffer, MeshValue->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);

        const VkDescriptorSet DescriptorSet =
            ::Engine::Applied::Render::Resources::Textures::GetBindlessSet();

        Disp.cmdBindDescriptorSets(
            Frame.CommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_FrameShader->Graphics.Layout,
            0,
            1,
            &DescriptorSet,
            0,
            nullptr
        );

        ::Engine::Base::Render::Vk::Commands::PushConstants(
            Frame.CommandBuffer,
            m_FrameShader->Graphics.Layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(PushConstants),
            &Constants
        );

        Disp.cmdDrawIndexed(Frame.CommandBuffer, MeshValue->IndexCount, 1, 0, 0, 0);
    }

    Disp.cmdEndRendering(Frame.CommandBuffer);
}

::Engine::Util::Applied::ResourceState GetModelState()
{
    return m_Model.GetState();
}

}

#endif
