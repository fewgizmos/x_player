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
VkDescriptorSet m_Set = VK_NULL_HANDLE;
std::mutex m_IndexMutex;
uint32_t m_BoundSamplers = 0;
uint32_t m_NextIndex = 0;
std::vector<uint32_t> m_FreeIndices;

struct ErasedIndex
{
    uint32_t Index = UINT32_MAX;
    int FramesLeft = 0;
};

std::vector<ErasedIndex> m_ErasedIndices;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("textures_init", Init, 35);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("textures_cleanup", Cleanup, 65);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterSync(
        ::Engine::Base::Runtime::EngineCore::RenderTh,
        "textures_sync_samplers",
        SyncSamplers,
        65
    );

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

uint32_t TakeIndex()
{
    std::lock_guard<std::mutex> Lock(m_IndexMutex);

    if (!m_FreeIndices.empty()) {
        const uint32_t Index = m_FreeIndices.back();
        m_FreeIndices.pop_back();
        return Index;
    }

    X_ASSERT(m_NextIndex >= ::Engine::Configs::Static::Graphics::MAX_TEXTURES, "textures limit exceeded");
    return m_NextIndex++;
}

void ReleaseIndex(uint32_t p_Index)
{
    if (p_Index != UINT32_MAX) {
        std::lock_guard<std::mutex> Lock(m_IndexMutex);
        m_ErasedIndices.push_back({ p_Index, ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT });
    }
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
            SetFailed("failed to load image: " + GetKey());
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

    m_Index = TakeIndex();

    ::Engine::Base::Render::Vk::Descriptors::WriteImage(
        m_Set,
        1,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        m_Image.View,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_NULL_HANDLE,
        m_Index
    );

    SetState(ResourceState::Ready);
}

void Texture::Unload()
{
    if (m_Image.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyImage(m_Image);
    }

    ReleaseIndex(m_Index);
    m_Index = UINT32_MAX;
}

const Engine::Base::Render::Image& Texture::GetImage() const
{
    return m_Image;
}

uint32_t Texture::GetIndex() const
{
    return m_Index;
}

void Init()
{
    const std::vector<VkDescriptorPoolSize> PoolSizes = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, ::Engine::Configs::Static::Graphics::MAX_SAMPLERS },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ::Engine::Configs::Static::Graphics::MAX_TEXTURES }
    };

    m_DescriptorPool = ::Engine::Base::Render::Vk::Descriptors::CreatePool(
        1,
        PoolSizes,
        VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
    );

    const std::vector<VkDescriptorSetLayoutBinding> Bindings = {
        { 0, VK_DESCRIPTOR_TYPE_SAMPLER, ::Engine::Configs::Static::Graphics::MAX_SAMPLERS, VK_SHADER_STAGE_ALL, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ::Engine::Configs::Static::Graphics::MAX_TEXTURES, VK_SHADER_STAGE_ALL, nullptr }
    };

    const VkDescriptorBindingFlags Flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    m_SetLayout = ::Engine::Base::Render::Vk::Descriptors::CreateSetLayout(Bindings, { Flags, Flags });
    m_Set = ::Engine::Base::Render::Vk::Descriptors::AllocateSet(m_DescriptorPool, m_SetLayout);

    ::Engine::Base::Render::SamplerCreateInfo SamplerInfo;
    SamplerInfo.WrapU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.WrapV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.WrapW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ::Engine::Base::Render::Vk::Samplers::Make(SamplerInfo);
    SyncSamplers();
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

    m_Set = VK_NULL_HANDLE;
    m_BoundSamplers = 0;
    m_NextIndex = 0;
    m_FreeIndices.clear();
    m_ErasedIndices.clear();
}

VkDescriptorSetLayout GetBindlessSetLayout()
{
    return m_SetLayout;
}

VkDescriptorSet GetBindlessSet()
{
    return m_Set;
}

void SyncSamplers()
{
    std::lock_guard<std::mutex> Lock(m_IndexMutex);

    for (; m_BoundSamplers < ::Engine::Base::Render::Vk::Samplers::GetCount(); m_BoundSamplers++) {
        ::Engine::Base::Render::Vk::Descriptors::WriteImage(
            m_Set,
            0,
            VK_DESCRIPTOR_TYPE_SAMPLER,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            ::Engine::Base::Render::Vk::Samplers::Get(m_BoundSamplers),
            m_BoundSamplers
        );
    }

    for (auto It = m_ErasedIndices.begin(); It != m_ErasedIndices.end();) {
        if (It->FramesLeft > 0) {
            It->FramesLeft--;
            ++It;
            continue;
        }

        m_FreeIndices.push_back(It->Index);
        It = m_ErasedIndices.erase(It);
    }
}

}
