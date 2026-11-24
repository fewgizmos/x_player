#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Engine::Base::Render::ResourceLoader
{

void DestroyBuffer(Buffer& p_Target);
void DrainLoaderJobs();
void ProcessFrame();

namespace
{

struct AcquireBuffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
};

struct AcquireImage
{
    VkImage Handle = VK_NULL_HANDLE;

    VkExtent3D Extent = {};

    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
};

struct PendingAcquire
{
    uint64_t TransferSubmitValue = 0;

    std::vector<AcquireBuffer> Buffers;
    std::vector<AcquireImage> Images;

    std::vector<std::shared_ptr<ResourceJobTicket>> Tickets;
};

struct PendingRenderTicket
{
    std::shared_ptr<ResourceJobTicket> Ticket;
    uint64_t CompletionValue = 0;
};

struct StagingBlock
{
    Buffer Resource;
    VkDeviceSize Offset = 0;

    uint64_t RetireValue = 0;
};

struct StagingUse
{
    StagingBlock* Block = nullptr;
    VkDeviceSize Offset = 0;
    bool Image = false;
};

struct Batch
{
    VkCommandPool Pool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

    VkCommandPool GraphicsPool = VK_NULL_HANDLE;
    VkCommandBuffer GraphicsCommandBuffer = VK_NULL_HANDLE;

    VkFence Fence = VK_NULL_HANDLE;
    VkFence GraphicsFence = VK_NULL_HANDLE;

    uint64_t TransferSubmitValue = 0;
    uint64_t GraphicsSubmitValue = 0;

    std::atomic<bool> Submitted = false;

    std::atomic<bool> GraphicsSubmitted = false;
    std::atomic<bool> SubmissionFailed  = false;

    bool GraphicsHasCommands     = false;
    bool WaitForRenderCompletion = false;

    std::mutex CompletionMutex;
    std::condition_variable CompletionCv;
    bool RecordingFinished = false;
    std::string FailureReason;

    std::vector<std::shared_ptr<ResourceJobTicket>> Tickets;

    std::vector<StagingUse>    Staging;
    std::vector<AcquireBuffer> Buffers;
    std::vector<AcquireImage>  Images;
};

struct PendingGpuJob
{
    std::function<void(VkCommandBuffer)> Callback;

    std::shared_ptr<ResourceJobTicket> Ticket;
    GpuResourceOpsBudget Budget;

    int Order = 100;

    uint32_t Age      = 0;
    uint64_t Sequence = 0;
};

struct PendingCallbackJob
{
    std::function<void()> Callback;

    std::shared_ptr<ResourceJobTicket> Ticket;
    GpuResourceOpsBudget Budget;

    int Order = 100;

    uint32_t Age      = 0;
    uint64_t Sequence = 0;
};

std::mutex m_Mutex;

std::vector<PendingGpuJob>      m_PendingGpuJobs;
std::vector<PendingCallbackJob> m_PendingCallbackJobs;

std::vector<std::unique_ptr<Batch>> m_Batches;
std::size_t m_NextBatch = 0;

std::vector<std::unique_ptr<StagingBlock>> m_BufferStaging;
std::vector<std::unique_ptr<StagingBlock>> m_ImageStaging;

std::vector<PendingAcquire> m_PendingAcquires;
std::vector<std::shared_ptr<ResourceJobTicket>> m_PendingAcquireTickets;

std::vector<PendingRenderTicket> m_PendingRenderTickets;

thread_local Batch* m_ActiveBatch = nullptr;
std::atomic<uint64_t> m_NextJob = 0;

bool m_Initialized = false;

std::atomic<bool> m_AcceptingJobs = true;

VkSemaphore m_TransferTimeline = VK_NULL_HANDLE;
VkSemaphore m_GraphicsTimeline = VK_NULL_HANDLE;

std::atomic<uint64_t> m_NextTransferValue = 0;
std::atomic<uint64_t> m_NextGraphicsValue = 0;

std::atomic<uint64_t> m_CompletedTransferValue = 0;
std::atomic<uint64_t> m_CompletedGraphicsValue = 0;
std::atomic<uint32_t> m_PendingDestroyJobs     = 0;
std::atomic<uint64_t> m_FrameTransferWaitValue = 0;

GpuResourceOpsBudget m_BudgetUsage;

uint64_t m_BudgetFrame = 0;

std::mutex m_BudgetMutex;
std::mutex m_DestroyMutex;

std::condition_variable m_DestroyCv;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("resource_loader_init", Init, 65);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("resource_loader_cleanup", Cleanup, 61);

void PollCompletedTimelines();

void RecordAcquire(
    VkCommandBuffer p_CommandBuffer,
    const std::vector<AcquireBuffer>& p_Buffers,
    const std::vector<AcquireImage>& p_Images
);

bool CanFitBudget(
    const GpuResourceOpsBudget& p_Usage,
    const GpuResourceOpsBudget& p_Job,
    bool p_AllowOversized
)
{
    if (p_AllowOversized) { return true; }

    return p_Usage.UploadBytes + p_Job.UploadBytes
        <= ::Engine::Configs::Static::ResourceLoader::GPU_UPLOAD_BYTES_PER_FRAME
        && p_Usage.CopyCommands + p_Job.CopyCommands
        <= ::Engine::Configs::Static::ResourceLoader::GPU_COPY_COMMANDS_PER_FRAME
        && p_Usage.ResourceCreates + p_Job.ResourceCreates
        <= ::Engine::Configs::Static::ResourceLoader::GPU_RESOURCE_CREATES_PER_FRAME
        && p_Usage.ResourceDestroys + p_Job.ResourceDestroys
        <= ::Engine::Configs::Static::ResourceLoader::GPU_RESOURCE_DESTROYS_PER_FRAME
        && p_Usage.DescriptorUpdates + p_Job.DescriptorUpdates
        <= ::Engine::Configs::Static::ResourceLoader::GPU_DESCRIPTOR_UPDATES_PER_FRAME;
}

void AddBudget(GpuResourceOpsBudget& p_Target, const GpuResourceOpsBudget& p_Source)
{
    p_Target.UploadBytes += p_Source.UploadBytes;
    p_Target.CopyCommands += p_Source.CopyCommands;
    p_Target.ResourceCreates += p_Source.ResourceCreates;
    p_Target.ResourceDestroys += p_Source.ResourceDestroys;
    p_Target.DescriptorUpdates += p_Source.DescriptorUpdates;
}

int GetEffectiveOrder(int p_Order, uint32_t p_Age)
{
    const int Boost = static_cast<int>(std::min<uint32_t>(
        p_Age,
        static_cast<uint32_t>(::Engine::Configs::Static::ResourceLoader::GPU_OPS_MAX_AGING_BOOST)
    ));

    return p_Order + Boost;
}

bool IsBatchComplete(Batch& p_Batch)
{
    {
        std::lock_guard<std::mutex> Lock(p_Batch.CompletionMutex);

        if (!p_Batch.RecordingFinished) { return false; }
    }

    if (!p_Batch.Submitted || p_Batch.SubmissionFailed) { return true; }

    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    if (::Engine::Base::Render::Vk::Device::GetFenceStatus(p_Batch.Fence) == VK_NOT_READY) {
        return false;
    }

    if (p_Batch.GraphicsSubmitted
        && ::Engine::Base::Render::Vk::Device::GetFenceStatus(p_Batch.GraphicsFence) == VK_NOT_READY) {

        return false;
    }

    return true;
}

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterTimelinePoll(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "resource_loader_poll_timelines",
        PollCompletedTimelines,
        10
    );

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterLoaderFrame(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "resource_loader_frame",
        ::Engine::Base::Render::ResourceLoader::ProcessFrame,
        20
    );

VkSemaphore MakeTimelineSemaphore()
{
    VkSemaphoreTypeCreateInfo TypeInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    TypeInfo.initialValue = 0;

    VkSemaphoreCreateInfo Info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    Info.pNext = &TypeInfo;
    VkSemaphore Result = VK_NULL_HANDLE;

    X_ASSERT(::Engine::Base::Render::Vk::Device::GetDisp().createSemaphore(&Info, nullptr, &Result) != VK_SUCCESS,
        "failed to create resource loader semaphore"
    );

    return Result;
}

void PollCompletedTimelines()
{
    if (!m_Initialized) { return; }

    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();
    uint64_t TransferValue = 0;
    uint64_t GraphicsValue = 0;
    uint64_t RenderValue = 0;

    X_ASSERT(
        Disp.getSemaphoreCounterValue(m_TransferTimeline, &TransferValue) != VK_SUCCESS,
        "failed to poll transfer timeline"
    );

    X_ASSERT(
        Disp.getSemaphoreCounterValue(m_GraphicsTimeline, &GraphicsValue) != VK_SUCCESS,
        "failed to poll graphics timeline"
    );

    X_ASSERT(
        Disp.getSemaphoreCounterValue(
            ::Engine::Base::Render::Vk::Device::GetRenderCompletionSemaphore(),
            &RenderValue
        ) != VK_SUCCESS,
        "failed to poll render completion timeline"
    );

    m_CompletedTransferValue.store(TransferValue, std::memory_order_release);
    m_CompletedGraphicsValue.store(GraphicsValue, std::memory_order_release);

    std::lock_guard<std::mutex> Lock(m_Mutex);

    for (std::vector<PendingRenderTicket>::iterator It = m_PendingRenderTickets.begin();
        It != m_PendingRenderTickets.end();) {
        if (It->CompletionValue > RenderValue) {
            ++It;

            continue;
        }

        It->Ticket->Done.store(true, std::memory_order_release);
        It = m_PendingRenderTickets.erase(It);
    }
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
    if (p_Target.View != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Device::GetDisp().destroyImageView(p_Target.View, nullptr);
    }

    vmaDestroyImage(::Engine::Base::Render::Vk::GpuResources::GetAllocator(),
        p_Target.Handle,
        p_Target.Allocation
    );
}

bool IsResourceUseComplete(
    uint64_t p_TransferValue,
    uint64_t p_GraphicsValue,
    uint64_t p_RenderValue
)
{
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    uint64_t CompletedValue = 0;

    if (p_TransferValue != 0) {
        X_ASSERT(
            Disp.getSemaphoreCounterValue(m_TransferTimeline, &CompletedValue) != VK_SUCCESS,
            "failed to poll transfer destruction timeline"
        );

        if (CompletedValue < p_TransferValue) { return false; }
    }

    if (p_GraphicsValue != 0) {
        X_ASSERT(
            Disp.getSemaphoreCounterValue(m_GraphicsTimeline, &CompletedValue) != VK_SUCCESS,
            "failed to poll graphics destruction timeline"
        );

        if (CompletedValue < p_GraphicsValue) { return false; }
    }

    if (p_RenderValue != 0) {
        X_ASSERT(
            Disp.getSemaphoreCounterValue(
                ::Engine::Base::Render::Vk::Device::GetRenderCompletionSemaphore(),
                &CompletedValue
            ) != VK_SUCCESS,
            "failed to poll render destruction timeline"
        );

        if (CompletedValue < p_RenderValue) { return false; }
    }

    return true;
}

void QueueDeferredBufferDestroy(
    Buffer p_Target,
    uint64_t p_TransferValue,
    uint64_t p_GraphicsValue,
    uint64_t p_RenderValue,
    std::string p_Name
)
{
    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        std::move(p_Name),
        [p_Target, p_TransferValue, p_GraphicsValue, p_RenderValue] {
            if (!IsResourceUseComplete(p_TransferValue, p_GraphicsValue, p_RenderValue)) {
                QueueDeferredBufferDestroy(
                    p_Target,
                    p_TransferValue,
                    p_GraphicsValue,
                    p_RenderValue,
                    "resource_destroy_buffer_poll"
                );

                return;
            }

            try {
                DestroyBufferNow(p_Target);
            }

            catch (const std::exception& Error) {
                std::cerr << "resource buffer destruction failed: " << Error.what() << "\n";
            }

            catch (...) {
                std::cerr << "resource buffer destruction failed: unknown error\n";
            }

            m_PendingDestroyJobs.fetch_sub(1, std::memory_order_acq_rel);
            m_DestroyCv.notify_all();
        },
        -110
    );
}

