#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Configs/Static.hpp"
#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Render/Vulkan/Descriptors.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"
#include "X/Engine/Util/Base/Assert.hpp"

#include <stb_image.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Resources::Textures
{

namespace
{

Texture m_DefaultEngineLogo;
VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
VkSampler m_Sampler = VK_NULL_HANDLE;

std::mutex m_LoadMutex;
std::vector<std::byte> m_Pixels;
uint32_t m_Width = 0;
uint32_t m_Height = 0;
bool m_LoadFinished = false;
bool m_LoadStarted = false;
bool m_ImageCreated = false;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("textures_init", Init, 35);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("textures_cleanup", Cleanup, 65);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterUpdate(::Engine::Base::Runtime::EngineCore::RenderTh, "textures_update", Update, 60);

void LoadDefaultEngineLogo()
{
    int Width = 0;
    int Height = 0;
    int Channels = 0;

    stbi_uc* Pixels = stbi_load("Resources/Images/DefaultEngineLogo.png", &Width, &Height, &Channels, 4);
    X_ASSERT(Pixels == nullptr, "failed to load default engine logo");

    std::vector<std::byte> Data(
        reinterpret_cast<std::byte*>(Pixels),
        reinterpret_cast<std::byte*>(Pixels) + static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4
    );
    stbi_image_free(Pixels);

    std::lock_guard<std::mutex> Lock(m_LoadMutex);
    m_Pixels = std::move(Data);
    m_Width = static_cast<uint32_t>(Width);
    m_Height = static_cast<uint32_t>(Height);
    m_LoadFinished = true;
}

}

void Init()
{
    const std::vector<VkDescriptorPoolSize> PoolSizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ::Engine::Configs::Static::Graphics::MAX_TEXTURES }
    };

    m_DescriptorPool = ::Engine::Base::Render::Vk::Descriptors::CreatePool(
        ::Engine::Configs::Static::Graphics::MAX_TEXTURES,
        PoolSizes
    );

    const VkDescriptorSetLayoutBinding Binding = {
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    m_SetLayout = ::Engine::Base::Render::Vk::Descriptors::CreateSetLayout({ Binding });

    ::Engine::Base::Render::SamplerCreateInfo SamplerInfo;
    SamplerInfo.WrapU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.WrapV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.WrapW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const uint32_t SamplerIndex = ::Engine::Base::Render::Vk::Samplers::Make(SamplerInfo);
    m_Sampler = ::Engine::Base::Render::Vk::Samplers::Get(SamplerIndex);

    if constexpr (::Engine::Configs::Static::Tests::TEXTURE_RENDERER_ENABLED) {
        m_LoadStarted = true;
        ::Engine::Base::JobSystem::It::PushBackground(
            ::Engine::Base::Runtime::EngineCore::ResourceLoaderTh,
            "textures_default_engine_logo_load",
            LoadDefaultEngineLogo
        );
    }
}

void Update()
{
    if constexpr (!::Engine::Configs::Static::Tests::TEXTURE_RENDERER_ENABLED) {
        return;
    }

    if (m_ImageCreated || !m_LoadStarted) {
        return;
    }

    std::vector<std::byte> Pixels;
    uint32_t Width = 0;
    uint32_t Height = 0;

    {
        std::lock_guard<std::mutex> Lock(m_LoadMutex);

        if (!m_LoadFinished) {
            return;
        }

        Pixels = std::move(m_Pixels);
        Width = m_Width;
        Height = m_Height;
        m_LoadFinished = false;
    }

    m_DefaultEngineLogo.Image = ::Engine::Base::Render::Vk::GpuResources::CreateImage(
        { Width, Height, 1 },
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    ::Engine::Base::Render::Vk::GpuResources::QueueImageUpload(
        m_DefaultEngineLogo.Image,
        Pixels.data(),
        Pixels.size()
    );

    m_DefaultEngineLogo.DescriptorSet = ::Engine::Base::Render::Vk::Descriptors::AllocateSet(
        m_DescriptorPool,
        m_SetLayout
    );

    ::Engine::Base::Render::Vk::Descriptors::WriteImage(
        m_DefaultEngineLogo.DescriptorSet,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        m_DefaultEngineLogo.Image.View,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_Sampler
    );

    m_DefaultEngineLogo.Ready = true;
    m_ImageCreated = true;
}

void Cleanup()
{
    if (m_DefaultEngineLogo.Image.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyImage(m_DefaultEngineLogo.Image);
    }

    if (m_SetLayout != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Descriptors::DestroySetLayout(m_SetLayout);
        m_SetLayout = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Descriptors::DestroyPool(m_DescriptorPool);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    m_DefaultEngineLogo = {};
    m_Sampler = VK_NULL_HANDLE;
    m_ImageCreated = false;
}

const Texture& GetDefaultEngineLogo()
{
    return m_DefaultEngineLogo;
}

VkDescriptorSetLayout GetSetLayout()
{
    return m_SetLayout;
}

}
