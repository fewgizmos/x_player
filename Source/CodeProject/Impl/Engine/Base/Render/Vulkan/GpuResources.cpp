#define VMA_IMPLEMENTATION

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include <algorithm>

namespace Engine::Base::Render::Vk::GpuResources
{

namespace
{

struct PendingImageUpload
{
    Buffer Staging;
    VkDeviceSize StagingOffset = 0;

    VkImage Target = VK_NULL_HANDLE;

    VkExtent3D Extent = {};

    uint32_t MipLevels   = 1;
    uint32_t ArrayLayers = 1;
};

VmaAllocator m_Allocator = VK_NULL_HANDLE;

void TransitionMips(
    VkCommandBuffer p_Cmd,
    VkImage p_Target,
    uint32_t p_BaseMip,
    uint32_t p_MipCount,
    uint32_t p_LayerCount,
    VkImageLayout p_OldLayout,
    VkImageLayout p_NewLayout,
    VkPipelineStageFlags2 p_SrcStage,
    VkAccessFlags2 p_SrcAccess,
    VkPipelineStageFlags2 p_DstStage,
    VkAccessFlags2 p_DstAccess
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
    Barrier.image = p_Target;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel = p_BaseMip;
    Barrier.subresourceRange.levelCount = p_MipCount;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount = p_LayerCount;

    VkDependencyInfo DependencyInfo = {};

    DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    DependencyInfo.imageMemoryBarrierCount = 1;
    DependencyInfo.pImageMemoryBarriers = &Barrier;

    Device::GetDisp().cmdPipelineBarrier2(p_Cmd, &DependencyInfo);
}

void GenerateMips(VkCommandBuffer p_Cmd, const PendingImageUpload& p_Upload);

void FlushImageUpload(VkCommandBuffer p_Cmd, const PendingImageUpload& p_Upload)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    TransitionMips(
        p_Cmd,
        p_Upload.Target,
        0,
        p_Upload.MipLevels,
        p_Upload.ArrayLayers,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,
        0,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT
    );

    VkBufferImageCopy Region = {};

    Region.bufferOffset = p_Upload.StagingOffset;

    Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Region.imageSubresource.mipLevel = 0;
    Region.imageSubresource.baseArrayLayer = 0;
    Region.imageSubresource.layerCount = p_Upload.ArrayLayers;
    Region.imageExtent = p_Upload.Extent;

    Disp.cmdCopyBufferToImage(
        p_Cmd,
        p_Upload.Staging.Handle,
        p_Upload.Target,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &Region
    );

    GenerateMips(p_Cmd, p_Upload);
}

void GenerateMips(VkCommandBuffer p_Cmd, const PendingImageUpload& p_Upload)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    int32_t MipWidth  = static_cast<int32_t>(p_Upload.Extent.width);
    int32_t MipHeight = static_cast<int32_t>(p_Upload.Extent.height);

    for (uint32_t Mip = 1; Mip < p_Upload.MipLevels; Mip++) {
        const int32_t NextWidth  = std::max(MipWidth / 2, 1);
        const int32_t NextHeight = std::max(MipHeight / 2, 1);

        TransitionMips(
            p_Cmd,
            p_Upload.Target,
            Mip - 1,
            1,
            p_Upload.ArrayLayers,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT
        );

        VkImageBlit Blit = {};

        Blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Blit.srcSubresource.mipLevel = Mip - 1;
        Blit.srcSubresource.baseArrayLayer = 0;
        Blit.srcSubresource.layerCount = p_Upload.ArrayLayers;
        Blit.srcOffsets[1] = { MipWidth, MipHeight, 1 };
        Blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Blit.dstSubresource.mipLevel = Mip;
        Blit.dstSubresource.baseArrayLayer = 0;
        Blit.dstSubresource.layerCount = p_Upload.ArrayLayers;
        Blit.dstOffsets[1] = { NextWidth, NextHeight, 1 };

        Disp.cmdBlitImage(
            p_Cmd,
            p_Upload.Target,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            p_Upload.Target,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &Blit,
            VK_FILTER_LINEAR
        );

        MipWidth  = NextWidth;
        MipHeight = NextHeight;
    }

    if (p_Upload.MipLevels > 1) {
        TransitionMips(
            p_Cmd,
            p_Upload.Target,
            0,
            p_Upload.MipLevels - 1,
            p_Upload.ArrayLayers,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );
    }

    TransitionMips(
        p_Cmd,
        p_Upload.Target,
        p_Upload.MipLevels - 1,
        1,
        p_Upload.ArrayLayers,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
    );
}

}

void CreateAllocator(const vkb::Instance& p_Instance)
{
    VmaVulkanFunctions Functions = {};

    Functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    Functions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo CreateInfo = {};

    CreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (Device::HasMemoryBudget()) {
        CreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    }

    CreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    CreateInfo.instance = p_Instance.instance;
    CreateInfo.physicalDevice = Device::GetPhysical().physical_device;
    CreateInfo.device = Device::GetDevice().device;
    CreateInfo.pVulkanFunctions = &Functions;

    const VkResult Result = vmaCreateAllocator(&CreateInfo, &m_Allocator);

    X_ASSERT(Result != VK_SUCCESS, "failed to create vma allocator");

    ArtifactsDeletor::Push([Allocator = m_Allocator]() {
        vmaDestroyAllocator(Allocator);
        m_Allocator = VK_NULL_HANDLE;
    });

}

VmaAllocator GetAllocator() { return m_Allocator; }

void RecordImageUpload(
    VkCommandBuffer p_Cmd,
    const Image& p_Target,
    const Buffer& p_Staging,
    VkDeviceSize p_StagingOffset
)
{
    PendingImageUpload Upload;

    Upload.Staging      = p_Staging;
    Upload.StagingOffset = p_StagingOffset;
    Upload.Target       = p_Target.Handle;
    Upload.Extent       = p_Target.Extent;
    Upload.MipLevels   = p_Target.MipLevels;
    Upload.ArrayLayers = p_Target.ArrayLayers;

    FlushImageUpload(p_Cmd, Upload);
}

void RecordMipGeneration(VkCommandBuffer p_Cmd, const Image& p_Target)
{
    PendingImageUpload Upload;

    Upload.Target       = p_Target.Handle;
    Upload.Extent       = p_Target.Extent;
    Upload.MipLevels   = p_Target.MipLevels;
    Upload.ArrayLayers = p_Target.ArrayLayers;

    GenerateMips(p_Cmd, Upload);
}

}
