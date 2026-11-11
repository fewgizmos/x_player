#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Engine::Base::Render::ResourceLoader
{

void DestroyBuffer(Buffer& p_Target);

namespace
{

struct AcquireBuffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

struct Batch
{
    VkCommandPool Pool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

    VkCommandPool GraphicsPool = VK_NULL_HANDLE;
    VkCommandBuffer GraphicsCommandBuffer = VK_NULL_HANDLE;

    VkFence Fence = VK_NULL_HANDLE;
    VkFence GraphicsFence = VK_NULL_HANDLE;

    VkSemaphore Finished = VK_NULL_HANDLE;
    VkSemaphore TransferFinished = VK_NULL_HANDLE;

    bool Submitted = false;
    bool GraphicsSubmitted = false;
    bool GraphicsHasCommands = false;

    std::mutex CompletionMutex;
    std::condition_variable CompletionCv;
    bool RecordingFinished = false;
    std::string FailureReason;

    std::vector<std::shared_ptr<ResourceJobTicket>> Tickets;
    std::vector<Buffer> Staging;
    std::vector<AcquireBuffer> Buffers;
};

std::mutex m_Mutex;

std::vector<std::function<void(VkCommandBuffer)>> m_PendingJobs;
std::vector<std::shared_ptr<ResourceJobTicket>> m_PendingTickets;
std::vector<std::unique_ptr<Batch>> m_Batches;

thread_local Batch* m_ActiveBatch = nullptr;
std::size_t m_NextJob = 0;
bool m_Initialized = false;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("resource_loader_init", Init, 65);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("resource_loader_cleanup", Cleanup, 70);

VkSemaphore MakeSemaphore()
{
    VkSemaphoreCreateInfo Info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore Result = VK_NULL_HANDLE;
    X_ASSERT(::Engine::Base::Render::Vk::Device::GetDisp().createSemaphore(&Info, nullptr, &Result) != VK_SUCCESS,
        "failed to create resource loader semaphore");
    return Result;
}

bool IsLoaderThread()
{
    const ::Engine::Base::JobSystem::JobThread& LoaderThread = ::Engine::Base::JobSystem::It::GetThread(
        ::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh
    );

    return std::this_thread::get_id() == LoaderThread.GetThreadId();
}

void DestroyBufferNow(Buffer p_Target)
{
    vmaDestroyBuffer(
        ::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
        p_Target.Handle,
        p_Target.Allocation
    );
}

void DestroyImageNow(Image p_Target)
{
    ::Engine::Base::Render::Vk::Device::GetDisp().destroyImageView(p_Target.View, nullptr);
    vmaDestroyImage(::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
        p_Target.Handle,
        p_Target.Allocation
    );
}

void FinishBatch(Batch& p_Batch)
{
    if (!p_Batch.Submitted) {

        return;
    }

    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();
    X_ASSERT(
        Disp.waitForFences(1, &p_Batch.Fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS,
        "failed to wait for resource loader batch"
    );

    if (p_Batch.GraphicsSubmitted) {
        X_ASSERT(
            Disp.waitForFences(1, &p_Batch.GraphicsFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS,
            "failed to wait for resource loader graphics batch"
        );
    }

    for (const std::shared_ptr<ResourceJobTicket>& Ticket : p_Batch.Tickets) {
        if (!Ticket->Failed.load(std::memory_order_acquire)) {
            Ticket->Done.store(true, std::memory_order_release);
        }
    }

    for (Buffer& Staging : p_Batch.Staging) {
        DestroyBuffer(Staging);
    }

    p_Batch.Tickets.clear();
    p_Batch.Staging.clear();
    p_Batch.Buffers.clear();
    p_Batch.Submitted = false;
    p_Batch.GraphicsSubmitted = false;
    p_Batch.GraphicsHasCommands = false;
    p_Batch.RecordingFinished = false;
    p_Batch.FailureReason.clear();
}

void BeginBatch(Batch& p_Batch)
{
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    X_ASSERT(
        Disp.resetCommandBuffer(p_Batch.CommandBuffer, 0) != VK_SUCCESS,
        "failed to reset resource loader command buffer"
    );

    X_ASSERT(
        Disp.resetCommandBuffer(p_Batch.GraphicsCommandBuffer, 0) != VK_SUCCESS,
        "failed to reset resource loader graphics command buffer"
    );

    ::Engine::Base::Render::Vk::Commands::Begin(p_Batch.CommandBuffer);
    ::Engine::Base::Render::Vk::Commands::Begin(p_Batch.GraphicsCommandBuffer);
}

void RecordJobs(
    Batch& p_Batch,
    const std::vector<std::function<void(VkCommandBuffer)>>& p_Jobs,
    const std::vector<std::shared_ptr<ResourceJobTicket>>& p_Tickets
)
{
    for (std::size_t I = 0; I < p_Jobs.size(); ++I) {
        try {
            p_Jobs[I](p_Batch.CommandBuffer);
            p_Batch.Tickets.push_back(p_Tickets[I]);
        }
        catch (const std::exception& Error) {
            p_Tickets[I]->FailureReason = Error.what();
            p_Tickets[I]->Failed.store(true, std::memory_order_release);
            p_Tickets[I]->Done.store(true, std::memory_order_release);
        }
        catch (...) {
            p_Tickets[I]->FailureReason = "Unknown resource loader job failure";
            p_Tickets[I]->Failed.store(true, std::memory_order_release);
            p_Tickets[I]->Done.store(true, std::memory_order_release);
        }
    }
}

void EndBatch(Batch& p_Batch)
{
    ::Engine::Base::Render::Vk::Commands::End(p_Batch.CommandBuffer);
    ::Engine::Base::Render::Vk::Commands::End(p_Batch.GraphicsCommandBuffer);
}

void SubmitTransfer(Batch& p_Batch, const vkb::DispatchTable& p_Disp)
{
    VkCommandBufferSubmitInfo Command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    Command.commandBuffer = p_Batch.CommandBuffer;

    VkSemaphoreSubmitInfo Signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    Signal.semaphore = p_Batch.GraphicsHasCommands
        ? p_Batch.TransferFinished
        : p_Batch.Finished;
    Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &Command;
    Submit.signalSemaphoreInfoCount = 1;
    Submit.pSignalSemaphoreInfos = &Signal;

    X_ASSERT(
        p_Disp.resetFences(1, &p_Batch.Fence) != VK_SUCCESS,
        "failed to reset resource loader fence"
    );

    X_ASSERT(
        p_Disp.queueSubmit2(
            ::Engine::Base::Render::Vk::Device::GetQueues().Transfer,
            1,
            &Submit,
            p_Batch.Fence
        ) != VK_SUCCESS,
        "failed to submit resource loader batch"
    );

    p_Batch.Submitted = true;
}

void SubmitGraphics(Batch& p_Batch, const vkb::DispatchTable& p_Disp)
{
    if (!p_Batch.GraphicsHasCommands) {

        return;
    }

    VkCommandBufferSubmitInfo Command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    Command.commandBuffer = p_Batch.GraphicsCommandBuffer;

    VkSemaphoreSubmitInfo Wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    Wait.semaphore = p_Batch.TransferFinished;
    Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSemaphoreSubmitInfo Signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    Signal.semaphore = p_Batch.Finished;
    Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    Submit.waitSemaphoreInfoCount = 1;
    Submit.pWaitSemaphoreInfos = &Wait;
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &Command;
    Submit.signalSemaphoreInfoCount = 1;
    Submit.pSignalSemaphoreInfos = &Signal;

    X_ASSERT(
        p_Disp.resetFences(1, &p_Batch.GraphicsFence) != VK_SUCCESS,
        "failed to reset resource loader graphics fence"
    );

    X_ASSERT(
        p_Disp.queueSubmit2(
            ::Engine::Base::Render::Vk::Device::GetQueues().Graphics,
            1,
            &Submit,
            p_Batch.GraphicsFence
        ) != VK_SUCCESS,
        "failed to submit resource loader graphics batch"
    );

    p_Batch.GraphicsSubmitted = true;
}

void ExecuteBatch(
    Batch& p_Batch,
    std::vector<std::function<void(VkCommandBuffer)>> p_Jobs,
    std::vector<std::shared_ptr<ResourceJobTicket>> p_Tickets
)
{
    try {
        m_ActiveBatch = &p_Batch;

        const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

        BeginBatch(p_Batch);
        RecordJobs(p_Batch, p_Jobs, p_Tickets);
        EndBatch(p_Batch);
        SubmitTransfer(p_Batch, Disp);
        SubmitGraphics(p_Batch, Disp);
    }
    catch (const std::exception& Error) {
        p_Batch.FailureReason = Error.what();
    }
    catch (...) {
        p_Batch.FailureReason = "Unknown resource loader batch failure";
    }

    m_ActiveBatch = nullptr;

    {
        std::lock_guard<std::mutex> Lock(p_Batch.CompletionMutex);
        p_Batch.RecordingFinished = true;
    }
    p_Batch.CompletionCv.notify_one();
}

void RecordReleaseBuffer(VkCommandBuffer p_CommandBuffer, const AcquireBuffer& p_Buffer)
{
    const ::Engine::Base::Render::QueueSet& Queues =
        ::Engine::Base::Render::Vk::Device::GetQueues();

    VkBufferMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };

    Barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    Barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    Barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    Barrier.dstAccessMask = 0;
    Barrier.srcQueueFamilyIndex = Queues.DedicatedTransfer
        ? Queues.TransferFamily
        : VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = Queues.DedicatedTransfer
        ? Queues.GraphicsFamily
        : VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer = p_Buffer.Handle;
    Barrier.size = p_Buffer.Size;

    VkDependencyInfo Dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    Dependency.bufferMemoryBarrierCount = 1;
    Dependency.pBufferMemoryBarriers = &Barrier;
    ::Engine::Base::Render::Vk::Device::GetDisp().cmdPipelineBarrier2(
        p_CommandBuffer,
        &Dependency
    );
}

void RecordAcquire(VkCommandBuffer p_CommandBuffer, const Batch& p_Batch)
{
    const ::Engine::Base::Render::QueueSet& Queues =
        ::Engine::Base::Render::Vk::Device::GetQueues();
    if (!Queues.DedicatedTransfer
        || p_Batch.Buffers.empty()) {

        return;
    }

    std::vector<VkBufferMemoryBarrier2> Buffers;

    for (const AcquireBuffer& Source : p_Batch.Buffers) {
        VkBufferMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        Barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        Barrier.srcAccessMask = 0;
        Barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        Barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT
            | VK_ACCESS_2_MEMORY_WRITE_BIT;
        Barrier.srcQueueFamilyIndex = Queues.TransferFamily;
        Barrier.dstQueueFamilyIndex = Queues.GraphicsFamily;
        Barrier.buffer = Source.Handle;
        Barrier.size = Source.Size;
        Buffers.push_back(Barrier);
    }

    VkDependencyInfo Dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    Dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(Buffers.size());
    Dependency.pBufferMemoryBarriers = Buffers.data();
    ::Engine::Base::Render::Vk::Device::GetDisp().cmdPipelineBarrier2(
        p_CommandBuffer,
        &Dependency
    );
}

}

void Init()
{
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    m_Batches.reserve(::Engine::Configs::Static::ResourceLoader::BATCH_COUNT);

    for (uint32_t I = 0; I < ::Engine::Configs::Static::ResourceLoader::BATCH_COUNT; ++I) {
        m_Batches.push_back(std::make_unique<Batch>());

        Batch& BatchValue = *m_Batches.back();

        VkCommandPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolInfo.queueFamilyIndex = ::Engine::Base::Render::Vk::Device::GetQueues().TransferFamily;
        X_ASSERT(
            Disp.createCommandPool(&PoolInfo, nullptr, &BatchValue.Pool) != VK_SUCCESS,
            "failed to create resource loader command pool"
        );

        VkCommandBufferAllocateInfo AllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        AllocateInfo.commandPool = BatchValue.Pool;
        AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        AllocateInfo.commandBufferCount = 1;
        X_ASSERT(
            Disp.allocateCommandBuffers(&AllocateInfo, &BatchValue.CommandBuffer) != VK_SUCCESS,
            "failed to allocate resource loader command buffer"
        );

        PoolInfo.queueFamilyIndex = ::Engine::Base::Render::Vk::Device::GetQueues().GraphicsFamily;
        X_ASSERT(
            Disp.createCommandPool(&PoolInfo, nullptr, &BatchValue.GraphicsPool) != VK_SUCCESS,
            "failed to create resource loader graphics command pool"
        );

        AllocateInfo.commandPool = BatchValue.GraphicsPool;
        X_ASSERT(
            Disp.allocateCommandBuffers(&AllocateInfo, &BatchValue.GraphicsCommandBuffer) != VK_SUCCESS,
            "failed to allocate resource loader graphics command buffer"
        );

        VkFenceCreateInfo FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        X_ASSERT(
            Disp.createFence(&FenceInfo, nullptr, &BatchValue.Fence) != VK_SUCCESS,
            "failed to create resource loader fence"
        );

        X_ASSERT(
            Disp.createFence(&FenceInfo, nullptr, &BatchValue.GraphicsFence) != VK_SUCCESS,
            "failed to create resource loader graphics fence"
        );

        BatchValue.Finished = MakeSemaphore();
        BatchValue.TransferFinished = MakeSemaphore();
    }

    m_Initialized = true;
}

void Cleanup()
{
    if (!m_Initialized) {

        return;
    }

    for (const std::unique_ptr<Batch>& BatchPtr : m_Batches) {
        Batch& BatchValue = *BatchPtr;

        FinishBatch(BatchValue);

        const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();
        Disp.destroySemaphore(BatchValue.Finished, nullptr);
        Disp.destroySemaphore(BatchValue.TransferFinished, nullptr);
        Disp.destroyFence(BatchValue.Fence, nullptr);
        Disp.destroyFence(BatchValue.GraphicsFence, nullptr);
        Disp.freeCommandBuffers(
            BatchValue.Pool,
            1,
            &BatchValue.CommandBuffer
        );
        Disp.destroyCommandPool(BatchValue.Pool, nullptr);
        Disp.freeCommandBuffers(
            BatchValue.GraphicsPool,
            1,
            &BatchValue.GraphicsCommandBuffer
        );
        Disp.destroyCommandPool(BatchValue.GraphicsPool, nullptr);
    }

    m_Batches.clear();
    m_PendingJobs.clear();
    m_PendingTickets.clear();

    m_Initialized = false;
}

std::shared_ptr<ResourceJobTicket> PushFrameJobCallback(std::function<void()> p_Callback)
{
    const std::shared_ptr<ResourceJobTicket> Ticket = std::make_shared<ResourceJobTicket>();
    const std::string Name = "resource_callback_" + std::to_string(m_NextJob++);

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh,
        Name,
        [Callback = std::move(p_Callback), Ticket] {
            try {
                Callback();
                Ticket->Done.store(true, std::memory_order_release);
            }
            catch (const std::exception& Error) {
                Ticket->FailureReason = Error.what();
                Ticket->Failed.store(true, std::memory_order_release);
                Ticket->Done.store(true, std::memory_order_release);
            }
        },
        100
    );
    return Ticket;
}

std::shared_ptr<ResourceJobTicket> PushFrameJobVk(std::function<void(VkCommandBuffer)> p_Callback)
{
    const std::shared_ptr<ResourceJobTicket> Ticket = std::make_shared<ResourceJobTicket>();

    std::lock_guard<std::mutex> Lock(m_Mutex);
    m_PendingJobs.push_back(
        [Callback = std::move(p_Callback), Ticket](VkCommandBuffer p_CommandBuffer) {
            Callback(p_CommandBuffer);
        }
    );
    m_PendingTickets.push_back(Ticket);

    return Ticket;
}

void BeginFrame()
{
    if (!m_Initialized) {
        return;
    }

    Batch& BatchValue = *m_Batches.at(::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex());
    FinishBatch(BatchValue);

    std::vector<std::function<void(VkCommandBuffer)>> Jobs;
    std::vector<std::shared_ptr<ResourceJobTicket>> Tickets;

    {
        std::lock_guard<std::mutex> Lock(m_Mutex);
        Jobs.swap(m_PendingJobs);
        Tickets.swap(m_PendingTickets);
    }
    if (Jobs.empty()) {

        m_ActiveBatch = nullptr;
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(BatchValue.CompletionMutex);
        BatchValue.RecordingFinished = false;
    }

    const std::string Name = "resource_gpu_batch_" + std::to_string(m_NextJob++);
    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh,
        Name,
        [&BatchValue, Jobs = std::move(Jobs), Tickets = std::move(Tickets)]() mutable {
            ExecuteBatch(BatchValue, std::move(Jobs), std::move(Tickets));
        },
        100
    );

    std::unique_lock<std::mutex> Lock(BatchValue.CompletionMutex);
    BatchValue.CompletionCv.wait(Lock, [&BatchValue] { return BatchValue.RecordingFinished; });

    X_ASSERT(
        !BatchValue.FailureReason.empty(),
        BatchValue.FailureReason
    );
}

