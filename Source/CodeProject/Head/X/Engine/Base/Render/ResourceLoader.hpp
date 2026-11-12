#pragma once

#include <vulkan/vulkan_core.h>

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Engine::Base::Render::ResourceLoader
{

struct ResourceJobTicket
{
    std::atomic<bool> Done = false;
    std::atomic<bool> Failed = false;
    std::atomic<bool> Rejected = false;
    uint64_t VersionChecksum = 0;
    std::string FailureReason;
};

struct GpuResourceOpsBudget
{
    VkDeviceSize UploadBytes = 0;
    uint32_t CopyCommands = 0;
    uint32_t ResourceCreates = 0;
    uint32_t ResourceDestroys = 0;
    uint32_t DescriptorUpdates = 0;
};

using Buffer = ::Engine::Base::Render::Buffer;
using Image = ::Engine::Base::Render::Image;

void Init();
void Cleanup();
void StopAcceptingJobs();

std::shared_ptr<ResourceJobTicket> PushFrameJobCallback(
    std::function<void()> p_Callback,
    int p_Order = 100,
    GpuResourceOpsBudget p_Budget = {}
);
std::shared_ptr<ResourceJobTicket> PushFrameJobVk(
    std::function<void(VkCommandBuffer)> p_Callback,
    int p_Order = 100,
    GpuResourceOpsBudget p_Budget = {}
);
void PushDeferredGpuCallback(
    std::function<void()> p_Callback,
    int p_Order = 100
);

void BeginFrame();
void AdvanceBudgetFrame();
GpuResourceOpsBudget GetBudgetUsage();
void RecordPendingAcquires(VkCommandBuffer p_CommandBuffer);
void OnRenderSubmitted(uint64_t p_RenderCompletionValue);
struct CompletionWait
{
    VkSemaphore Semaphore = VK_NULL_HANDLE;
    uint64_t Value = 0;
};

uint32_t GetCompletionWaits(CompletionWait* p_Waits, uint32_t p_Capacity);

void QueueBufferUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size);
void QueueImageUpload(const Image& p_Target, const void* p_Data, VkDeviceSize p_Size);

Buffer CreateBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage, VmaAllocationCreateFlags p_Flags = 0);
void DestroyBuffer(Buffer& p_Target);

Image CreateImage(
    VkExtent3D p_Extent,
    VkFormat p_Format,
    VkImageUsageFlags p_Usage,
    VkImageAspectFlags p_Aspect,
    uint32_t p_MipLevels = 1,
    uint32_t p_ArrayLayers = 1,
    bool p_IsCube = false
);
void DestroyImage(Image& p_Target);

VkImageView CreateImageView(
    const Image& p_Target,
    VkImageViewType p_ViewType,
    uint32_t p_BaseMip,
    uint32_t p_MipCount
);
void DestroyImageView(VkImageView p_View);

}
