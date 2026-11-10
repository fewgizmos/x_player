#pragma once

#include <vulkan/vulkan_core.h>

#include <vkb/VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <future>

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

Buffer CreateBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage, VmaAllocationCreateFlags p_Flags = 0);
void DestroyBuffer(Buffer& p_Target);

void UploadBuffer(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size);
void DownloadBuffer(const Buffer& p_Source, void* p_Data, VkDeviceSize p_Size);

void QueueUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size);
void QueueImageUpload(const Image& p_Target, const void* p_Pixels, VkDeviceSize p_Size);

void RecordImageUpload(VkCommandBuffer p_Cmd, const Image& p_Target, const Buffer& p_Staging);
void RecordMipGeneration(VkCommandBuffer p_Cmd, const Image& p_Target);

void FlushUploads();
void DestroyUsedStaging();

Image CreateImage(
    VkExtent3D p_Extent,
    VkFormat p_Format,
    VkImageUsageFlags p_Usage,
    VkImageAspectFlags p_Aspect,
    uint32_t p_MipLevels = 1,
    uint32_t p_ArrayLayers = 1,
    bool p_IsCube = false
);

Image CreateVolumeImage(VkExtent3D p_Extent, VkFormat p_Format, VkImageUsageFlags p_Usage);

void DestroyImage(Image& p_Target);

VkImageView CreateImageView(const Image& p_Target, VkImageViewType p_ViewType, uint32_t p_BaseMip, uint32_t p_MipCount);
void DestroyImageView(VkImageView p_View);

void OpenRequests();
void CloseRequests();
void ProcessRequests();

std::future<Buffer> RequestCreateBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage,
    VmaAllocationCreateFlags p_Flags = 0);
std::future<Image> RequestCreateImage(VkExtent3D p_Extent, VkFormat p_Format,
    VkImageUsageFlags p_Usage, VkImageAspectFlags p_Aspect, uint32_t p_MipLevels = 1,
    uint32_t p_ArrayLayers = 1, bool p_IsCube = false);
std::future<void> RequestDestroyBuffer(Buffer p_Target);
std::future<void> RequestDestroyImage(Image p_Target);

}