void QueueDeferredImageDestroy(
    Image p_Target,
    uint64_t p_TransferValue,
    uint64_t p_GraphicsValue,
    uint64_t p_RenderValue,
    std::string p_Name
)
{
    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        std::move(p_Name),
        [p_Target, p_TransferValue, p_GraphicsValue, p_RenderValue] {
            if (!IsResourceUseComplete(p_TransferValue, p_GraphicsValue, p_RenderValue)) {
                QueueDeferredImageDestroy(
                    p_Target,
                    p_TransferValue,
                    p_GraphicsValue,
                    p_RenderValue,
                    "resource_destroy_image_poll"
                );

                return;
            }

            try { DestroyImageNow(p_Target); }

            catch (const std::exception& Error) {
                std::cerr << "resource image destruction failed: " << Error.what() << "\n";
            }

            catch (...) {
                std::cerr << "resource image destruction failed: unknown error\n";
            }

            m_PendingDestroyJobs.fetch_sub(1, std::memory_order_acq_rel);
            m_DestroyCv.notify_all();
        },
        -110
    );
}

void QueueDeferredGpuCallback(
    std::function<void()> p_Callback,
    uint64_t p_TransferValue,
    uint64_t p_GraphicsValue,
    uint64_t p_RenderValue,
    int p_Order,
    std::string p_Name
)
{
    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        std::move(p_Name),
        [Callback = std::move(p_Callback), p_TransferValue, p_GraphicsValue, p_RenderValue,
            p_Order] {
            if (!IsResourceUseComplete(p_TransferValue, p_GraphicsValue, p_RenderValue)) {
                QueueDeferredGpuCallback(
                    Callback,
                    p_TransferValue,
                    p_GraphicsValue,
                    p_RenderValue,
                    p_Order,
                    "resource_deferred_callback_poll"
                );

                return;
            }

            try { Callback(); }

            catch (const std::exception& Error) {
                std::cerr << "deferred gpu callback failed: " << Error.what() << "\n";
            }

            catch (...) {
                std::cerr << "deferred gpu callback failed: unknown error\n";
            }
        },
        -p_Order
    );
}