void RecordPendingAcquires(VkCommandBuffer p_CommandBuffer)
{
    RecordAcquire(
        p_CommandBuffer,
        *m_Batches.at(::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex())
    );
}

VkSemaphore GetTransferSemaphore()
{
    const Batch& BatchValue =
        *m_Batches.at(::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex());

    return BatchValue.Finished;
}

bool HasTransferSubmission()
{
    const Batch& BatchValue =
        *m_Batches.at(::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex());

    return BatchValue.Submitted;
}

void QueueBufferUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(m_ActiveBatch == nullptr, "buffer upload must be recorded by a frame job");

    Buffer Staging = CreateBuffer(
        p_Size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    const VmaAllocator Allocator = ::Engine::Base::Render::Vk::GpuResources::GetAllocator();

    X_ASSERT(
        vmaCopyMemoryToAllocation(
            Allocator,
            p_Data,
            Staging.Allocation,
            0,
            p_Size
        ) != VK_SUCCESS,
        "failed to upload staging buffer"
    );

    VkBufferCopy Copy{ 0, 0, p_Size };

    ::Engine::Base::Render::Vk::Device::GetDisp().cmdCopyBuffer(
        m_ActiveBatch->CommandBuffer,
        Staging.Handle,
        p_Target.Handle,
        1,
        &Copy
    );

    m_ActiveBatch->Staging.push_back(Staging);
    m_ActiveBatch->Buffers.push_back({ p_Target.Handle, p_Target.Size });
    RecordReleaseBuffer(m_ActiveBatch->CommandBuffer, m_ActiveBatch->Buffers.back());
}

