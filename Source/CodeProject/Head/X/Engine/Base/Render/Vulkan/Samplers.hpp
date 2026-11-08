#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace Engine::Base::Render
{

struct SamplerCreateInfo
{
    VkFilter MagFilter = VK_FILTER_LINEAR;
    VkFilter MinFilter = VK_FILTER_LINEAR;

    VkSamplerMipmapMode MipMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSamplerAddressMode WrapU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode WrapV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode WrapW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    bool Anisotropy = true;
};

}

namespace Engine::Base::Render::Vk::Samplers
{

uint32_t Make(const SamplerCreateInfo& p_CreateInfo);

VkSampler Get(uint32_t p_Index);

uint32_t GetCount();

void DestroyAll();

}
