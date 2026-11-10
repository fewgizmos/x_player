#define VMA_IMPLEMENTATION

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace Engine::Base::Render::Vk::GpuResources
{

namespace
{

struct PendingUpload
{
    Buffer Staging;

    VkBuffer Target = VK_NULL_HANDLE;
};

struct UsedStaging
{
    Buffer Staging;

    uint32_t FrameSlot = 0;
};

struct PendingImageUpload
{
    Buffer Staging;

    VkImage Target = VK_NULL_HANDLE;

    VkExtent3D Extent = {};

    uint32_t MipLevels   = 1;
    uint32_t ArrayLayers = 1;
};

VmaAllocator m_Allocator = VK_NULL_HANDLE;

std::vector<PendingUpload> m_PendingUploads;
std::vector<PendingImageUpload> m_PendingImageUploads;

std::vector<UsedStaging> m_UsedStaging;

std::mutex m_RequestsMutex;

std::vector<std::function<void()>> m_Requests;

bool m_AcceptRequests = false;

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClose("gpu_requests_close", CloseRequests, 110);

void Enqueue(std::function<void()> p_Function)
{
    std::lock_guard<std::mutex> Lock(m_RequestsMutex);

    X_ASSERT(!m_AcceptRequests, "GPU request queue is closed");

    m_Requests.push_back(std::move(p_Function));
}

template<class Function>
auto Request(Function p_FunctionValue)
{
    using Result = std::invoke_result_t<Function>;

    auto Task = std::make_shared<std::packaged_task<Result()>>(std::move(p_FunctionValue));

    auto Future = Task->get_future();

    Enqueue([Task] {
        (*Task)();
    });

    return Future;
}

void DestroyAllUploads()
{
    for (PendingUpload& Upload : m_PendingUploads) {
        DestroyBuffer(Upload.Staging);
    }

    for (PendingImageUpload& Upload : m_PendingImageUploads) {
        DestroyBuffer(Upload.Staging);
    }

    for (UsedStaging& Used : m_UsedStaging) {
        DestroyBuffer(Used.Staging);
    }

    m_PendingUploads.clear();
    m_PendingImageUploads.clear();

    m_UsedStaging.clear();
}

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

    ArtifactsDeletor::Push([]() {
        DestroyAllUploads();
    });
}

VmaAllocator GetAllocator() { return m_Allocator; }

Buffer CreateBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage, VmaAllocationCreateFlags p_Flags)
{
    VkBufferCreateInfo BufferCreateInfo = {};

    BufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferCreateInfo.size = p_Size;
    BufferCreateInfo.usage = p_Usage;
    BufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo AllocCreateInfo = {};

    AllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    AllocCreateInfo.flags = p_Flags;

    Buffer ResultBuffer;

    ResultBuffer.Size = p_Size;

    const VkResult Result = vmaCreateBuffer(
        m_Allocator,
        &BufferCreateInfo,
        &AllocCreateInfo,
        &ResultBuffer.Handle,
        &ResultBuffer.Allocation,
        nullptr
    );

    X_ASSERT(Result != VK_SUCCESS, "failed to create buffer");

    if (p_Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo AddressInfo = {};

        AddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        AddressInfo.buffer = ResultBuffer.Handle;

        ResultBuffer.Address = Device::GetDisp().getBufferDeviceAddress(&AddressInfo);
    }

    return ResultBuffer;
}

void DestroyBuffer(Buffer& p_Target)
{
    vmaDestroyBuffer(m_Allocator, p_Target.Handle, p_Target.Allocation);

    p_Target = {};
}

void UploadBuffer(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(p_Size > p_Target.Size, "upload exceeds Buffer size");

    const VkResult Result = vmaCopyMemoryToAllocation(m_Allocator, p_Data, p_Target.Allocation, 0, p_Size);

    X_ASSERT(Result != VK_SUCCESS, "failed to upload buffer");
}

