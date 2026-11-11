#include "X/Engine/Base/Render/RenderManager.hpp"

#include "X/Configs/Static.hpp"
#include "X/Configs/PreCompiled/Dynamic.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan_core.h>
#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cmath>
#include <cstdint>

namespace Engine::Base::Render::It
{

namespace
{

vkb::Instance m_Instance;

uint32_t m_FrameCounter = 0;

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("render_init", Init, 70);

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterWait("render_wait_idle", WaitIdle, 90);

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("render_clean", Cleanup, 60);

const JobSystem::DeferredTaskRegistration::Registrar
    RegisterFrame(::Engine::Base::Runtime::EngineThreadID::RenderTh, "render_frame", RenderFrame, 50);

void ClearBackground(VkCommandBuffer p_Cmd)
{
    const auto& Target = Vk::Frame::GetCurrentImage();
    const auto& Disp   = Vk::Device::GetDisp();

    Vk::Commands::TransitionImage(p_Cmd, Target.Image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo Attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

    Attachment.imageView   = Target.ImageView;
    Attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    const bool SRGB = Vk::Frame::GetSwapchain().image_format == VK_FORMAT_B8G8R8A8_SRGB
        || Vk::Frame::GetSwapchain().image_format == VK_FORMAT_R8G8B8A8_SRGB;

    for (int I = 0; I < 3; ++I) {
        const float Value = Configs::Static::Graphics::BACKGROUND_RGB[I] / 255.0f;

        Attachment.clearValue.color.float32[I] = SRGB
            ? (Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f))
            : Value;
    }

    Attachment.clearValue.color.float32[3] = 1.0f;

    VkRenderingInfo Info{VK_STRUCTURE_TYPE_RENDERING_INFO};

    Info.renderArea.extent     = Vk::Frame::GetSwapchain().extent;
    Info.layerCount            = 1;
    Info.colorAttachmentCount = 1;
    Info.pColorAttachments    = &Attachment;

    Disp.cmdBeginRendering(p_Cmd, &Info);
    Disp.cmdEndRendering(p_Cmd);

    Vk::Commands::MemoryBarrier(p_Cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

}

void Init()
{
    Uint32 Count = 0;
    const char* const* Extensions = SDL_Vulkan_GetInstanceExtensions(&Count);

    X_ASSERT(Extensions == nullptr, SDL_GetError());

    vkb::InstanceBuilder Builder;

    Builder
        .set_app_name(X_PROJECT_NAME)
        .enable_extensions(Count, Extensions)
        .require_api_version(1, 3, 0);

#ifdef XP_DEV_FEATURES_ENABLED
    Builder.use_default_debug_messenger().request_validation_layers();
#endif

    auto Result = Builder.build();

    X_ASSERT(!Result, Result.error().message());

    m_Instance = Result.value();

    ArtifactsDeletor::Push([] {
        vkb::destroy_instance(m_Instance);
        m_Instance = {};
    });

    Vk::Device::CreateSurface(m_Instance);
    Vk::Device::CreateDevice(m_Instance);
    Vk::Device::MakeQueues();

    ArtifactsDeletor::Push(Vk::Frame::DestroySwapchain);
    Vk::Frame::CreateSwapchain();

    Vk::Commands::CreatePool();

    Vk::GpuResources::CreateAllocator(m_Instance);

    Vk::Frame::CreateFrameData();

}

void WaitIdle()
{
    if (Vk::Device::GetDevice().device != VK_NULL_HANDLE) {
        X_ASSERT(Vk::Device::GetDisp().deviceWaitIdle() != VK_SUCCESS, "Failed to wait for Vulkan device");
    }
}

const vkb::Instance& GetInstance()
{
    return m_Instance;
}

void Cleanup()
{
    RenderingPool::Clear();

    if (Vk::Device::GetDevice().device != VK_NULL_HANDLE) {
        Vk::Samplers::DestroyAll();
    }

    ArtifactsDeletor::CleanAll();
}

void RenderFrame()
{
    const auto& Disp = Vk::Device::GetDisp();
    auto& Frame = Vk::Frame::GetCurrentFrame();

    X_ASSERT(Disp.waitForFences(1, &Frame.FenceInflight, VK_TRUE, UINT64_MAX) != VK_SUCCESS,
        "Failed to wait for frame fence");

    const auto Extent = Viewport::Surface::GetExtent();

    if (Extent.x == 0 || Extent.y == 0) {
        return;
    }

    if (Viewport::Surface::GetUnhandleResized()) {
        Vk::Frame::RecreateSwapchain();
    }

    VkResult Result = Disp.acquireNextImageKHR(Vk::Frame::GetSwapchain(), UINT64_MAX,
        Frame.SemaphoreAvailable, VK_NULL_HANDLE, &Vk::Frame::GetImageIndex());

    if (Result == VK_ERROR_OUT_OF_DATE_KHR) {
        Vk::Frame::RecreateSwapchain();
        return;
    }

    X_ASSERT(Result != VK_SUCCESS && Result != VK_SUBOPTIMAL_KHR, "Failed to acquire swapchain image");

    const bool Recreate = Result == VK_SUBOPTIMAL_KHR;
    auto& Image = Vk::Frame::GetCurrentImage();

    vmaSetCurrentFrameIndex(Vk::GpuResources::GetAllocator(), m_FrameCounter++);
    Vk::Commands::Reset(Frame.CommandBuffer);
    Vk::Commands::Begin(Frame.CommandBuffer);
    ResourceLoader::BeginFrame();
    ResourceLoader::RecordPendingAcquires(Frame.CommandBuffer);

    ClearBackground(Frame.CommandBuffer);

    RenderingPool::RenderAll();

    Vk::Commands::TransitionImage(Frame.CommandBuffer, Image.Image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, 0);

    Vk::Commands::End(Frame.CommandBuffer);

    X_ASSERT(Disp.resetFences(1, &Frame.FenceInflight) != VK_SUCCESS, "Failed to reset frame fence");

    VkSemaphoreSubmitInfo Wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};

    Wait.semaphore = Frame.SemaphoreAvailable;
    Wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo Signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};

