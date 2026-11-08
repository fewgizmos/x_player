#pragma once

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>

namespace Engine::Base::Render::Vk::Commands
{

void CreatePool();

VkCommandPool GetPool();

VkCommandBuffer Allocate();

void Reset(VkCommandBuffer p_Cmd);

void Begin(VkCommandBuffer p_Cmd, VkCommandBufferUsageFlags p_Flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
void End(VkCommandBuffer p_Cmd);

void Submit(
    VkCommandBuffer p_Cmd,
    const VkSemaphoreSubmitInfo* p_Wait,
    const VkSemaphoreSubmitInfo* p_Signal,
    VkFence p_Fence
);

void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& p_Record);

void PushConstants(VkCommandBuffer p_Cmd, VkPipelineLayout p_Layout, VkShaderStageFlags p_Stages, uint32_t p_Size, const void* p_Data);

void Dispatch(
    VkCommandBuffer p_Cmd,
    const Pipeline& p_Target,
    const void* p_Push,
    uint32_t p_PushSize,
    uint32_t p_GroupsX,
    uint32_t p_GroupsY,
    uint32_t p_GroupsZ
);

void MemoryBarrier(
    VkCommandBuffer p_Cmd,
    VkPipelineStageFlags2 p_SrcStage,
    VkAccessFlags2 p_SrcAccess,
    VkPipelineStageFlags2 p_DstStage,
    VkAccessFlags2 p_DstAccess
);

void TransitionImage(
    VkCommandBuffer p_Cmd,
    VkImage p_Image,
    VkImageLayout p_OldLayout,
    VkImageLayout p_NewLayout,
    VkPipelineStageFlags2 p_SrcStage,
    VkAccessFlags2 p_SrcAccess,
    VkPipelineStageFlags2 p_DstStage,
    VkAccessFlags2 p_DstAccess,
    VkImageAspectFlags p_Aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t p_MipCount = 1,
    uint32_t p_LayerCount = 1
);

}
