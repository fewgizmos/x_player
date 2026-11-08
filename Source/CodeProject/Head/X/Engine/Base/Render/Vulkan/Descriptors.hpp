#pragma once

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace Engine::Base::Render::Vk::Descriptors
{

VkDescriptorPool CreatePool(
    uint32_t p_MaxSets,
    const std::vector<VkDescriptorPoolSize>& p_Sizes,
    VkDescriptorPoolCreateFlags p_Flags = 0
);

void DestroyPool(VkDescriptorPool p_Pool);

VkDescriptorSetLayout CreateSetLayout(
    const std::vector<VkDescriptorSetLayoutBinding>& p_Bindings,
    const std::vector<VkDescriptorBindingFlags>& p_BindingFlags = {}
);

void DestroySetLayout(VkDescriptorSetLayout p_Layout);

VkDescriptorSet AllocateSet(VkDescriptorPool p_Pool, VkDescriptorSetLayout p_Layout);

void WriteImage(
    VkDescriptorSet p_Set,
    uint32_t p_Binding,
    VkDescriptorType p_Type,
    VkImageView p_View,
    VkImageLayout p_Layout,
    VkSampler p_Sampler = VK_NULL_HANDLE,
    uint32_t p_Element = 0
);

void WriteBuffer(
    VkDescriptorSet p_Set,
    uint32_t p_Binding,
    VkDescriptorType p_Type,
    const Buffer& p_Target,
    VkDeviceSize p_Offset = 0,
    VkDeviceSize p_Range = VK_WHOLE_SIZE
);

}