void QueueImageUpload(const Image& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(m_ActiveBatch == nullptr, "image upload must be recorded by a frame job");

    Buffer Staging = CreateBuffer(
        p_Size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    const VmaAllocator Allocator = ::Engine::Base::Render::Vk::GpuResources::GetAllocator();

    X_ASSERT(
        vmaCopyMemoryToAllocation(
            Allocator,
            p_Data,
            Staging.Allocation,
            0,
            p_Size
        ) != VK_SUCCESS,
        "failed to upload staging buffer"
    );

    ::Engine::Base::Render::Vk::GpuResources::RecordImageUpload(
        m_ActiveBatch->GraphicsCommandBuffer,
        p_Target,
        Staging
    );

    m_ActiveBatch->Staging.push_back(Staging);
    m_ActiveBatch->GraphicsHasCommands = true;
}

Buffer CreateBuffer(
    VkDeviceSize p_Size,
    VkBufferUsageFlags p_Usage,
    VmaAllocationCreateFlags p_Flags
)
{
    VkBufferCreateInfo BufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInfo.size = p_Size;
    BufferInfo.usage = p_Usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo AllocationInfo{};
    AllocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    AllocationInfo.flags = p_Flags;

    Buffer Result;
    Result.Size = p_Size;

    X_ASSERT(
        vmaCreateBuffer(
            ::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
            &BufferInfo,
            &AllocationInfo,
            &Result.Handle,
            &Result.Allocation,
            nullptr
        ) != VK_SUCCESS,
        "failed to create resource buffer"
    );

    if (p_Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo Address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        Address.buffer = Result.Handle;
        Result.Address = ::Engine::Base::Render::Vk::Device::GetDisp().getBufferDeviceAddress(&Address);
    }

    return Result;
}

void DestroyBuffer(Buffer& p_Target)
{
    if (p_Target.Handle == VK_NULL_HANDLE) {

        return;
    }

    const Buffer Target = p_Target;
    p_Target = {};

    if (IsLoaderThread()) {
        DestroyBufferNow(Target);

        return;
    }

    const std::shared_ptr<std::promise<void>> Promise = std::make_shared<std::promise<void>>();
    std::future<void> Future = Promise->get_future();
    const std::string Name = "resource_destroy_buffer_" + std::to_string(m_NextJob++);

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh,
        Name,
        [Target, Promise] {
            try {
                DestroyBufferNow(Target);
                Promise->set_value();
            }
            catch (...) {
                Promise->set_exception(std::current_exception());
            }
        },
        110
    );
    Future.get();
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
    VkImageCreateInfo ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInfo.flags = p_IsCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ImageInfo.format = p_Format;
    ImageInfo.extent = p_Extent;
    ImageInfo.mipLevels = p_MipLevels;
    ImageInfo.arrayLayers = p_ArrayLayers;
    ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ImageInfo.usage = p_Usage;
    ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo AllocationInfo{};
    AllocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    AllocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    Image Result;
    Result.Extent = p_Extent;
    Result.Format = p_Format;
    Result.MipLevels = p_MipLevels;
    Result.ArrayLayers = p_ArrayLayers;

    X_ASSERT(
        vmaCreateImage(
            ::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
            &ImageInfo,
            &AllocationInfo,
            &Result.Handle,
            &Result.Allocation,
            nullptr
        ) != VK_SUCCESS,
        "failed to create resource image"
    );

    VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInfo.image = Result.Handle;
    ViewInfo.viewType = p_IsCube
        ? VK_IMAGE_VIEW_TYPE_CUBE
        : (p_ArrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    ViewInfo.format = p_Format;
    ViewInfo.subresourceRange.aspectMask = p_Aspect;
    ViewInfo.subresourceRange.levelCount = p_MipLevels;
    ViewInfo.subresourceRange.layerCount = p_ArrayLayers;
    if (::Engine::Base::Render::Vk::Device::GetDisp().createImageView(
            &ViewInfo,
            nullptr,
            &Result.View
        ) != VK_SUCCESS) {
        vmaDestroyImage(
            ::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
            Result.Handle,
            Result.Allocation
        );
        X_ASSERT(true, "failed to create resource image view");
    }
    return Result;
}

void DestroyImage(Image& p_Target)
{
    if (p_Target.Handle == VK_NULL_HANDLE) {

        return;
    }

    const Image Target = p_Target;
    p_Target = {};

    if (IsLoaderThread()) {
        DestroyImageNow(Target);

        return;
    }

    const std::shared_ptr<std::promise<void>> Promise = std::make_shared<std::promise<void>>();
    std::future<void> Future = Promise->get_future();
    const std::string Name = "resource_destroy_image_" + std::to_string(m_NextJob++);

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh,
        Name,
        [Target, Promise] {
            try {
                DestroyImageNow(Target);
                Promise->set_value();
            }
            catch (...) {
                Promise->set_exception(std::current_exception());
            }
        },
        110
    );
    Future.get();
}

VkImageView CreateImageView(
    const Image& p_Target,
    VkImageViewType p_ViewType,
    uint32_t p_BaseMip,
    uint32_t p_MipCount
)
{
    VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInfo.image = p_Target.Handle;
    ViewInfo.viewType = p_ViewType;
    ViewInfo.format = p_Target.Format;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.baseMipLevel = p_BaseMip;
    ViewInfo.subresourceRange.levelCount = p_MipCount;
    ViewInfo.subresourceRange.layerCount = p_Target.ArrayLayers;

    VkImageView Result = VK_NULL_HANDLE;
    X_ASSERT(::Engine::Base::Render::Vk::Device::GetDisp().createImageView(&ViewInfo, nullptr, &Result) != VK_SUCCESS,
        "failed to create resource image view");
    return Result;
}

void DestroyImageView(VkImageView p_View)
{
    ::Engine::Base::Render::Vk::Device::GetDisp().destroyImageView(p_View, nullptr);
}

}
