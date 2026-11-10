#pragma once

#include <vulkan/vulkan_core.h>

#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace Engine::Base::Render
{

struct FrameData
{
    VkFence FenceInflight = VK_NULL_HANDLE;
    VkSemaphore SemaphoreAvailable = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
};

struct SwapchainImageData
{
    VkSemaphore SemaphoreFinished = VK_NULL_HANDLE;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView ImageView = VK_NULL_HANDLE;
};

}

namespace Engine::Base::Render::Vk::Frame
{

void CreateSwapchain();
void DestroySwapchain();
void RecreateSwapchain();

void CreateFrameData();

void Advance();

const vkb::Swapchain& GetSwapchain();

FrameData& GetCurrentFrame();
SwapchainImageData& GetCurrentImage();

uint32_t& GetImageIndex();

uint32_t GetCurrentFrameIndex();

}
