#include "X/Engine/Applied/Render/SceneRendering/SceneRenderer.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

namespace Engine::Applied::Render::SceneRendering::It
{

namespace
{

constexpr VkFormat TargetFormat = VK_FORMAT_R8G8B8A8_SRGB;

::Engine::Base::Render::Image m_DepthImage;

VkImageView m_DepthView = VK_NULL_HANDLE;
VkExtent2D m_DepthExtent = {};

bool m_DepthLayoutReady = false;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_renderer_init", Init, 20);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("scene_renderer_cleanup", Cleanup, 80);

void EnsureDepthTarget(const VkExtent2D& p_Extent)
{
    if (m_DepthImage.Handle != VK_NULL_HANDLE
        && m_DepthExtent.width == p_Extent.width
        && m_DepthExtent.height == p_Extent.height) {

        return;
    }

    if (m_DepthView != VK_NULL_HANDLE) { m_DepthView = VK_NULL_HANDLE; }

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
    ::Engine::Base::Render::RenderingPool::Register("scene", Render, TargetFormat,
        ::Engine::Base::Render::RenderingPool::WindowExtent, 1000);
}

void Cleanup()
{
    SceneList::PublishFrame(nullptr);

    if (m_DepthView != VK_NULL_HANDLE) { m_DepthView = VK_NULL_HANDLE; }

    if (m_DepthImage.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::ResourceLoader::DestroyImage(m_DepthImage);
    }

    m_DepthExtent = {};
    m_DepthLayoutReady = false;
}

void Render(const ::Engine::Base::Render::RenderingPool::RenderTarget& p_Target)
{
    const auto& Frame          = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Disp  = ::Engine::Base::Render::Vk::Device::GetDisp();

    EnsureDepthTarget(p_Target.Extent);

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

    VkRenderingAttachmentInfo Attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    Attachment.imageView = p_Target.View;
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
    RenderingInfo.renderArea.extent = p_Target.Extent;
    RenderingInfo.layerCount = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments = &Attachment;
    RenderingInfo.pDepthAttachment = &DepthAttachment;
    Disp.cmdBeginRendering(Frame.CommandBuffer, &RenderingInfo);

    VkViewport Viewport{};
    Viewport.width = static_cast<float>(p_Target.Extent.width);
    Viewport.height = static_cast<float>(p_Target.Extent.height);
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = p_Target.Extent;
    Disp.cmdSetViewport(Frame.CommandBuffer, 0, 1, &Viewport);
    Disp.cmdSetScissor(Frame.CommandBuffer, 0, 1, &Scissor);

    SceneRenderPassPool::RenderAll();

    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}