void DownloadBuffer(const Buffer& p_Source, void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(p_Size > p_Source.Size, "download exceeds Buffer size");

    const VkResult Result = vmaCopyAllocationToMemory(m_Allocator, p_Source.Allocation, 0, p_Data, p_Size);

    X_ASSERT(Result != VK_SUCCESS, "failed to download buffer");
}

void StageUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    PendingUpload Upload;

    Upload.Staging = CreateBuffer(
        p_Size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Upload.Target = p_Target.Handle;

    try {
        UploadBuffer(Upload.Staging, p_Data, p_Size);
    }

    catch (...) {
        DestroyBuffer(Upload.Staging);

        throw;
    }

    m_PendingUploads.push_back(Upload);
}

void StageImageUpload(const Image& p_Target, const void* p_Pixels, VkDeviceSize p_Size)
{
    PendingImageUpload Upload;

    Upload.Staging = CreateBuffer(
        p_Size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Upload.Target      = p_Target.Handle;
    Upload.Extent      = p_Target.Extent;
    Upload.MipLevels   = p_Target.MipLevels;
    Upload.ArrayLayers = p_Target.ArrayLayers;

    try {
        UploadBuffer(Upload.Staging, p_Pixels, p_Size);
    }

    catch (...) {
        DestroyBuffer(Upload.Staging);

        throw;
    }

    m_PendingImageUploads.push_back(Upload);
}

void RecordImageUpload(VkCommandBuffer p_Cmd, const Image& p_Target, const Buffer& p_Staging)
{
    PendingImageUpload Upload;

    Upload.Staging      = p_Staging;
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

void FlushUploads()
{
    VkCommandBuffer Cmd = Frame::GetCurrentFrame().CommandBuffer;

    for (const PendingImageUpload& Upload : m_PendingImageUploads) {
        FlushImageUpload(Cmd, Upload);

        m_UsedStaging.push_back({ Upload.Staging, Frame::GetCurrentFrameIndex() });
    }

    m_PendingImageUploads.clear();

    if (m_PendingUploads.empty()) {
        return;
    }

    for (const PendingUpload& Upload : m_PendingUploads) {
        VkBufferCopy Region = {};

        Region.size = Upload.Staging.Size;

        Device::GetDisp().cmdCopyBuffer(Cmd, Upload.Staging.Handle, Upload.Target, 1, &Region);

        m_UsedStaging.push_back({ Upload.Staging, Frame::GetCurrentFrameIndex() });
    }

    m_PendingUploads.clear();

    Commands::MemoryBarrier(
        Cmd,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        VK_ACCESS_2_MEMORY_READ_BIT
    );
}

void DestroyUsedStaging()
{
    for (auto It = m_UsedStaging.begin(); It != m_UsedStaging.end();) {
        if (It->FrameSlot != Frame::GetCurrentFrameIndex()) {
            It++;

            continue;
        }

        DestroyBuffer(It->Staging);

        It = m_UsedStaging.erase(It);
    }
}

Image CreateImage(
    VkExtent3D p_Extent,
    VkFormat p_Format,
    VkImageUsageFlags p_Usage,
    VkImageAspectFlags p_Aspect,
    uint32_t p_MipLevels,
    uint32_t p_ArrayLayers,
    bool p_IsCube
)
{
    VkImageCreateInfo ImageCreateInfo = {};

    ImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageCreateInfo.flags = p_IsCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageCreateInfo.format = p_Format;
    ImageCreateInfo.extent = p_Extent;
    ImageCreateInfo.mipLevels = p_MipLevels;
    ImageCreateInfo.arrayLayers = p_ArrayLayers;
    ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ImageCreateInfo.usage = p_Usage;
    ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo AllocCreateInfo = {};

    AllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    AllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Image ResultImage;

    ResultImage.Extent = p_Extent;
    ResultImage.Format = p_Format;
    ResultImage.MipLevels = p_MipLevels;
    ResultImage.ArrayLayers = p_ArrayLayers;

    VkResult Result = vmaCreateImage(
        m_Allocator,
        &ImageCreateInfo,
        &AllocCreateInfo,
        &ResultImage.Handle,
        &ResultImage.Allocation,
        nullptr
    );

    X_ASSERT(Result != VK_SUCCESS, "failed to create image");

    VkImageViewCreateInfo ViewCreateInfo = {};

    ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewCreateInfo.image = ResultImage.Handle;
    ViewCreateInfo.viewType = p_IsCube ? VK_IMAGE_VIEW_TYPE_CUBE : (p_ArrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    ViewCreateInfo.format = p_Format;
    ViewCreateInfo.subresourceRange.aspectMask = p_Aspect;
    ViewCreateInfo.subresourceRange.baseMipLevel = 0;
    ViewCreateInfo.subresourceRange.levelCount = p_MipLevels;
    ViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    ViewCreateInfo.subresourceRange.layerCount = p_ArrayLayers;

    Result = Device::GetDisp().createImageView(&ViewCreateInfo, nullptr, &ResultImage.View);

    if (Result != VK_SUCCESS) {
        vmaDestroyImage(m_Allocator, ResultImage.Handle, ResultImage.Allocation);

        X_ASSERT(true, "failed to create Image view");
    }

    return ResultImage;
}

Image CreateVolumeImage(VkExtent3D p_Extent, VkFormat p_Format, VkImageUsageFlags p_Usage)
{
    VkImageCreateInfo ImageCreateInfo = {};

    ImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
    ImageCreateInfo.format = p_Format;
    ImageCreateInfo.extent = p_Extent;
    ImageCreateInfo.mipLevels = 1;
    ImageCreateInfo.arrayLayers = 1;
    ImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ImageCreateInfo.usage = p_Usage;
    ImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo AllocCreateInfo = {};

    AllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    AllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Image ResultImage;

    ResultImage.Extent = p_Extent;
    ResultImage.Format = p_Format;

    VkResult Result = vmaCreateImage(
        m_Allocator,
        &ImageCreateInfo,
        &AllocCreateInfo,
        &ResultImage.Handle,
        &ResultImage.Allocation,
        nullptr
    );

    X_ASSERT(Result != VK_SUCCESS, "failed to create volume image");

    VkImageViewCreateInfo ViewCreateInfo = {};

    ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewCreateInfo.image = ResultImage.Handle;
    ViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    ViewCreateInfo.format = p_Format;
    ViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewCreateInfo.subresourceRange.levelCount = 1;
    ViewCreateInfo.subresourceRange.layerCount = 1;

    Result = Device::GetDisp().createImageView(&ViewCreateInfo, nullptr, &ResultImage.View);

    if (Result != VK_SUCCESS) {
        vmaDestroyImage(m_Allocator, ResultImage.Handle, ResultImage.Allocation);

        X_ASSERT(true, "failed to create volume Image view");
    }

    return ResultImage;
}

void DestroyImage(Image& p_Target)
{
    Device::GetDisp().destroyImageView(p_Target.View, nullptr);

    vmaDestroyImage(m_Allocator, p_Target.Handle, p_Target.Allocation);

    p_Target = {};
}

VkImageView CreateImageView(const Image& p_Target, VkImageViewType p_ViewType, uint32_t p_BaseMip, uint32_t p_MipCount)
{
    VkImageViewCreateInfo ViewCreateInfo = {};

    ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewCreateInfo.image = p_Target.Handle;
    ViewCreateInfo.viewType = p_ViewType;
    ViewCreateInfo.format = p_Target.Format;
    ViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewCreateInfo.subresourceRange.baseMipLevel = p_BaseMip;
    ViewCreateInfo.subresourceRange.levelCount = p_MipCount;
    ViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    ViewCreateInfo.subresourceRange.layerCount = p_Target.ArrayLayers;

    VkImageView View = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createImageView(&ViewCreateInfo, nullptr, &View);

    X_ASSERT(Result != VK_SUCCESS, "failed to create Image view");

    return View;
}

void DestroyImageView(VkImageView p_View)
{
    Device::GetDisp().destroyImageView(p_View, nullptr);
}

void OpenRequests()
{
    std::lock_guard<std::mutex> Lock(m_RequestsMutex);

    m_AcceptRequests = true;
}

void CloseRequests()
{
    std::vector<std::function<void()>> Cancelled;

    {
        std::lock_guard<std::mutex> Lock(m_RequestsMutex);

        m_AcceptRequests = false;

        Cancelled.swap(m_Requests);
    }

}

void ProcessRequests()
{
    std::vector<std::function<void()>> Requests;

    {
        std::lock_guard<std::mutex> Lock(m_RequestsMutex);

        Requests.swap(m_Requests);
    }

    for (auto& Function : Requests) {
        Function();
    }
}

void QueueUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(p_Size > p_Target.Size || p_Size == 0 || p_Data == nullptr, "Invalid buffer upload");

    std::vector<std::byte> Bytes(p_Size);

    std::memcpy(Bytes.data(), p_Data, p_Size);

    Enqueue([p_Target, Bytes = std::move(Bytes)] {
        StageUpload(p_Target, Bytes.data(), Bytes.size());
    });
}

void QueueImageUpload(const Image& p_Target, const void* p_Pixels, VkDeviceSize p_Size)
{
    X_ASSERT(p_Size == 0 || p_Pixels == nullptr, "Invalid image upload");

    std::vector<std::byte> Bytes(p_Size);

    std::memcpy(Bytes.data(), p_Pixels, p_Size);

    Enqueue([p_Target, Bytes = std::move(Bytes)] {
        StageImageUpload(p_Target, Bytes.data(), Bytes.size());
    });
}

std::future<Buffer> RequestCreateBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage,
    VmaAllocationCreateFlags p_Flags)
{
    return Request([=] {
        return CreateBuffer(p_Size, p_Usage, p_Flags);
    });
}

std::future<Image> RequestCreateImage(VkExtent3D p_Extent, VkFormat p_Format,
    VkImageUsageFlags p_Usage, VkImageAspectFlags p_Aspect, uint32_t p_MipLevels,
    uint32_t p_ArrayLayers, bool p_IsCube)
{
    return Request([=] {
        return CreateImage(p_Extent, p_Format, p_Usage, p_Aspect, p_MipLevels, p_ArrayLayers, p_IsCube);
    });
}

namespace
{

void DrainBeforeDestroy()
{
    if (!m_PendingUploads.empty() || !m_PendingImageUploads.empty()) {
        Commands::ImmediateSubmit([](VkCommandBuffer p_Cmd) {
            for (const auto& Upload : m_PendingUploads) {
                VkBufferCopy Region{};

                Region.size = Upload.Staging.Size;
                Device::GetDisp().cmdCopyBuffer(p_Cmd, Upload.Staging.Handle, Upload.Target, 1, &Region);
            }

            for (const auto& Upload : m_PendingImageUploads) {
                FlushImageUpload(p_Cmd, Upload);
            }

            Commands::MemoryBarrier(p_Cmd, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT);
        });

        for (auto& Upload : m_PendingUploads) {
            DestroyBuffer(Upload.Staging);
        }

        for (auto& Upload : m_PendingImageUploads) {
            DestroyBuffer(Upload.Staging);
        }

        m_PendingUploads.clear();
        m_PendingImageUploads.clear();
    }

    X_ASSERT(Device::GetDisp().deviceWaitIdle() != VK_SUCCESS, "Failed to wait before GPU resource destruction");
}

}

std::future<void> RequestDestroyBuffer(Buffer p_Target)
{
    return Request([p_Target]() mutable {
        DrainBeforeDestroy();

        DestroyBuffer(p_Target);
    });
}

std::future<void> RequestDestroyImage(Image p_Target)
{
    return Request([p_Target]() mutable {
        DrainBeforeDestroy();

        DestroyImage(p_Target);
    });
}

}
