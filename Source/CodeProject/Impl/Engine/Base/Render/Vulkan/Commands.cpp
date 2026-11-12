#include "X/Engine/Base/Render/Vulkan/Commands.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <vkb/VkBootstrap.h>

namespace Engine::Base::Render::Vk::Commands
{

namespace
{

VkCommandPool m_Pool = VK_NULL_HANDLE;

}

void CreatePool()
{
    VkCommandPoolCreateInfo PoolCreateInfo = {};

    PoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolCreateInfo.queueFamilyIndex = Device::GetQueues().GraphicsFamily;

    const VkResult Result = Device::GetDisp().createCommandPool(&PoolCreateInfo, nullptr, &m_Pool);

    X_ASSERT(Result != VK_SUCCESS, "failed to create command pool");

    ArtifactsDeletor::Push([Pool = m_Pool]() {
        Device::GetDisp().destroyCommandPool(Pool, nullptr);
    });
}

VkCommandPool GetPool() { return m_Pool; }

VkCommandBuffer Allocate()
{
    VkCommandBufferAllocateInfo AllocInfo = {};

    AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocInfo.commandPool = m_Pool;
    AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;

    VkCommandBuffer Cmd = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().allocateCommandBuffers(&AllocInfo, &Cmd);

    X_ASSERT(Result != VK_SUCCESS, "failed to allocate command buffer");

    return Cmd;
}

void Reset(VkCommandBuffer p_Cmd)
{
    const VkResult Result = Device::GetDisp().resetCommandBuffer(p_Cmd, 0);

    X_ASSERT(Result != VK_SUCCESS, "failed to reset command buffer");
}

void Begin(VkCommandBuffer p_Cmd, VkCommandBufferUsageFlags p_Flags)
{
    VkCommandBufferBeginInfo BeginInfo = {};

    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = p_Flags;

    const VkResult Result = Device::GetDisp().beginCommandBuffer(p_Cmd, &BeginInfo);

    X_ASSERT(Result != VK_SUCCESS, "failed to begin command buffer");
}

void End(VkCommandBuffer p_Cmd)
{
    const VkResult Result = Device::GetDisp().endCommandBuffer(p_Cmd);

    X_ASSERT(Result != VK_SUCCESS, "failed to record command buffer");
}

void Submit(
    VkCommandBuffer p_Cmd,
    const VkSemaphoreSubmitInfo* p_Wait,
    const VkSemaphoreSubmitInfo* p_Signal,
    VkFence p_Fence,
    const VkSemaphoreSubmitInfo* p_ExtraWait,
    uint32_t p_ExtraWaitCount
)
{
    VkCommandBufferSubmitInfo CmdInfo = {};

    CmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    CmdInfo.commandBuffer = p_Cmd;

    VkSemaphoreSubmitInfo Waits[4] = {};
    uint32_t WaitCount = 0;

    if (p_Wait != nullptr) {
        Waits[WaitCount++] = *p_Wait;
    }
    for (uint32_t I = 0; I < p_ExtraWaitCount; ++I) {
        if (p_ExtraWait != nullptr) {
            Waits[WaitCount++] = p_ExtraWait[I];
        }
    }

    VkSubmitInfo2 SubmitInfo = {};
    SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    SubmitInfo.waitSemaphoreInfoCount = WaitCount;
    SubmitInfo.pWaitSemaphoreInfos = WaitCount > 0 ? Waits : nullptr;
    SubmitInfo.commandBufferInfoCount = 1;
    SubmitInfo.pCommandBufferInfos = &CmdInfo;
    SubmitInfo.signalSemaphoreInfoCount = p_Signal != nullptr ? 1 : 0;
    SubmitInfo.pSignalSemaphoreInfos = p_Signal;

    const VkResult Result = Device::QueueSubmit2(
        Device::GetQueues().Graphics,
        1,
        &SubmitInfo,
        p_Fence
    );

    X_ASSERT(Result != VK_SUCCESS, "failed to submit command buffer");
}

void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& p_Record)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    VkCommandBuffer Cmd = Allocate();

    Begin(Cmd);
    p_Record(Cmd);
    End(Cmd);

    VkFenceCreateInfo FenceCreateInfo = {};

    FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence Fence = VK_NULL_HANDLE;

    VkResult Result = Disp.createFence(&FenceCreateInfo, nullptr, &Fence);

    X_ASSERT(Result != VK_SUCCESS, "failed to create immediate Submit fence");

    Submit(Cmd, nullptr, nullptr, Fence);

    Result = Disp.waitForFences(1, &Fence, VK_TRUE, UINT64_MAX);

    Disp.destroyFence(Fence, nullptr);
    Disp.freeCommandBuffers(m_Pool, 1, &Cmd);

    X_ASSERT(Result != VK_SUCCESS, "failed to wait for immediate Submit");
}

