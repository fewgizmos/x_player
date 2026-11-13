#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/Descriptors.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"

#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include <stb_image.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
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

    Engine::Base::Render::Image Image;
    bool UploadRejected = false;

    std::string FailureReason;
    bool Finished = false;
    uint64_t VersionChecksum = 0;
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
        ::Engine::Base::Runtime::EngineThreadID::RenderTh,
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
        p_Data->FailureReason = stbi_failure_reason() ? stbi_failure_reason() : "Image decoding failed";
        p_Data->Finished = true;
        return;
    }

    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> Owner(Pixels, stbi_image_free);
    std::vector<std::byte> Data(
        reinterpret_cast<std::byte*>(Pixels),
        reinterpret_cast<std::byte*>(Pixels)
            + static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4
    );

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

Texture::Texture(std::string p_Path, VkFormat p_Format)
    : Resource(std::to_string(static_cast<int>(p_Format)) + ":" + p_Path)
    , Path(std::move(p_Path))
    , Format(p_Format)
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
    AdvanceVersionChecksum();

    m_LoadData = std::make_shared<LoadData>();
    m_GpuTicket.reset();
    m_DescriptorTicket.reset();
    m_GpuVersionChecksum = 0;
    m_Index = UINT32_MAX;

    if (Format != VK_FORMAT_R8G8B8A8_SRGB && Format != VK_FORMAT_R8G8B8A8_UNORM) {
        SetFailed("unsupported decoded texture format; expected RGBA8 SRGB or UNORM");
        return;
    }

    const std::shared_ptr<LoadData> Data = m_LoadData;
    const std::string TexturePath = Path;
    Data->VersionChecksum = GetVersionChecksum();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "texture_load_" + TexturePath,
        [Data, TexturePath] {
            try {
                LoadTexture(Data, TexturePath);
            }
            catch (const std::exception& Error) {
                std::lock_guard<std::mutex> Lock(Data->Mutex);
                Data->FailureReason = Error.what();
                Data->Finished = true;
            }
            catch (...) {
                std::lock_guard<std::mutex> Lock(Data->Mutex);
                Data->FailureReason = "Unknown texture load failure";
                Data->Finished = true;
            }
        }
    );
}