    Signal.semaphore = Image.SemaphoreFinished;
    Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSemaphoreSubmitInfo TransferWait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    const VkSemaphoreSubmitInfo* TransferWaitPtr = nullptr;
    if (ResourceLoader::HasTransferSubmission()) {
        TransferWait.semaphore = ResourceLoader::GetCompletionSemaphore();
        TransferWait.value = ResourceLoader::GetCompletionValue();
        TransferWait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        TransferWaitPtr = &TransferWait;
    }

    Vk::Commands::Submit(
        Frame.CommandBuffer,
        &Wait,
        &Signal,
        Frame.FenceInflight,
        TransferWaitPtr
    );

    VkSwapchainKHR Swapchain = Vk::Frame::GetSwapchain();
    VkPresentInfoKHR Present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

    Present.waitSemaphoreCount = 1;
    Present.pWaitSemaphores = &Image.SemaphoreFinished;
    Present.swapchainCount = 1;
    Present.pSwapchains = &Swapchain;
    Present.pImageIndices = &Vk::Frame::GetImageIndex();

    Result = Disp.queuePresentKHR(Vk::Device::GetQueues().Present, &Present);

    Vk::Frame::Advance();

    if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR || Recreate) {
        X_ASSERT(Result != VK_SUCCESS && Result != VK_ERROR_OUT_OF_DATE_KHR && Result != VK_SUBOPTIMAL_KHR,
            "Failed to present swapchain image");
        Vk::Frame::RecreateSwapchain();
        return;
    }

    X_ASSERT(Result != VK_SUCCESS, "Failed to present swapchain image");
}

}
