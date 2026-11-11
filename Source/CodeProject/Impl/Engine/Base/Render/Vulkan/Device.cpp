#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_vulkan.h>

#include <string>

namespace Engine::Base::Render::Vk::Device
{

namespace
{

VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

vkb::PhysicalDevice m_Physical;
vkb::Device m_Device;
vkb::DispatchTable m_Disp;

QueueSet m_Queues;
std::mutex m_GraphicsQueueMutex;

bool m_HasMemoryBudget = false;

VkPhysicalDeviceIDProperties m_IdProperties = {};

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

std::mutex& GetGraphicsQueueMutex() { return m_GraphicsQueueMutex; }

bool HasMemoryBudget() { return m_HasMemoryBudget; }

const VkPhysicalDeviceIDProperties& GetIdProperties() { return m_IdProperties; }

}
