#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"
#include "X/Engine/Base/Viewport/Surface.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <string>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <thread>

namespace Engine::Base::Render::Vk::Device
{

namespace
{

VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

vkb::PhysicalDevice m_Physical;
vkb::Device m_Device;
vkb::DispatchTable m_Disp;

QueueSet m_Queues;
std::mutex m_QueueSubmitMutex;
std::atomic<uint64_t> m_NextQueueOperation = 0;
std::atomic<uint64_t> m_NextRenderCompletionValue = 0;
VkSemaphore m_RenderCompletionSemaphore = VK_NULL_HANDLE;
std::mutex m_QueueExecutorMutex;
std::condition_variable m_QueueExecutorCv;
std::deque<std::function<void()>> m_QueueExecutorJobs;
std::jthread m_QueueExecutorThread;
std::thread::id m_QueueExecutorThreadId;
bool m_QueueExecutorStopping = false;

bool m_HasMemoryBudget = false;

VkPhysicalDeviceIDProperties m_IdProperties = {};

void StartQueueExecutor()
{
    m_QueueExecutorStopping = false;
    m_QueueExecutorThread = std::jthread([](std::stop_token p_Stop) {
        while (!p_Stop.stop_requested()) {
            std::function<void()> Job;
            {
                std::unique_lock<std::mutex> Lock(m_QueueExecutorMutex);
                m_QueueExecutorCv.wait(Lock, [&p_Stop] {
                    return p_Stop.stop_requested()
                        || m_QueueExecutorStopping
                        || !m_QueueExecutorJobs.empty();
                });

                if ((p_Stop.stop_requested() || m_QueueExecutorStopping)
                    && m_QueueExecutorJobs.empty()) {
                    return;
                }

                Job = std::move(m_QueueExecutorJobs.front());
                m_QueueExecutorJobs.pop_front();
            }

            Job();
        }
    });
    m_QueueExecutorThreadId = m_QueueExecutorThread.get_id();
}

void StopQueueExecutorInternal()
{
    {
        std::lock_guard<std::mutex> Lock(m_QueueExecutorMutex);
        m_QueueExecutorStopping = true;
    }

    m_QueueExecutorThread.request_stop();
    m_QueueExecutorCv.notify_all();
    m_QueueExecutorThread = {};
    m_QueueExecutorThreadId = {};
    m_QueueExecutorJobs.clear();
}

void PushQueueExecutorJob(std::function<void()> p_Job)
{
    bool ExecuteImmediately = false;

    {
        std::lock_guard<std::mutex> Lock(m_QueueExecutorMutex);

        if (std::this_thread::get_id() == m_QueueExecutorThreadId) {
            ExecuteImmediately = true;
        } else {
            m_QueueExecutorJobs.push_back(std::move(p_Job));
        }
    }

    if (ExecuteImmediately) {
        p_Job();
        return;
    }

    m_QueueExecutorCv.notify_one();
}

}

void StopQueueExecutor()
{
    StopQueueExecutorInternal();
}

void CreateSurface(const vkb::Instance& p_Instance)
{
    const bool Created = SDL_Vulkan_CreateSurface(Viewport::Surface::GetNative(), p_Instance, nullptr, &m_Surface);

    X_ASSERT(!Created, SDL_GetError());

    ArtifactsDeletor::Push([p_Instance, Surface = m_Surface]() {
        vkb::destroy_surface(p_Instance, Surface);
        m_Surface = VK_NULL_HANDLE;
    });
}

void CreateDevice(const vkb::Instance& p_Instance)
{
    VkPhysicalDeviceVulkan13Features Features13 = {};

    Features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    Features13.dynamicRendering = VK_TRUE;
    Features13.synchronization2 = VK_TRUE;
    Features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceVulkan12Features Features12 = {};

    Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    Features12.bufferDeviceAddress = VK_TRUE;
    Features12.timelineSemaphore = VK_TRUE;
    Features12.descriptorIndexing = VK_TRUE;
    Features12.runtimeDescriptorArray = VK_TRUE;
    Features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    Features12.descriptorBindingPartiallyBound = VK_TRUE;
    Features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    Features12.drawIndirectCount = VK_TRUE;

    VkPhysicalDeviceFeatures Features = {};

    Features.samplerAnisotropy = VK_TRUE;
    Features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    Features.depthClamp = VK_TRUE;
    Features.shaderClipDistance = VK_TRUE;
    Features.multiDrawIndirect = VK_TRUE;
    Features.drawIndirectFirstInstance = VK_TRUE;
    Features.shaderStorageImageExtendedFormats = VK_TRUE;
    Features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    Features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    vkb::PhysicalDeviceSelector PhysDeviceSelector(p_Instance);

    auto PhysDeviceRet = PhysDeviceSelector
        .set_surface(m_Surface)
        .set_minimum_version(1, 3)
        .set_required_features(Features)
    .set_required_features_12(Features12)
    .set_required_features_13(Features13)
        .select();

    if (!PhysDeviceRet) {
        std::string Message = PhysDeviceRet.error().message();

        for (const std::string& Reason : PhysDeviceRet.detailed_failure_reasons()) {
            Message += "\n    " + Reason;
        }

        X_ASSERT(true, Message);
    }

    m_Physical = PhysDeviceRet.value();

    m_HasMemoryBudget = m_Physical.enable_extension_if_present(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    m_IdProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 Properties2 = {};

    Properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    Properties2.pNext = &m_IdProperties;

    vkGetPhysicalDeviceProperties2(m_Physical.physical_device, &Properties2);

    m_IdProperties.pNext = nullptr;

    vkb::DeviceBuilder DeviceBuilder(m_Physical);

    auto DeviceRet = DeviceBuilder.build();

    X_ASSERT(!DeviceRet, DeviceRet.error().message());

    m_Device = DeviceRet.value();
    m_Disp   = m_Device.make_table();
    StartQueueExecutor();

    VkSemaphoreTypeCreateInfo TypeInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    TypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    TypeInfo.initialValue = 0;

    VkSemaphoreCreateInfo SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    SemaphoreInfo.pNext = &TypeInfo;
    X_ASSERT(
        m_Disp.createSemaphore(&SemaphoreInfo, nullptr, &m_RenderCompletionSemaphore) != VK_SUCCESS,
        "failed to create render completion semaphore"
    );

    ArtifactsDeletor::Push([] {
        DestroyRenderCompletionSemaphore();
    });

    ArtifactsDeletor::Push([]() {
        vkb::destroy_device(m_Device);
        m_Device = {};
        m_Disp = {};
    });
}

void MakeQueues()
{
    auto Gq = m_Device.get_queue(vkb::QueueType::graphics);

    X_ASSERT(!Gq.has_value(), "failed to Get graphics queue: " + Gq.error().message());

    m_Queues.Graphics = Gq.value();

    auto Pq = m_Device.get_queue(vkb::QueueType::present);

    X_ASSERT(!Pq.has_value(), "failed to Get present queue: " + Pq.error().message());

    m_Queues.Present = Pq.value();

    m_Queues.GraphicsFamily = m_Device.get_queue_index(vkb::QueueType::graphics).value();
    m_Queues.PresentFamily  = m_Device.get_queue_index(vkb::QueueType::present).value();

    const vkb::Result<VkQueue> Tq = m_Device.get_queue(vkb::QueueType::transfer);
    if (Tq.has_value()) {
        m_Queues.Transfer = Tq.value();
        m_Queues.TransferFamily = m_Device.get_queue_index(vkb::QueueType::transfer).value();
        m_Queues.DedicatedTransfer = m_Queues.TransferFamily != m_Queues.GraphicsFamily;
    } else {
        m_Queues.Transfer = m_Queues.Graphics;
        m_Queues.TransferFamily = m_Queues.GraphicsFamily;
    }
}

const vkb::PhysicalDevice& GetPhysical() { return m_Physical; }
const vkb::Device& GetDevice()           { return m_Device; }
const vkb::DispatchTable& GetDisp()      { return m_Disp; }

VkSurfaceKHR GetSurface() { return m_Surface; }

const QueueSet& GetQueues() { return m_Queues; }

std::mutex& GetQueueSubmitMutex() { return m_QueueSubmitMutex; }

VkResult QueueSubmit2(
    VkQueue p_Queue,
    uint32_t p_SubmitCount,
    const VkSubmitInfo2* p_Submits,
    VkFence p_Fence
)
{
    const std::shared_ptr<std::promise<VkResult>> Promise =
        std::make_shared<std::promise<VkResult>>();
    std::future<VkResult> Future = Promise->get_future();

    PushQueueExecutorJob(
        [Promise, p_Queue, p_SubmitCount, p_Submits, p_Fence] {
            std::lock_guard<std::mutex> Lock(m_QueueSubmitMutex);

            Promise->set_value(m_Disp.queueSubmit2(
                p_Queue,
                p_SubmitCount,
                p_Submits,
                p_Fence
            ));
        }
    );

    return Future.get();
}

VkResult QueuePresent(const VkPresentInfoKHR* p_PresentInfo)
{
    const std::shared_ptr<std::promise<VkResult>> Promise =
        std::make_shared<std::promise<VkResult>>();
    std::future<VkResult> Future = Promise->get_future();

    PushQueueExecutorJob(
        [Promise, p_PresentInfo] {
            std::lock_guard<std::mutex> Lock(m_QueueSubmitMutex);

            Promise->set_value(m_Disp.queuePresentKHR(m_Queues.Present, p_PresentInfo));
        }
    );

    return Future.get();
}

void DrainRenderOperations()
{
    const ::Engine::Base::JobSystem::JobThread& OperationsThread =
        ::Engine::Base::JobSystem::It::GetThread(
            ::Engine::Base::Runtime::EngineThreadID::RenderOpsDedicatedTh
        );

    if (std::this_thread::get_id() == OperationsThread.GetThreadId()) {
        return;
    }

    const std::shared_ptr<std::promise<void>> Promise =
        std::make_shared<std::promise<void>>();
    std::future<void> Future = Promise->get_future();
    const std::string Name = "render_ops_drain_"
        + std::to_string(m_NextQueueOperation.fetch_add(1, std::memory_order_relaxed));

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderOpsDedicatedTh,
        Name,
        [Promise] { Promise->set_value(); },
        -1000
    );

    Future.get();
}

uint64_t QueueSubmitAndPresentAsync(
    const VkSubmitInfo2* p_Submit,
    VkQueue p_Queue,
    VkFence p_Fence,
    const VkPresentInfoKHR* p_PresentInfo
)
{
    struct QueueOperation
    {
        VkSubmitInfo2 Submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        VkCommandBufferSubmitInfo Command = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        VkSemaphoreSubmitInfo Waits[4] = {};
        VkSemaphoreSubmitInfo Signals[2] = {};
        VkPresentInfoKHR Present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        VkSemaphore PresentWait = VK_NULL_HANDLE;
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
        uint32_t ImageIndex = 0;
        VkQueue Queue = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        uint64_t CompletionValue = 0;
    };

    const std::shared_ptr<QueueOperation> Operation = std::make_shared<QueueOperation>();
    Operation->Queue = p_Queue;
    Operation->Fence = p_Fence;
    Operation->CompletionValue = m_NextRenderCompletionValue.fetch_add(
        1,
        std::memory_order_acq_rel
    ) + 1;
    Operation->Submit = *p_Submit;
    Operation->Submit.pCommandBufferInfos = &Operation->Command;
    Operation->Submit.pWaitSemaphoreInfos = Operation->Waits;
    Operation->Submit.pSignalSemaphoreInfos = Operation->Signals;
    Operation->Command = *p_Submit->pCommandBufferInfos;

    for (uint32_t I = 0; I < p_Submit->waitSemaphoreInfoCount; ++I) {
        Operation->Waits[I] = p_Submit->pWaitSemaphoreInfos[I];
    }

    for (uint32_t I = 0; I < p_Submit->signalSemaphoreInfoCount; ++I) {
        Operation->Signals[I] = p_Submit->pSignalSemaphoreInfos[I];
    }

    Operation->Signals[p_Submit->signalSemaphoreInfoCount] = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        nullptr,
        m_RenderCompletionSemaphore,
        Operation->CompletionValue,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        0
    };
    Operation->Submit.signalSemaphoreInfoCount = p_Submit->signalSemaphoreInfoCount + 1;