VkDeviceSize AlignStagingOffset(VkDeviceSize p_Offset)
{
    return (p_Offset + 3) & ~VkDeviceSize(3);
}

StagingUse AllocateStaging(bool p_Image, VkDeviceSize p_Size)
{
    std::vector<std::unique_ptr<StagingBlock>>& Blocks =
        p_Image ? m_ImageStaging : m_BufferStaging;

    const VkDeviceSize ConfiguredBlockSize = p_Image
        ? ::Engine::Configs::Static::ResourceLoader::IMAGE_STAGING_BLOCK_SIZE
        : ::Engine::Configs::Static::ResourceLoader::BUFFER_STAGING_BLOCK_SIZE;

    const VkDeviceSize BlockSize = p_Size > ConfiguredBlockSize ? p_Size : ConfiguredBlockSize;
    const VkSemaphore Timeline = p_Image ? m_GraphicsTimeline : m_TransferTimeline;
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    while (true) {
        uint64_t CompletedValue = 0;

        X_ASSERT(
            Disp.getSemaphoreCounterValue(Timeline, &CompletedValue) != VK_SUCCESS,
            "failed to query resource loader timeline"
        );

        for (const std::unique_ptr<StagingBlock>& Block : Blocks) {
            if (Block->RetireValue != 0 && CompletedValue >= Block->RetireValue) {
                Block->Offset = 0;
                Block->RetireValue = 0;
            }

            const VkDeviceSize Offset = AlignStagingOffset(Block->Offset);
            if (Block->RetireValue == 0 && Offset + p_Size <= BlockSize) {
                Block->Offset = Offset + p_Size;

                return { Block.get(), Offset, p_Image };
            }
        }

        Blocks.push_back(std::make_unique<StagingBlock>());

        StagingBlock& Block = *Blocks.back();

        Block.Resource = CreateBuffer(
            BlockSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
    }
}

void FinishBatch(Batch& p_Batch)
{
    std::unique_lock<std::mutex> CompletionLock(p_Batch.CompletionMutex);

    p_Batch.CompletionCv.wait(
        CompletionLock,
        [&p_Batch] { return p_Batch.RecordingFinished; }
    );

    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    if (p_Batch.Submitted && !p_Batch.SubmissionFailed) {
        X_ASSERT(
            ::Engine::Base::Render::Vk::Device::WaitForFence(p_Batch.Fence) != VK_SUCCESS,
            "failed to wait for resource loader batch"
        );

        if (p_Batch.GraphicsSubmitted) {
            X_ASSERT(
                ::Engine::Base::Render::Vk::Device::WaitForFence(p_Batch.GraphicsFence) != VK_SUCCESS,
                "failed to wait for resource loader graphics batch"
            );
        }
    }

    for (const std::shared_ptr<ResourceJobTicket>& Ticket : p_Batch.Tickets) {
        if ((!p_Batch.Submitted || p_Batch.SubmissionFailed)
            && !Ticket->Done.load(std::memory_order_acquire)) {

            Ticket->FailureReason = p_Batch.FailureReason.empty()
                ? "resource loader batch was not submitted"
                : p_Batch.FailureReason;

            Ticket->Failed.store(true, std::memory_order_release);
            Ticket->Done.store(true, std::memory_order_release);
        } else if (p_Batch.Submitted
            && !p_Batch.WaitForRenderCompletion
            && !Ticket->Failed.load(std::memory_order_acquire)) {

            Ticket->Done.store(true, std::memory_order_release);
        }
    }

    if (!p_Batch.Submitted) {
        for (const StagingUse& Use : p_Batch.Staging) {
            Use.Block->Offset = 0;
            Use.Block->RetireValue = 0;
        }
    }

    p_Batch.Tickets.clear();
    p_Batch.Staging.clear();
    p_Batch.Buffers.clear();
    p_Batch.Images.clear();

    p_Batch.Submitted               = false;
    p_Batch.GraphicsSubmitted       = false;
    p_Batch.SubmissionFailed        = false;
    p_Batch.GraphicsHasCommands     = false;
    p_Batch.WaitForRenderCompletion = false;

    p_Batch.TransferSubmitValue = 0;
    p_Batch.GraphicsSubmitValue = 0;

    p_Batch.RecordingFinished = true;
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
            p_Tickets[I]->FailureReason = "unknown resource loader job failure";
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
    p_Batch.TransferSubmitValue = ++m_NextTransferValue;
    Signal.semaphore = m_TransferTimeline;
    Signal.value = p_Batch.TransferSubmitValue;
    Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &Command;
    Submit.signalSemaphoreInfoCount = 1;
    Submit.pSignalSemaphoreInfos = &Signal;

    X_ASSERT(
        ::Engine::Base::Render::Vk::Device::ResetFence(p_Batch.Fence) != VK_SUCCESS,
        "failed to reset resource loader fence"
    );

    p_Batch.Submitted = true;

    ::Engine::Base::Render::Vk::Device::QueueSubmit2Async(
        ::Engine::Base::Render::Vk::Device::GetQueues().Transfer,
        1,
        &Submit,
        p_Batch.Fence,
        [&p_Batch](VkResult p_Result) {
            if (p_Result != VK_SUCCESS) {
                p_Batch.FailureReason = "failed to submit resource loader batch";
                p_Batch.SubmissionFailed = true;
            }
        }
    );

    for (const StagingUse& Use : p_Batch.Staging) {
        if (!Use.Image) {
            Use.Block->RetireValue = p_Batch.TransferSubmitValue;
        }
    }

    if (::Engine::Base::Render::Vk::Device::GetQueues().DedicatedTransfer
        && !p_Batch.Buffers.empty()) {
        p_Batch.WaitForRenderCompletion = true;

        std::lock_guard<std::mutex> Lock(m_Mutex);
        m_PendingAcquires.push_back({
            p_Batch.TransferSubmitValue,
            p_Batch.Buffers,
            {},
            p_Batch.Tickets
        });
    }
}

void SubmitGraphics(Batch& p_Batch, const vkb::DispatchTable& p_Disp)
{
    if (!p_Batch.GraphicsHasCommands) { return; }

    VkCommandBufferSubmitInfo Command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    Command.commandBuffer = p_Batch.GraphicsCommandBuffer;

    VkSemaphoreSubmitInfo Wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    Wait.semaphore = m_TransferTimeline;
    Wait.value = p_Batch.TransferSubmitValue;
    Wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSemaphoreSubmitInfo Signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    p_Batch.GraphicsSubmitValue = ++m_NextGraphicsValue;
    Signal.semaphore = m_GraphicsTimeline;
    Signal.value = p_Batch.GraphicsSubmitValue;
    Signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    Submit.waitSemaphoreInfoCount = 1;
    Submit.pWaitSemaphoreInfos = &Wait;
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &Command;
    Submit.signalSemaphoreInfoCount = 1;
    Submit.pSignalSemaphoreInfos = &Signal;

    X_ASSERT(
        ::Engine::Base::Render::Vk::Device::ResetFence(p_Batch.GraphicsFence) != VK_SUCCESS,
        "failed to reset resource loader graphics fence"
    );

    p_Batch.GraphicsSubmitted = true;

    ::Engine::Base::Render::Vk::Device::QueueSubmit2Async(
        ::Engine::Base::Render::Vk::Device::GetQueues().Graphics,
        1,
        &Submit,
        p_Batch.GraphicsFence,
        [&p_Batch](VkResult p_Result) {
            if (p_Result != VK_SUCCESS) {
                p_Batch.FailureReason = "failed to submit resource loader graphics batch";
                p_Batch.SubmissionFailed = true;
            }
        }
    );

    for (const StagingUse& Use : p_Batch.Staging) {
        if (Use.Image) {
            Use.Block->RetireValue = p_Batch.GraphicsSubmitValue;
        }
    }
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

        for (const AcquireImage& Image : p_Batch.Images) {
            if (Image.MipLevels == 1) {
                continue;
            }

            ::Engine::Base::Render::Vk::GpuResources::RecordImageMipGeneration(
                p_Batch.GraphicsCommandBuffer,
                ::Engine::Base::Render::Image{
                    Image.Handle, VK_NULL_HANDLE, VK_NULL_HANDLE,
                    Image.Extent, VK_FORMAT_UNDEFINED, Image.MipLevels, Image.ArrayLayers
                }
            );

            p_Batch.GraphicsHasCommands = true;
        }

        EndBatch(p_Batch);
        SubmitTransfer(p_Batch, Disp);
        SubmitGraphics(p_Batch, Disp);
    }

    catch (const std::exception& Error) {
        p_Batch.FailureReason = Error.what();
    }

    catch (...) {
        p_Batch.FailureReason = "unknown resource loader batch failure";
    }

    if (!p_Batch.FailureReason.empty()) {
        for (const std::shared_ptr<ResourceJobTicket>& Ticket : p_Tickets) {
            if (Ticket->Done.load(std::memory_order_acquire)) { continue; }

            Ticket->FailureReason = p_Batch.FailureReason;
            Ticket->Failed.store(true, std::memory_order_release);
            Ticket->Done.store(true, std::memory_order_release);
        }
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

void RecordAcquire(
    VkCommandBuffer p_CommandBuffer,
    const std::vector<AcquireBuffer>& p_Buffers,
    const std::vector<AcquireImage>& p_Images
)
{
    const ::Engine::Base::Render::QueueSet& Queues = ::Engine::Base::Render::Vk::Device::GetQueues();

    if (!Queues.DedicatedTransfer || (p_Buffers.empty() && p_Images.empty())) { return; }

    std::vector<VkBufferMemoryBarrier2> Buffers;

    for (const AcquireBuffer& Source : p_Buffers) {
        VkBufferMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        Barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        Barrier.srcAccessMask = 0;
        Barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        Barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
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

    std::vector<VkImageMemoryBarrier2> Images;
    Images.reserve(p_Images.size());

    for (const AcquireImage& Source : p_Images) {
        VkImageMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        Barrier.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
        Barrier.srcAccessMask = 0;
        Barrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        Barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
        Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.srcQueueFamilyIndex = Queues.TransferFamily;
        Barrier.dstQueueFamilyIndex = Queues.GraphicsFamily;
        Barrier.image = Source.Handle;
        Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount = Source.MipLevels;
        Barrier.subresourceRange.layerCount = Source.ArrayLayers;

        Images.push_back(Barrier);
    }

    if (!Images.empty()) {
        VkDependencyInfo ImageDependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        ImageDependency.imageMemoryBarrierCount = static_cast<uint32_t>(Images.size());
        ImageDependency.pImageMemoryBarriers = Images.data();

        ::Engine::Base::Render::Vk::Device::GetDisp().cmdPipelineBarrier2(
            p_CommandBuffer, &ImageDependency
        );
    }
}

}

void Init()
{
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    m_AcceptingJobs.store(true, std::memory_order_release);
    m_NextJob.store(0, std::memory_order_release);
    m_NextTransferValue.store(0, std::memory_order_release);
    m_NextGraphicsValue.store(0, std::memory_order_release);
    m_CompletedTransferValue.store(0, std::memory_order_release);
    m_CompletedGraphicsValue.store(0, std::memory_order_release);
    m_FrameTransferWaitValue.store(0, std::memory_order_release);
    m_PendingAcquires.clear();
    m_PendingAcquireTickets.clear();
    m_PendingRenderTickets.clear();
    m_NextBatch = 0;

    {
        std::lock_guard<std::mutex> Lock(m_BudgetMutex);
        m_BudgetUsage = {};
        m_BudgetFrame = 0;
    }

    m_TransferTimeline = MakeTimelineSemaphore();
    m_GraphicsTimeline = MakeTimelineSemaphore();

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

        BatchValue.RecordingFinished = true;
    }

    m_Initialized = true;
}

void Cleanup()
{
    StopAcceptingJobs();

    if (!m_Initialized) { return; }

    BeginFrame();

    for (const std::unique_ptr<Batch>& BatchPtr : m_Batches) {
        Batch& BatchValue = *BatchPtr;

        FinishBatch(BatchValue);

        const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

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

    for (const std::unique_ptr<StagingBlock>& Block : m_BufferStaging) {
        DestroyBuffer(Block->Resource);
    }

    for (const std::unique_ptr<StagingBlock>& Block : m_ImageStaging) {
        DestroyBuffer(Block->Resource);
    }

    DrainLoaderJobs();

    m_BufferStaging.clear();
    m_ImageStaging.clear();

    std::unique_lock<std::mutex> DestroyLock(m_DestroyMutex);
    m_DestroyCv.wait(
        DestroyLock,
        [] { return m_PendingDestroyJobs.load(std::memory_order_acquire) == 0; }
    );

    m_Batches.clear();

    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    Disp.destroySemaphore(m_TransferTimeline, nullptr);
    Disp.destroySemaphore(m_GraphicsTimeline, nullptr);

    m_TransferTimeline = VK_NULL_HANDLE;
    m_GraphicsTimeline = VK_NULL_HANDLE;

    m_PendingGpuJobs.clear();
    m_PendingCallbackJobs.clear();
    m_PendingAcquires.clear();

    for (const std::shared_ptr<ResourceJobTicket>& Ticket : m_PendingAcquireTickets) {
        Ticket->FailureReason = "resource loader stopped before render completion";
        Ticket->Failed.store(true, std::memory_order_release);
        Ticket->Done.store(true, std::memory_order_release);
    }

    for (const PendingRenderTicket& Pending : m_PendingRenderTickets) {
        Pending.Ticket->FailureReason = "resource loader stopped before render completion";
        Pending.Ticket->Failed.store(true, std::memory_order_release);
        Pending.Ticket->Done.store(true, std::memory_order_release);
    }

    m_PendingAcquireTickets.clear();
    m_PendingRenderTickets.clear();

    m_Initialized = false;
}

void StopAcceptingJobs()
{
    std::lock_guard<std::mutex> Lock(m_Mutex);
    m_AcceptingJobs.store(false, std::memory_order_release);
}

void DrainJobs()
{
    DrainLoaderJobs();
}

void DrainLoaderJobs()
{
    const std::shared_ptr<std::promise<void>> Promise = std::make_shared<std::promise<void>>();
    std::future<void> Future = Promise->get_future();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "resource_loader_shutdown_drain",
        [Promise] { Promise->set_value(); },
        -1000
    );

    Future.get();
}

std::shared_ptr<ResourceJobTicket> PushFrameJobCallback(
    std::function<void()> p_Callback,
    int p_Order,
    GpuResourceOpsBudget p_Budget
)
{
    const std::shared_ptr<ResourceJobTicket> Ticket = std::make_shared<ResourceJobTicket>();

    std::lock_guard<std::mutex> Lock(m_Mutex);

    if (!m_AcceptingJobs.load(std::memory_order_acquire)) {
        Ticket->Rejected.store(true, std::memory_order_release);
        Ticket->Done.store(true, std::memory_order_release);
        Ticket->FailureReason = "resource loader is not accepting jobs";

        return Ticket;
    }

    m_PendingCallbackJobs.push_back({
        std::move(p_Callback),
        Ticket,
        p_Budget,
        p_Order,
        0,
        m_NextJob++
    });

    return Ticket;
}

std::shared_ptr<ResourceJobTicket> PushFrameJobVk(
    std::function<void(VkCommandBuffer)> p_Callback,
    int p_Order,
    GpuResourceOpsBudget p_Budget
)
{
    const std::shared_ptr<ResourceJobTicket> Ticket = std::make_shared<ResourceJobTicket>();

    std::lock_guard<std::mutex> Lock(m_Mutex);

    if (!m_AcceptingJobs.load(std::memory_order_acquire)) {
        Ticket->Rejected.store(true, std::memory_order_release);
        Ticket->Done.store(true, std::memory_order_release);
        Ticket->FailureReason = "resource loader is not accepting jobs";

        return Ticket;
    }

    m_PendingGpuJobs.push_back({
        std::move(p_Callback),
        Ticket,
        p_Budget,
        p_Order,
        0,
        m_NextJob++
    });

    return Ticket;
}

void PushDeferredGpuCallback(std::function<void()> p_Callback, int p_Order)
{
    const uint64_t TransferValue = m_NextTransferValue.load(std::memory_order_acquire);
    const uint64_t GraphicsValue = m_NextGraphicsValue.load(std::memory_order_acquire);

    const uint64_t RenderValue = ::Engine::Base::Render::Vk::Device::GetRenderCompletionValue();

    const std::string Name = "resource_deferred_callback_" + std::to_string(m_NextJob++);

    QueueDeferredGpuCallback(
        std::move(p_Callback),
        TransferValue,
        GraphicsValue,
        RenderValue,
        p_Order,
        Name
    );
}

void AdvanceBudgetFrame()
{
    std::lock_guard<std::mutex> Lock(m_Mutex);
    std::lock_guard<std::mutex> BudgetLock(m_BudgetMutex);

    m_BudgetFrame++;
    m_BudgetUsage = {};

    for (PendingGpuJob& Job : m_PendingGpuJobs) { Job.Age++; }
    for (PendingCallbackJob& Job : m_PendingCallbackJobs) { Job.Age++; }
}

GpuResourceOpsBudget GetBudgetUsage()
{
    std::lock_guard<std::mutex> Lock(m_BudgetMutex);

    return m_BudgetUsage;
}

void BeginFrame()
{
    if (!m_Initialized) { return; }

    for (const std::unique_ptr<Batch>& BatchPtr : m_Batches) {
        Batch& BatchValue = *BatchPtr;

        if (IsBatchComplete(BatchValue)) { FinishBatch(BatchValue); }
    }

    Batch* BatchValue = nullptr;

    for (std::size_t I = 0; I < m_Batches.size(); ++I) {
        const std::size_t Index = (m_NextBatch + I) % m_Batches.size();
        Batch& Candidate = *m_Batches[Index];

        if (IsBatchComplete(Candidate)) {
            BatchValue = &Candidate;
            m_NextBatch = (Index + 1) % m_Batches.size();

            break;
        }
    }

    if (BatchValue == nullptr) {
        return;
    }

    std::vector<PendingGpuJob> SelectedGpuJobs;
    std::vector<PendingCallbackJob> SelectedCallbackJobs;

    {
        std::lock_guard<std::mutex> Lock(m_Mutex);
        std::lock_guard<std::mutex> BudgetLock(m_BudgetMutex);

        while (true) {
            bool Found = false;
            bool IsGpu = false;
            std::size_t Index = 0;
            int BestOrder = 0;
            uint64_t BestSequence = 0;

            const bool AllowOversized = m_BudgetUsage.UploadBytes == 0
                && m_BudgetUsage.CopyCommands == 0
                && m_BudgetUsage.ResourceCreates == 0
                && m_BudgetUsage.ResourceDestroys == 0
                && m_BudgetUsage.DescriptorUpdates == 0;

            for (std::size_t I = 0; I < m_PendingGpuJobs.size(); ++I) {
                const PendingGpuJob& Job = m_PendingGpuJobs[I];
                const int EffectiveOrder = GetEffectiveOrder(Job.Order, Job.Age);

                if (!CanFitBudget(m_BudgetUsage, Job.Budget, AllowOversized)) {
                    continue;
                }

                if ((!Found || EffectiveOrder > BestOrder
                        || (EffectiveOrder == BestOrder && Job.Sequence < BestSequence))) {
                    Found = true;
                    IsGpu = true;
                    Index = I;
                    BestOrder = EffectiveOrder;
                    BestSequence = Job.Sequence;
                }
            }

            for (std::size_t I = 0; I < m_PendingCallbackJobs.size(); ++I) {
                const PendingCallbackJob& Job = m_PendingCallbackJobs[I];
                const int EffectiveOrder = GetEffectiveOrder(Job.Order, Job.Age);

                if (!CanFitBudget(m_BudgetUsage, Job.Budget, AllowOversized)) {
                    continue;
                }

                if ((!Found || EffectiveOrder > BestOrder
                        || (EffectiveOrder == BestOrder && Job.Sequence < BestSequence))) {
                    Found = true;
                    IsGpu = false;
                    Index = I;
                    BestOrder = EffectiveOrder;
                    BestSequence = Job.Sequence;
                }
            }

            if (!Found) { break; }

            const GpuResourceOpsBudget& JobBudget = IsGpu
                ? m_PendingGpuJobs[Index].Budget
                : m_PendingCallbackJobs[Index].Budget;

            AddBudget(m_BudgetUsage, JobBudget);

            if (IsGpu) {
                SelectedGpuJobs.push_back(std::move(m_PendingGpuJobs[Index]));
                m_PendingGpuJobs.erase(m_PendingGpuJobs.begin() + static_cast<std::ptrdiff_t>(Index));
            } else {
                SelectedCallbackJobs.push_back(std::move(m_PendingCallbackJobs[Index]));
                m_PendingCallbackJobs.erase(
                    m_PendingCallbackJobs.begin() + static_cast<std::ptrdiff_t>(Index)
                );
            }
        }
    }

    for (PendingCallbackJob& Job : SelectedCallbackJobs) {
        const std::string Name = "resource_callback_" + std::to_string(Job.Sequence);

        ::Engine::Base::JobSystem::It::PushBackground(
            ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
            Name,
            [Callback = std::move(Job.Callback), Ticket = Job.Ticket] {
                try {
                    Callback();

                    Ticket->Done.store(true, std::memory_order_release);
                }

                catch (const std::exception& Error) {
                    Ticket->FailureReason = Error.what();
                    Ticket->Failed.store(true, std::memory_order_release);
                    Ticket->Done.store(true, std::memory_order_release);
                }

                catch (...) {
                    Ticket->FailureReason = "unknown resource loader callback failure";
                    Ticket->Failed.store(true, std::memory_order_release);
                    Ticket->Done.store(true, std::memory_order_release);
                }
            },
            Job.Order
        );
    }

    if (SelectedGpuJobs.empty()) {
        m_ActiveBatch = nullptr;

        return;
    }

    std::vector<std::function<void(VkCommandBuffer)>> Jobs;
    std::vector<std::shared_ptr<ResourceJobTicket>> Tickets;

    for (PendingGpuJob& Job : SelectedGpuJobs) {
        Jobs.push_back(std::move(Job.Callback));
        Tickets.push_back(std::move(Job.Ticket));
    }

    {
        std::lock_guard<std::mutex> Lock(BatchValue->CompletionMutex);
        BatchValue->RecordingFinished = false;
    }

    const std::string Name = "resource_gpu_batch_" + std::to_string(m_NextJob++);
    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        Name,
        [BatchValue, Jobs = std::move(Jobs), Tickets = std::move(Tickets)]() mutable {
            ExecuteBatch(*BatchValue, std::move(Jobs), std::move(Tickets));
        },
        100
    );

}

