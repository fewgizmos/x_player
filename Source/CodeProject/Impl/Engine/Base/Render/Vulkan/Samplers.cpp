#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <vector>

namespace Engine::Base::Render::Vk::Samplers
{

namespace
{

struct MadeSampler
{
    SamplerCreateInfo CreateInfo;
    VkSampler Handle = VK_NULL_HANDLE;
};

constexpr float MAX_ANISOTROPY = 16.0f;

std::vector<MadeSampler> m_Samplers;

bool IsSame(const SamplerCreateInfo& p_Left, const SamplerCreateInfo& p_Right)
{
    return p_Left.MagFilter == p_Right.MagFilter
        && p_Left.MinFilter == p_Right.MinFilter
        && p_Left.MipMode   == p_Right.MipMode
        && p_Left.WrapU     == p_Right.WrapU
        && p_Left.WrapV     == p_Right.WrapV
        && p_Left.WrapW     == p_Right.WrapW
        && p_Left.Anisotropy == p_Right.Anisotropy;
}

}

uint32_t Make(const SamplerCreateInfo& p_CreateInfo)
{
    for (uint32_t I = 0; I < m_Samplers.size(); I++) {
        if (IsSame(m_Samplers[I].CreateInfo, p_CreateInfo)) {
            return I;
        }
    }

    X_ASSERT(m_Samplers.size() >= Configs::Static::Graphics::MAX_SAMPLERS, "samplers limit exceeded");

    VkSamplerCreateInfo VkCreateInfo = {};

    VkCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    VkCreateInfo.magFilter = p_CreateInfo.MagFilter;
    VkCreateInfo.minFilter = p_CreateInfo.MinFilter;
    VkCreateInfo.mipmapMode = p_CreateInfo.MipMode;
    VkCreateInfo.addressModeU = p_CreateInfo.WrapU;
    VkCreateInfo.addressModeV = p_CreateInfo.WrapV;
    VkCreateInfo.addressModeW = p_CreateInfo.WrapW;
    VkCreateInfo.anisotropyEnable = p_CreateInfo.Anisotropy ? VK_TRUE : VK_FALSE;
    VkCreateInfo.maxAnisotropy = std::min(
        MAX_ANISOTROPY,
        Device::GetPhysical().properties.limits.maxSamplerAnisotropy
    );

    VkCreateInfo.minLod = 0.0f;
    VkCreateInfo.maxLod = VK_LOD_CLAMP_NONE;

    MadeSampler Made;

    Made.CreateInfo = p_CreateInfo;

    const VkResult Result = Device::GetDisp().createSampler(&VkCreateInfo, nullptr, &Made.Handle);

    X_ASSERT(Result != VK_SUCCESS, "failed to create sampler");

    m_Samplers.push_back(Made);

    return static_cast<uint32_t>(m_Samplers.size() - 1);
}

VkSampler Get(uint32_t p_Index) { return m_Samplers.at(p_Index).Handle; }

uint32_t GetCount() { return static_cast<uint32_t>(m_Samplers.size()); }

void DestroyAll()
{
    for (const MadeSampler& Made : m_Samplers) {
        Device::GetDisp().destroySampler(Made.Handle, nullptr);
    }

    m_Samplers.clear();
}

}
