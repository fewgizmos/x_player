#pragma once

#include <vulkan/vulkan_core.h>

#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace Engine::Base::Render
{

struct Buffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;

    VkDeviceSize Size = 0;
    VkDeviceAddress Address = 0;
};

struct Image
{
    VkImage Handle = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;

    VkExtent3D Extent = {};
    VkFormat Format = VK_FORMAT_UNDEFINED;

    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
};

}

namespace Engine::Base::Render::Vk::GpuResources
{

void CreateAllocator(const vkb::Instance& p_Instance);

VmaAllocator GetAllocator();

uint8_t* GetMappedData(const Buffer& p_Buffer);
void FlushMappedBuffer(const Buffer& p_Buffer, VkDeviceSize p_Offset, VkDeviceSize p_Size);

void RecordImageUpload(
    VkCommandBuffer p_Cmd, const Image& p_Target,
    const Buffer& p_Staging, VkDeviceSize p_StagingOffset = 0
);

void RecordImageUploadChunk(
    VkCommandBuffer, const Image&, const Buffer&, VkDeviceSize,
    uint32_t, uint32_t, bool, bool
);

void RecordImageMipGeneration(VkCommandBuffer p_Cmd, const Image& p_Target);
void RecordMipGeneration(VkCommandBuffer p_Cmd, const Image& p_Target);

}