void Texture::Update()
{
    if (GetState() != ::Engine::Util::Applied::ResourceState::LoadingCpu
        && GetState() != ::Engine::Util::Applied::ResourceState::QueuedGpu
        && GetState() != ::Engine::Util::Applied::ResourceState::Uploading) {

        return;
    }

    if (m_GpuTicket) {
        if (GetVersionChecksum() != m_GpuVersionChecksum) {
            SetRejected("texture GPU job version is stale");
            return;
        }

        if (!m_GpuTicket->Done.load(std::memory_order_acquire)) {
            SetState(::Engine::Util::Applied::ResourceState::Uploading);

            return;
        }
        if (m_GpuTicket->Rejected.load(std::memory_order_acquire)) {
            SetRejected(m_GpuTicket->FailureReason);

            return;
        }

        bool UploadRejected = false;
        {
            std::lock_guard<std::mutex> Lock(m_LoadData->Mutex);
            UploadRejected = m_LoadData->UploadRejected;
        }

        if (UploadRejected) {
            SetRejected("texture upload version is stale");
            return;
        }
        if (m_GpuTicket->Failed.load(std::memory_order_acquire)) {
            SetFailed(m_GpuTicket->FailureReason);

            return;
        }

        if (m_DescriptorTicket) {
            if (!m_DescriptorTicket->Done.load(std::memory_order_acquire)) {
                SetState(::Engine::Util::Applied::ResourceState::Uploading);

                return;
            }

            if (m_DescriptorTicket->Failed.load(std::memory_order_acquire)
                || m_DescriptorTicket->Rejected.load(std::memory_order_acquire)) {
                SetFailed(m_DescriptorTicket->FailureReason);

                return;
            }
        } else {
            const Engine::Base::Render::Image PreviousImage = m_Image;
            const uint32_t PreviousIndex = m_Index;

            m_Image = m_LoadData->Image;
            m_LoadData->Pixels.clear();
            m_Index = TakeIndex();

            const VkImageView ImageView = m_Image.View;
            const uint32_t TextureIndex = m_Index;
            m_DescriptorTicket =
                ::Engine::Base::Render::ResourceLoader::PushFrameJobCallback(
                    [Set = m_Set, ImageView, TextureIndex] {
                        ::Engine::Base::Render::Vk::Descriptors::WriteImage(
                            Set,
                            1,
                            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            ImageView,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_NULL_HANDLE,
                            TextureIndex
                        );
                    },
                    ::Engine::Configs::Static::ResourceLoader::GPU_OPS_HIGH_ORDER,
                    { 0, 0, 0, 0, 1 }
                );

            if (PreviousImage.Handle != VK_NULL_HANDLE) {
                Engine::Base::Render::Image OldImage = PreviousImage;
                ::Engine::Base::Render::ResourceLoader::DestroyImage(OldImage);
            }

            ReleaseIndex(PreviousIndex);
            SetState(::Engine::Util::Applied::ResourceState::Uploading);

            return;
        }

        const std::shared_ptr<TextureSnapshot> Snapshot = std::make_shared<TextureSnapshot>();
        Snapshot->Image = m_Image;
        Snapshot->Index = m_Index;
        Snapshot->Format = Format;
        Snapshot->VersionChecksum = m_GpuVersionChecksum;
        m_Snapshot.store(
            std::shared_ptr<const TextureSnapshot>(Snapshot),
            std::memory_order_release
        );

        SetState(::Engine::Util::Applied::ResourceState::Ready);
        return;
    }

    uint32_t Width = 0;
    uint32_t Height = 0;

    {
        std::lock_guard<std::mutex> Lock(m_LoadData->Mutex);

        if (!m_LoadData->Finished) {

            return;
        }

        if (m_LoadData->Pixels.empty()) {
            SetFailed("Failed to load image " + Path + ": " + m_LoadData->FailureReason);

            return;
        }

        Width = m_LoadData->Width;
        Height = m_LoadData->Height;
    }

    uint32_t MipLevels = 1;
    for (uint32_t Size = std::max(Width, Height); Size > 1; Size /= 2) {
        ++MipLevels;
    }

    VkFormatProperties Properties = {};

    vkGetPhysicalDeviceFormatProperties(::Engine::Base::Render::Vk::Device::GetPhysical().physical_device,
        Format, &Properties);
    const VkFormatFeatureFlags Required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
        | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    X_ASSERT((Properties.optimalTilingFeatures & Required) != Required, "Texture format does not support filtered mipmaps");

    const std::shared_ptr<LoadData> Data = m_LoadData;
    const VkFormat TextureFormat = Format;
    m_GpuVersionChecksum = GetVersionChecksum();
    const uint64_t VersionChecksum = m_GpuVersionChecksum;

    SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);
    m_GpuTicket = ::Engine::Base::Render::ResourceLoader::PushFrameJobVk(
        [Data, TextureFormat, Width, Height, MipLevels, VersionChecksum](VkCommandBuffer) {
            std::lock_guard<std::mutex> Lock(Data->Mutex);
            if (Data->VersionChecksum != VersionChecksum) {
                Data->UploadRejected = true;
                return;
            }

            try {
                Data->Image = ::Engine::Base::Render::ResourceLoader::CreateImage(
                    { Width, Height, 1 },
                    TextureFormat,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                        | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    MipLevels
                );
                ::Engine::Base::Render::ResourceLoader::QueueImageUpload(
                    Data->Image,
                    Data->Pixels.data(),
                    Data->Pixels.size()
                );
            }
            catch (...) {
                ::Engine::Base::Render::ResourceLoader::DestroyImage(Data->Image);
                throw;
            }
        },
        ::Engine::Configs::Static::ResourceLoader::GPU_OPS_NORMAL_ORDER,
        {
            Data->Pixels.size(),
            1,
            1,
            0,
            0
        }
    );
    m_GpuTicket->VersionChecksum = VersionChecksum;
}

void Texture::Unload()
{
    AdvanceVersionChecksum();

    {
        std::lock_guard<std::mutex> Lock(m_LoadData->Mutex);
        m_LoadData->VersionChecksum = GetVersionChecksum();
        m_LoadData->UploadRejected = true;
    }

    if (GetState() == ::Engine::Util::Applied::ResourceState::Unloading) {
        return;
    }

    SetState(::Engine::Util::Applied::ResourceState::Unloading);

    if (m_Image.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::ResourceLoader::DestroyImage(m_Image);
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

VkFormat Texture::GetFormat() const { return Format; }

std::shared_ptr<const TextureSnapshot> Texture::GetSnapshot() const
{
    return m_Snapshot.load(std::memory_order_acquire);
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