void PushConstants(VkCommandBuffer p_Cmd, VkPipelineLayout p_Layout, VkShaderStageFlags p_Stages, uint32_t p_Size, const void* p_Data)
{
    Device::GetDisp().cmdPushConstants(p_Cmd, p_Layout, p_Stages, 0, p_Size, p_Data);
}

void Dispatch(
    VkCommandBuffer p_Cmd,
    const Pipeline& p_Target,
    const void* p_Push,
    uint32_t p_PushSize,
    uint32_t p_GroupsX,
    uint32_t p_GroupsY,
    uint32_t p_GroupsZ
)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    Disp.cmdBindPipeline(p_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_Target.Handle);

    if (p_PushSize > 0) {
        PushConstants(p_Cmd, p_Target.Layout, VK_SHADER_STAGE_COMPUTE_BIT, p_PushSize, p_Push);
    }

    Disp.cmdDispatch(p_Cmd, p_GroupsX, p_GroupsY, p_GroupsZ);
}

void MemoryBarrier(
    VkCommandBuffer p_Cmd,
    VkPipelineStageFlags2 p_SrcStage,
    VkAccessFlags2 p_SrcAccess,
    VkPipelineStageFlags2 p_DstStage,
    VkAccessFlags2 p_DstAccess
)
{
    VkMemoryBarrier2 Barrier = {};

    Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    Barrier.srcStageMask = p_SrcStage;
    Barrier.srcAccessMask = p_SrcAccess;
    Barrier.dstStageMask = p_DstStage;
    Barrier.dstAccessMask = p_DstAccess;

    VkDependencyInfo DependencyInfo = {};

    DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    DependencyInfo.memoryBarrierCount = 1;
    DependencyInfo.pMemoryBarriers = &Barrier;

    Device::GetDisp().cmdPipelineBarrier2(p_Cmd, &DependencyInfo);
}

void TransitionImage(
    VkCommandBuffer p_Cmd,
    VkImage p_Image,
    VkImageLayout p_OldLayout,
    VkImageLayout p_NewLayout,
    VkPipelineStageFlags2 p_SrcStage,
    VkAccessFlags2 p_SrcAccess,
    VkPipelineStageFlags2 p_DstStage,
    VkAccessFlags2 p_DstAccess,
    VkImageAspectFlags p_Aspect,
    uint32_t p_MipCount,
    uint32_t p_LayerCount
)
{
    VkImageMemoryBarrier2 Barrier = {};

    Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    Barrier.srcStageMask = p_SrcStage;
    Barrier.srcAccessMask = p_SrcAccess;
    Barrier.dstStageMask = p_DstStage;
    Barrier.dstAccessMask = p_DstAccess;
    Barrier.oldLayout = p_OldLayout;
    Barrier.newLayout = p_NewLayout;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image = p_Image;
    Barrier.subresourceRange.aspectMask = p_Aspect;
    Barrier.subresourceRange.baseMipLevel = 0;
    Barrier.subresourceRange.levelCount = p_MipCount;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount = p_LayerCount;

    VkDependencyInfo DependencyInfo = {};

    DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    DependencyInfo.imageMemoryBarrierCount = 1;
    DependencyInfo.pImageMemoryBarriers = &Barrier;

    Device::GetDisp().cmdPipelineBarrier2(p_Cmd, &DependencyInfo);
}

}