void ProcessFrame()
{
    if (!m_Initialized) { return; }

    AdvanceBudgetFrame();
    BeginFrame();
}

void RecordPendingAcquires(VkCommandBuffer p_CommandBuffer)
{
    std::vector<PendingAcquire> Acquires;
    {
        std::lock_guard<std::mutex> Lock(m_Mutex);
        Acquires.swap(m_PendingAcquires);
    }

    uint64_t TransferWaitValue = 0;

    for (const PendingAcquire& Acquire : Acquires) {
        RecordAcquire(p_CommandBuffer, Acquire.Buffers, Acquire.Images);
        TransferWaitValue = std::max(TransferWaitValue, Acquire.TransferSubmitValue);

        std::lock_guard<std::mutex> Lock(m_Mutex);

        m_PendingAcquireTickets.insert(
            m_PendingAcquireTickets.end(),
            Acquire.Tickets.begin(),
            Acquire.Tickets.end()
        );
    }

    m_FrameTransferWaitValue.store(TransferWaitValue, std::memory_order_release);
}

void OnRenderSubmitted(uint64_t p_RenderCompletionValue)
{
    std::lock_guard<std::mutex> Lock(m_Mutex);

    for (const std::shared_ptr<ResourceJobTicket>& Ticket : m_PendingAcquireTickets) {
        m_PendingRenderTickets.push_back({ Ticket, p_RenderCompletionValue });
    }

    m_PendingAcquireTickets.clear();
}

