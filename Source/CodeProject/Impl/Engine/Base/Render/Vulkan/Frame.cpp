#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <vector>

namespace Engine::Base::Render::Vk::Frame
{

namespace
{

struct SwapchainState
{
    vkb::Swapchain Handle;
    std::vector<SwapchainImageData> Images;
};

SwapchainState m_Swapchain;

std::vector<FrameData> m_Frames;

std::size_t m_CurrentFrame = 0;
uint32_t m_ImageIndex = 0;

VkSemaphore MakeSemaphore()
{
    VkSemaphoreCreateInfo SemaphoreCreateInfo = {};

    SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore Semaphore = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createSemaphore(&SemaphoreCreateInfo, nullptr, &Semaphore);

    X_ASSERT(Result != VK_SUCCESS, "failed to create semaphore");

    return Semaphore;
}

VkFence MakeFence(VkFenceCreateFlags p_Flags)
{
    VkFenceCreateInfo FenceCreateInfo = {};

    FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    FenceCreateInfo.flags = p_Flags;

    VkFence Fence = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createFence(&FenceCreateInfo, nullptr, &Fence);

    X_ASSERT(Result != VK_SUCCESS, "failed to create fence");

    return Fence;
}

void BuildSwapchain()
{
    const glm::ivec2 Extent = Viewport::Surface::GetExtent();

    vkb::SwapchainBuilder SwapchainBuilder(Device::GetDevice());

    auto SwapRet = SwapchainBuilder
        .set_old_swapchain(m_Swapchain.Handle)
        .set_desired_extent(static_cast<uint32_t>(Extent.x), static_cast<uint32_t>(Extent.y))

        .set_desired_present_mode(
            ::Engine::Configs::Static::Graphics::VSYNC
                ? VK_PRESENT_MODE_FIFO_KHR
                : VK_PRESENT_MODE_IMMEDIATE_KHR
        )

        .add_fallback_present_mode(
            ::Engine::Configs::Static::Graphics::VSYNC
                ? VK_PRESENT_MODE_FIFO_KHR
                : VK_PRESENT_MODE_FIFO_KHR
        )

        .build();

    X_ASSERT(!SwapRet, SwapRet.error().message());

    vkb::destroy_swapchain(m_Swapchain.Handle);

    m_Swapchain.Handle = SwapRet.value();
}

void CreateSwapchainImageData()
{
    auto Images      = m_Swapchain.Handle.get_images();
    auto ImageViews = m_Swapchain.Handle.get_image_views();

    X_ASSERT(!Images || !ImageViews, "failed to Get swapchain images");

    m_Swapchain.Images.resize(m_Swapchain.Handle.image_count);

    for (uint32_t I = 0; I < m_Swapchain.Handle.image_count; I++) {
        m_Swapchain.Images[I].Image     = Images.value()[I];
        m_Swapchain.Images[I].ImageView = ImageViews.value()[I];
    }

    for (uint32_t I = 0; I < m_Swapchain.Handle.image_count; I++) {
        SwapchainImageData& ImageData = m_Swapchain.Images[I];

        ImageData.Image     = Images.value()[I];
        ImageData.ImageView = ImageViews.value()[I];

        ImageData.SemaphoreFinished = MakeSemaphore();
    }
}

void DestroySwapchainImageData()
{
    for (SwapchainImageData& ImageData : m_Swapchain.Images) {
        Device::GetDisp().destroyImageView(ImageData.ImageView, nullptr);
        Device::GetDisp().destroySemaphore(ImageData.SemaphoreFinished, nullptr);
    }

    m_Swapchain.Images.clear();
}

}

void CreateSwapchain()
{
    BuildSwapchain();
    CreateSwapchainImageData();
}

void DestroySwapchain()
{
    DestroySwapchainImageData();

    vkb::destroy_swapchain(m_Swapchain.Handle);
    m_Swapchain.Handle = {};
}

void RecreateSwapchain()
{
    Viewport::Surface::ClearUnhandleResized();

    Device::WaitIdle();

    DestroySwapchainImageData();

    CreateSwapchain();
}

void CreateFrameData()
{
    m_Frames.resize(Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT);

    for (FrameData& Frame : m_Frames) {
        Frame.CommandBuffer = Commands::Allocate();

        Frame.SemaphoreAvailable = MakeSemaphore();

        ArtifactsDeletor::Push([Semaphore = Frame.SemaphoreAvailable]() {
            Device::GetDisp().destroySemaphore(Semaphore, nullptr);
        });

        Frame.FenceInflight = MakeFence(VK_FENCE_CREATE_SIGNALED_BIT);

        ArtifactsDeletor::Push([Fence = Frame.FenceInflight]() {
            Device::GetDisp().destroyFence(Fence, nullptr);
        });
    }
}

void Advance()
{
    m_CurrentFrame = (m_CurrentFrame + 1) % Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT;
}

const vkb::Swapchain& GetSwapchain() { return m_Swapchain.Handle; }

FrameData& GetCurrentFrame()             { return m_Frames.at(m_CurrentFrame);         }
SwapchainImageData& GetCurrentImage()   { return m_Swapchain.Images.at(m_ImageIndex); }

uint32_t& GetImageIndex() { return m_ImageIndex; }

uint32_t GetCurrentFrameIndex() { return static_cast<uint32_t>(m_CurrentFrame); }

}
