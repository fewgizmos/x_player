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

struct Texture::LoadData
{
    std::mutex Mutex;
    std::vector<std::byte> Pixels;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Finished = false;
};

namespace
{

VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
VkSampler m_Sampler = VK_NULL_HANDLE;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("textures_init", Init, 35);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("textures_cleanup", Cleanup, 65);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterUpdate(::Engine::Base::Runtime::EngineCore::RenderTh, "textures_update", Update, 60);

void LoadTexture(const std::shared_ptr<Texture::LoadData>& p_Data, const std::string& p_Path)
{
    int Width = 0;
    int Height = 0;
    int Channels = 0;

    stbi_uc* Pixels = stbi_load(p_Path.c_str(), &Width, &Height, &Channels, 4);

    if (Pixels == nullptr) {
        std::lock_guard<std::mutex> Lock(p_Data->Mutex);
        p_Data->Finished = true;
        return;
    }

    std::vector<std::byte> Data(
        reinterpret_cast<std::byte*>(Pixels),
        reinterpret_cast<std::byte*>(Pixels)
            + static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4
    );
    stbi_image_free(Pixels);

    std::lock_guard<std::mutex> Lock(p_Data->Mutex);
    p_Data->Pixels = std::move(Data);
    p_Data->Width = static_cast<uint32_t>(Width);
    p_Data->Height = static_cast<uint32_t>(Height);
    p_Data->Finished = true;
}

}

Texture::Texture(std::string p_Path)
    : Resource(std::move(p_Path))
    , m_LoadData(std::make_shared<LoadData>())
{
}

Texture::~Texture() = default;

std::string Texture::GetTypeName() const
{
    return "Texture";
}

void Texture::BeginLoad()
{
    const std::shared_ptr<LoadData> Data = m_LoadData;
    const std::string Path = GetKey();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineCore::ResourceLoaderTh,
        "texture_load_" + Path,
        [Data, Path] { LoadTexture(Data, Path); }
    );
}

void Texture::Update()
{
    if (GetState() != ResourceState::Loading) {
        return;
    }

    std::vector<std::byte> Pixels;
    uint32_t Width = 0;
    uint32_t Height = 0;

    {
        std::lock_guard<std::mutex> Lock(m_LoadData->Mutex);

        if (!m_LoadData->Finished) {
            return;
        }

        if (m_LoadData->Pixels.empty()) {
            SetState(ResourceState::Failed);
            return;
        }

        Pixels = std::move(m_LoadData->Pixels);
        Width = m_LoadData->Width;
        Height = m_LoadData->Height;
    }

    m_Image = ::Engine::Base::Render::Vk::GpuResources::CreateImage(
        { Width, Height, 1 },
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    ::Engine::Base::Render::Vk::GpuResources::QueueImageUpload(
        m_Image,
        Pixels.data(),
        Pixels.size()
    );

    m_DescriptorSet = ::Engine::Base::Render::Vk::Descriptors::AllocateSet(
        m_DescriptorPool,
        m_SetLayout
    );

    ::Engine::Base::Render::Vk::Descriptors::WriteImage(
        m_DescriptorSet,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        m_Image.View,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_Sampler
    );

    SetState(ResourceState::Ready);
}

void Texture::Unload()
{
    if (m_Image.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyImage(m_Image);
    }

    m_DescriptorSet = VK_NULL_HANDLE;
}

const Engine::Base::Render::Image& Texture::GetImage() const
{
    return m_Image;
}

VkDescriptorSet Texture::GetDescriptorSet() const
{
    return m_DescriptorSet;
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
}

void Cleanup()
{
    if (m_SetLayout != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Descriptors::DestroySetLayout(m_SetLayout);
        m_SetLayout = VK_NULL_HANDLE;
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Descriptors::DestroyPool(m_DescriptorPool);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    m_Sampler = VK_NULL_HANDLE;
}

void Update()
{
    if constexpr (::Engine::Configs::Static::Tests::TEXTURE_RENDERER_ENABLED) {
        ::Engine::Applied::Render::Resources::UpdateAll();
    }
}

VkDescriptorSetLayout GetSetLayout()
{
    return m_SetLayout;
}

}