uint32_t GetCompletionWaits(CompletionWait* p_Waits, uint32_t p_Capacity)
{
    if (p_Waits == nullptr || p_Capacity == 0) { return 0; }

    uint32_t Count = 0;
    const std::function<void(VkSemaphore, uint64_t)> AddWait =
        [&p_Waits, &Count, p_Capacity](VkSemaphore p_Semaphore, uint64_t p_Value) {
            for (uint32_t I = 0; I < Count; ++I) {
                if (p_Waits[I].Semaphore == p_Semaphore) {
                    p_Waits[I].Value = std::max(p_Waits[I].Value, p_Value);

                    return;
                }
            }

            if (Count < p_Capacity) { p_Waits[Count++] = { p_Semaphore, p_Value }; }
        };

    const uint64_t TransferWaitValue = m_FrameTransferWaitValue.load(std::memory_order_acquire);

    if (TransferWaitValue != 0) { AddWait(m_TransferTimeline, TransferWaitValue); }

    return Count;
}

void QueueBufferUpload(const Buffer& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(m_ActiveBatch == nullptr, "buffer upload must be recorded by a frame job");

    const StagingUse Use = AllocateStaging(false, p_Size);
    const Buffer& Staging = Use.Block->Resource;

    const VmaAllocator Allocator = ::Engine::Base::Render::Vk::GpuResources::GetAllocator();

    X_ASSERT(
        vmaCopyMemoryToAllocation(
            Allocator,
            p_Data,
            Staging.Allocation,
            Use.Offset,
            p_Size
        ) != VK_SUCCESS,
        "failed to upload staging buffer"
    );

    VkBufferCopy Copy{ Use.Offset, 0, p_Size };

    ::Engine::Base::Render::Vk::Device::GetDisp().cmdCopyBuffer(
        m_ActiveBatch->CommandBuffer,
        Staging.Handle,
        p_Target.Handle,
        1,
        &Copy
    );

    m_ActiveBatch->Staging.push_back(Use);
    m_ActiveBatch->Buffers.push_back({ p_Target.Handle, p_Target.Size });

    RecordReleaseBuffer(m_ActiveBatch->CommandBuffer, m_ActiveBatch->Buffers.back());
}

