#pragma once

#include <vulkan/vulkan_core.h>

#include <vkb/VkBootstrap.h>

#include <cstdint>

namespace Engine::Base::Render
{

struct QueueSet
{
    VkQueue Graphics = VK_NULL_HANDLE;
    VkQueue Present  = VK_NULL_HANDLE;
    VkQueue Transfer  = VK_NULL_HANDLE;

    uint32_t GraphicsFamily = 0;
    uint32_t PresentFamily  = 0;
    uint32_t TransferFamily = 0;

    bool DedicatedTransfer = false;
};

}

namespace Engine::Base::Render::Vk::Device
{

void CreateSurface(const vkb::Instance& p_Instance);
void CreateDevice(const vkb::Instance& p_Instance);

void MakeQueues();

const vkb::PhysicalDevice& GetPhysical();
const vkb::Device& GetDevice();
const vkb::DispatchTable& GetDisp();

VkSurfaceKHR GetSurface();

const QueueSet& GetQueues();

bool HasMemoryBudget();

const VkPhysicalDeviceIDProperties& GetIdProperties();

}