    Operation->Present = *p_PresentInfo;
    Operation->Present.pWaitSemaphores = &Operation->PresentWait;
    Operation->Present.pSwapchains = &Operation->Swapchain;
    Operation->Present.pImageIndices = &Operation->ImageIndex;
    Operation->PresentWait = p_PresentInfo->pWaitSemaphores[0];

    Operation->Swapchain = p_PresentInfo->pSwapchains[0];
    Operation->ImageIndex = p_PresentInfo->pImageIndices[0];

    const std::string Name = "render_ops_submit_present_"
        + std::to_string(m_NextQueueOperation.fetch_add(1, std::memory_order_relaxed));

    const std::shared_ptr<std::promise<void>> Promise =
        std::make_shared<std::promise<void>>();

    std::future<void> Future = Promise->get_future();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderOpsDedicatedTh,
        Name,
        [Operation, Promise] {
            const VkResult SubmitResult = QueueSubmit2(
                Operation->Queue,
                1,
                &Operation->Submit,
                Operation->Fence
            );

            if (SubmitResult == VK_SUCCESS) {
                QueuePresent(&Operation->Present);
            }

            Promise->set_value();
        }
    );

    Future.get();

    return Operation->CompletionValue;
}

VkSemaphore GetRenderCompletionSemaphore()
{
    return m_RenderCompletionSemaphore;
}

uint64_t GetRenderCompletionValue()
{
    return m_NextRenderCompletionValue.load(std::memory_order_acquire);
}

void DestroyRenderCompletionSemaphore()
{
    if (m_RenderCompletionSemaphore == VK_NULL_HANDLE) {
        return;
    }

    m_Disp.destroySemaphore(m_RenderCompletionSemaphore, nullptr);
    m_RenderCompletionSemaphore = VK_NULL_HANDLE;
}

bool HasMemoryBudget() { return m_HasMemoryBudget; }

const VkPhysicalDeviceIDProperties& GetIdProperties() { return m_IdProperties; }

}
