#include "X/Engine/Applied/Render/SceneRendering/SceneRenderer.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Applied/Render/SceneRendering/Passes/LightPass.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <array>
#include <span>

namespace Engine::Applied::Render::SceneRendering::It
{

namespace
{

namespace RenderBase = ::Engine::Base::Render;
namespace Vk         = RenderBase::Vk;
namespace Graphics   = Project::Configs::Static::Graphics;

using Phase = SceneRenderPassPool::Phase;

constexpr VkPipelineStageFlags2 DepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

struct GBuffer
{
    RenderBase::Image Albedo;
    RenderBase::Image Normal;
    RenderBase::Image Depth;
    VkExtent2D        Extent{};
};

std::array<GBuffer, Graphics::MAX_FRAMES_IN_FLIGHT> m_GBuffers;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_renderer_init", Init, 20);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("scene_renderer_cleanup", Cleanup, 80);

void DestroyGBuffer(GBuffer& p_Buffer)
{
    RenderBase::ResourceLoader::DestroyImage(p_Buffer.Albedo);
    RenderBase::ResourceLoader::DestroyImage(p_Buffer.Normal);
    RenderBase::ResourceLoader::DestroyImage(p_Buffer.Depth);
    p_Buffer.Extent = {};
}

GBuffer& EnsureGBuffer(const VkExtent2D& p_Extent)
{
    auto& Buffer = m_GBuffers[Vk::Frame::GetCurrentFrameIndex()];

    if (Buffer.Extent.width == p_Extent.width && Buffer.Extent.height == p_Extent.height) { return Buffer; }

    DestroyGBuffer(Buffer);

    const VkExtent3D            Extent{p_Extent.width, p_Extent.height, 1};
    constexpr VkImageUsageFlags ColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    Buffer.Albedo = RenderBase::ResourceLoader::CreateImage(
        Extent, Graphics::GBUFFER_ALBEDO_FORMAT, ColorUsage, VK_IMAGE_ASPECT_COLOR_BIT
    );

    Buffer.Normal = RenderBase::ResourceLoader::CreateImage(
        Extent, Graphics::GBUFFER_NORMAL_FORMAT, ColorUsage, VK_IMAGE_ASPECT_COLOR_BIT
    );

    Buffer.Depth = RenderBase::ResourceLoader::CreateImage(Extent, Graphics::DEPTH_FORMAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    Buffer.Extent = p_Extent;

    return Buffer;
}

VkRenderingAttachmentInfo MakeColorAttachment(VkImageView p_View, VkAttachmentLoadOp p_Load)
{
    VkRenderingAttachmentInfo Attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    Attachment.imageView   = p_View;
    Attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.loadOp      = p_Load;
    Attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    return Attachment;
}

void BeginRendering(VkCommandBuffer p_Cmd, VkExtent2D p_Extent,
    std::span<const VkRenderingAttachmentInfo> p_Colors, const VkRenderingAttachmentInfo* p_Depth = nullptr)
{
    VkRenderingInfo Info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    Info.renderArea.extent = p_Extent;
    Info.layerCount        = 1;

    Info.colorAttachmentCount = static_cast<uint32_t>(p_Colors.size());
    Info.pColorAttachments    = p_Colors.data();
    Info.pDepthAttachment     = p_Depth;

    Vk::Device::GetDisp().cmdBeginRendering(p_Cmd, &Info);
}

void RenderGeometry(VkCommandBuffer p_Cmd, const GBuffer& p_Buffer, const SceneList::Frame& p_Scene)
{
    for (const auto Image : {p_Buffer.Albedo.Handle, p_Buffer.Normal.Handle}) {
        Vk::Commands::TransitionImage(p_Cmd, Image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    Vk::Commands::TransitionImage(p_Cmd, p_Buffer.Depth.Handle,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, 0, DepthStages,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    const VkRenderingAttachmentInfo Colors[] = {
        MakeColorAttachment(p_Buffer.Albedo.View, VK_ATTACHMENT_LOAD_OP_CLEAR),
        MakeColorAttachment(p_Buffer.Normal.View, VK_ATTACHMENT_LOAD_OP_CLEAR)
    };
    VkRenderingAttachmentInfo Depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    Depth.imageView = p_Buffer.Depth.View;
    Depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    Depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    Depth.clearValue.depthStencil = {0.0f, 0};

    BeginRendering(p_Cmd, p_Buffer.Extent, Colors, &Depth);

    SceneRenderPassPool::RenderAll(Phase::Geometry, p_Scene);

    Vk::Device::GetDisp().cmdEndRendering(p_Cmd);

    for (const auto Image : {p_Buffer.Albedo.Handle, p_Buffer.Normal.Handle}) {
        Vk::Commands::TransitionImage(p_Cmd, Image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    Vk::Commands::TransitionImage(p_Cmd, p_Buffer.Depth.Handle,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        DepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
}

void RenderTransparent(VkCommandBuffer p_Cmd, const GBuffer& p_Buffer,
    const VkRenderingAttachmentInfo& p_Color, const SceneList::Frame& p_Scene)
{
    Vk::Commands::MemoryBarrier(p_Cmd,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    Vk::Commands::TransitionImage(p_Cmd, p_Buffer.Depth.Handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        DepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo Depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    Depth.imageView = p_Buffer.Depth.View;
    Depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    Depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    Depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    BeginRendering(p_Cmd, p_Buffer.Extent, {&p_Color, 1}, &Depth);
    SceneRenderPassPool::RenderAll(Phase::Transparent, p_Scene);
    Vk::Device::GetDisp().cmdEndRendering(p_Cmd);
}

}

void Init()
{
    RenderBase::RenderingPool::Register("scene", Render, Graphics::SCENE_COLOR_FORMAT,
        RenderBase::RenderingPool::WindowExtent, 1000);
}

void Cleanup()
{
    SceneList::PublishFrame(nullptr);

    for (auto& Buffer : m_GBuffers) { DestroyGBuffer(Buffer); }
}

void Render(const RenderBase::RenderingPool::RenderTarget& p_Target)
{
    const auto Scene = SceneList::Acquire();

    if (!Scene || !Scene->ActiveCamera) { return; }

    const auto Cmd = Vk::Frame::GetCurrentFrame().CommandBuffer;
    const auto& Disp = Vk::Device::GetDisp();
    const auto& Buffer = EnsureGBuffer(p_Target.Extent);

    Passes::LightPass::Prepare(*Scene, Buffer.Albedo.View, Buffer.Normal.View, Buffer.Depth.View);

    VkViewport Viewport{};
    Viewport.width    = static_cast<float>(p_Target.Extent.width);
    Viewport.height   = static_cast<float>(p_Target.Extent.height);
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = p_Target.Extent;
    Disp.cmdSetViewport(Cmd, 0, 1, &Viewport);
    Disp.cmdSetScissor(Cmd, 0, 1, &Scissor);

    RenderGeometry(Cmd, Buffer, *Scene);

    const auto Color = MakeColorAttachment(p_Target.View, VK_ATTACHMENT_LOAD_OP_LOAD);

    BeginRendering(Cmd, p_Target.Extent, {&Color, 1});

    SceneRenderPassPool::RenderAll(Phase::Light, *Scene);

    Disp.cmdEndRendering(Cmd);

    RenderTransparent(Cmd, Buffer, Color, *Scene);
}

}
