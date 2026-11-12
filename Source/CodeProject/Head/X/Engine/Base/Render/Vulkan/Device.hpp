#pragma once

#include <vulkan/vulkan_core.h>

#include <vkb/VkBootstrap.h>

#include <cstdint>
#include <functional>
#include <mutex>

namespace Engine::Base::Render
{

struct QueueSet
{
    VkQueue Graphics = VK_NULL_HANDLE;
    VkQueue Present  = VK_NULL_HANDLE;
    VkQueue Transfer  = VK_NULL_HANDLE;

    uint32_t GraphicsFamily = 0;
    uint32_t PresentFamily  = 0;
    uint32_t TransferFamily = 0;

    bool DedicatedTransfer = false;
};

}

namespace Engine::Base::Render::Vk::Device
{

void CreateSurface(const vkb::Instance& p_Instance);
void CreateDevice(const vkb::Instance& p_Instance);

void MakeQueues();

const vkb::PhysicalDevice& GetPhysical();
const vkb::Device& GetDevice();
const vkb::DispatchTable& GetDisp();

VkSurfaceKHR GetSurface();

const QueueSet& GetQueues();
std::mutex& GetQueueSubmitMutex();
VkResult QueueSubmit2(
    VkQueue p_Queue,
    uint32_t p_SubmitCount,
    const VkSubmitInfo2* p_Submits,
    VkFence p_Fence
);
void QueueSubmit2Async(
    VkQueue p_Queue,
    uint32_t p_SubmitCount,
    const VkSubmitInfo2* p_Submits,
    VkFence p_Fence,
    std::function<void(VkResult)> p_Completion = {}
);
VkResult QueuePresent(const VkPresentInfoKHR* p_PresentInfo);
uint64_t QueueSubmitAndPresentAsync(
    const VkSubmitInfo2* p_Submit,
    VkQueue p_Queue,
    VkFence p_Fence,
    const VkPresentInfoKHR* p_PresentInfo
);
VkSemaphore GetRenderCompletionSemaphore();
uint64_t GetRenderCompletionValue();
void DestroyRenderCompletionSemaphore();
void DrainRenderOperations();
void StopQueueExecutor();
void WaitIdle();
VkResult AcquireNextImage(
    VkSwapchainKHR p_Swapchain,
    uint64_t p_Timeout,
    VkSemaphore p_Semaphore,
    VkFence p_Fence,
    uint32_t* p_ImageIndex
);
void ExecuteQueueOperation(std::function<void()> p_Operation);
VkResult QueueSubmitLegacy(
    VkQueue p_Queue,
    uint32_t p_SubmitCount,
    const VkSubmitInfo* p_Submits,
    VkFence p_Fence
);
VkResult QueueWaitIdle(VkQueue p_Queue);
VkResult GetFenceStatus(VkFence p_Fence);
VkResult ResetFence(VkFence p_Fence);
VkResult WaitForFence(VkFence p_Fence, VkBool32 p_WaitAll = VK_TRUE, uint64_t p_Timeout = UINT64_MAX);

bool HasMemoryBudget();

const VkPhysicalDeviceIDProperties& GetIdProperties();

}
