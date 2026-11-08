#include "X/Engine/Base/Render/Vulkan/Descriptors.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

namespace Engine::Base::Render::Vk::Descriptors
{

VkDescriptorPool CreatePool(
    uint32_t p_MaxSets,
    const std::vector<VkDescriptorPoolSize>& p_Sizes,
    VkDescriptorPoolCreateFlags p_Flags
)
{
    VkDescriptorPoolCreateInfo PoolCreateInfo = {};

    PoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolCreateInfo.flags = p_Flags;
    PoolCreateInfo.maxSets = p_MaxSets;
    PoolCreateInfo.poolSizeCount = static_cast<uint32_t>(p_Sizes.size());
    PoolCreateInfo.pPoolSizes = p_Sizes.data();

    VkDescriptorPool Pool = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createDescriptorPool(&PoolCreateInfo, nullptr, &Pool);

    X_ASSERT(Result != VK_SUCCESS, "failed to create descriptor pool");

    return Pool;
}

void DestroyPool(VkDescriptorPool p_Pool)
{
    Device::GetDisp().destroyDescriptorPool(p_Pool, nullptr);
}

VkDescriptorSetLayout CreateSetLayout(
    const std::vector<VkDescriptorSetLayoutBinding>& p_Bindings,
    const std::vector<VkDescriptorBindingFlags>& p_BindingFlags
)
{
    VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsCreateInfo = {};

    FlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    FlagsCreateInfo.bindingCount = static_cast<uint32_t>(p_BindingFlags.size());
    FlagsCreateInfo.pBindingFlags = p_BindingFlags.data();

    VkDescriptorSetLayoutCreateInfo LayoutCreateInfo = {};

    LayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutCreateInfo.bindingCount = static_cast<uint32_t>(p_Bindings.size());
    LayoutCreateInfo.pBindings = p_Bindings.data();

    if (!p_BindingFlags.empty()) {
        LayoutCreateInfo.pNext = &FlagsCreateInfo;
        LayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createDescriptorSetLayout(&LayoutCreateInfo, nullptr, &Layout);

    X_ASSERT(Result != VK_SUCCESS, "failed to create descriptor set layout");

    return Layout;
}

void DestroySetLayout(VkDescriptorSetLayout p_Layout)
{
    Device::GetDisp().destroyDescriptorSetLayout(p_Layout, nullptr);
}

VkDescriptorSet AllocateSet(VkDescriptorPool p_Pool, VkDescriptorSetLayout p_Layout)
{
    VkDescriptorSetAllocateInfo AllocInfo = {};

    AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocInfo.descriptorPool = p_Pool;
    AllocInfo.descriptorSetCount = 1;
    AllocInfo.pSetLayouts = &p_Layout;

    VkDescriptorSet Set = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().allocateDescriptorSets(&AllocInfo, &Set);

    X_ASSERT(Result != VK_SUCCESS, "failed to allocate descriptor set");

    return Set;
}

void WriteImage(
    VkDescriptorSet p_Set,
    uint32_t p_Binding,
    VkDescriptorType p_Type,
    VkImageView p_View,
    VkImageLayout p_Layout,
    VkSampler p_Sampler,
    uint32_t p_Element
)
{
    VkDescriptorImageInfo ImageInfo = {};

    ImageInfo.sampler = p_Sampler;
    ImageInfo.imageView = p_View;
    ImageInfo.imageLayout = p_Layout;

    VkWriteDescriptorSet Write = {};

    Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet = p_Set;
    Write.dstBinding = p_Binding;
    Write.dstArrayElement = p_Element;
    Write.descriptorCount = 1;
    Write.descriptorType = p_Type;
    Write.pImageInfo = &ImageInfo;

    Device::GetDisp().updateDescriptorSets(1, &Write, 0, nullptr);
}

void WriteBuffer(
    VkDescriptorSet p_Set,
    uint32_t p_Binding,
    VkDescriptorType p_Type,
    const Buffer& p_Target,
    VkDeviceSize p_Offset,
    VkDeviceSize p_Range
)
{
    VkDescriptorBufferInfo BufferInfo = {};

    BufferInfo.buffer = p_Target.Handle;
    BufferInfo.offset = p_Offset;
    BufferInfo.range = p_Range;

    VkWriteDescriptorSet Write = {};

    Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet = p_Set;
    Write.dstBinding = p_Binding;
    Write.descriptorCount = 1;
    Write.descriptorType = p_Type;
    Write.pBufferInfo = &BufferInfo;

    Device::GetDisp().updateDescriptorSets(1, &Write, 0, nullptr);
}

}