void QueueImageUpload(const Image& p_Target, const void* p_Data, VkDeviceSize p_Size)
{
    X_ASSERT(m_ActiveBatch == nullptr, "image upload must be recorded by a frame job");

    const StagingUse Use = AllocateStaging(true, p_Size);
    const Buffer& Staging = Use.Block->Resource;

    const VmaAllocator Allocator = ::Engine::Base::Render::Vk::GpuResources::GetAllocator();

    X_ASSERT(
        vmaCopyMemoryToAllocation(
            Allocator,
            p_Data,
            Staging.Allocation,
            Use.Offset,
            p_Size
        ) != VK_SUCCESS,
        "failed to upload staging buffer"
    );

    ::Engine::Base::Render::Vk::GpuResources::RecordImageUpload(
        m_ActiveBatch->CommandBuffer,
        p_Target,
        Staging,
        Use.Offset
    );

    m_ActiveBatch->Staging.push_back(Use);

    if (::Engine::Base::Render::Vk::Device::GetQueues().DedicatedTransfer) {
        m_ActiveBatch->Images.push_back({ p_Target.Handle, p_Target.Extent, p_Target.MipLevels, p_Target.ArrayLayers });
    } else {
        m_ActiveBatch->GraphicsHasCommands = true;
    }
}

void QueueImageUploadChunk(const Image& Target, const void* Data, VkDeviceSize Size,
    uint32_t FirstRow, uint32_t RowCount, bool FirstChunk, bool LastChunk)
{
    X_ASSERT(m_ActiveBatch == nullptr, "image upload must be recorded by a frame job");

    const StagingUse Use = AllocateStaging(true, Size);

    X_ASSERT(vmaCopyMemoryToAllocation(
        ::Engine::Base::Render::Vk::GpuResources::GetAllocator(), Data,
        Use.Block->Resource.Allocation, Use.Offset, Size
    ) != VK_SUCCESS, "failed to upload staging chunk");

    ::Engine::Base::Render::Vk::GpuResources::RecordImageUploadChunk(
        m_ActiveBatch->CommandBuffer, Target, Use.Block->Resource, Use.Offset,
        FirstRow, RowCount, FirstChunk, LastChunk);

    m_ActiveBatch->Staging.push_back(Use);

    if (!::Engine::Base::Render::Vk::Device::GetQueues().DedicatedTransfer) {
        m_ActiveBatch->GraphicsHasCommands = true;
    }
}

Buffer CreateMappedBuffer(VkDeviceSize p_Size, VkBufferUsageFlags p_Usage)
{
    return CreateBuffer(
        p_Size, p_Usage,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
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
    if (p_Target.Handle == VK_NULL_HANDLE) { return; }

    const Buffer Target = p_Target;
    p_Target = {};

    const std::string Name = "resource_destroy_buffer_" + std::to_string(m_NextJob++);
    const uint64_t TransferValue = m_NextTransferValue.load(std::memory_order_acquire);
    const uint64_t GraphicsValue = m_NextGraphicsValue.load(std::memory_order_acquire);

    const uint64_t RenderValue = ::Engine::Base::Render::Vk::Device::GetRenderCompletionValue();

    m_PendingDestroyJobs.fetch_add(1, std::memory_order_acq_rel);

    QueueDeferredBufferDestroy(
        Target,
        TransferValue,
        GraphicsValue,
        RenderValue,
        Name
    );
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

    uint32_t QueueFamilies[2] = {
        ::Engine::Base::Render::Vk::Device::GetQueues().TransferFamily,
        ::Engine::Base::Render::Vk::Device::GetQueues().GraphicsFamily
    };

    if (::Engine::Base::Render::Vk::Device::GetQueues().DedicatedTransfer) {
        ImageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        ImageInfo.queueFamilyIndexCount = 2;
        ImageInfo.pQueueFamilyIndices = QueueFamilies;
    } else {
        ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

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

        Result.Handle = VK_NULL_HANDLE;
        Result.Allocation = VK_NULL_HANDLE;

        X_ASSERT(true, "failed to create resource image view");
    }

    return Result;
}

void DestroyImage(Image& p_Target)
{
    if (p_Target.Handle == VK_NULL_HANDLE) { return; }

    const Image Target = p_Target;
    p_Target = {};

    const std::string Name = "resource_destroy_image_" + std::to_string(m_NextJob++);
    const uint64_t TransferValue = m_NextTransferValue.load(std::memory_order_acquire);
    const uint64_t GraphicsValue = m_NextGraphicsValue.load(std::memory_order_acquire);
    const uint64_t RenderValue =
        ::Engine::Base::Render::Vk::Device::GetRenderCompletionValue();

    m_PendingDestroyJobs.fetch_add(1, std::memory_order_acq_rel);

    QueueDeferredImageDestroy(
        Target,
        TransferValue,
        GraphicsValue,
        RenderValue,
        Name
    );
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

    ViewInfo.subresourceRange.aspectMask =
           p_Target.Format == VK_FORMAT_D16_UNORM
        || p_Target.Format == VK_FORMAT_X8_D24_UNORM_PACK32
        || p_Target.Format == VK_FORMAT_D32_SFLOAT
        || p_Target.Format == VK_FORMAT_D16_UNORM_S8_UINT
        || p_Target.Format == VK_FORMAT_D24_UNORM_S8_UINT
        || p_Target.Format == VK_FORMAT_D32_SFLOAT_S8_UINT
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

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
