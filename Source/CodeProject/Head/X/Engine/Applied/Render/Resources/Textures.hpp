#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine::Applied::Render::Resources::Textures
{

struct TextureCreateInfo
{
    std::string Path;

    VkFormat Format = VK_FORMAT_R8G8B8A8_SRGB;
    std::shared_ptr<const std::vector<std::byte>> EncodedData{};
};

struct TextureSnapshot
{
    Engine::Base::Render::Image Image;

    uint32_t Index = UINT32_MAX;
    VkFormat Format = VK_FORMAT_UNDEFINED;

    uint64_t VersionChecksum = 0;
};

class Texture final : public ::Engine::Util::Applied::Resource
{
public:
    struct LoadData;

    using CreateInfo = TextureCreateInfo;

    explicit Texture(CreateInfo p_CreateInfo);
    ~Texture() override;

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const Engine::Base::Render::Image& GetImage() const;

    uint32_t GetIndex() const;
    VkFormat GetFormat() const;

    std::shared_ptr<const TextureSnapshot> GetSnapshot() const;

private:
    std::string Path;

    VkFormat Format;
    std::shared_ptr<const std::vector<std::byte>> m_EncodedData;

    Engine::Base::Render::Image m_Image;
    uint32_t m_Index = UINT32_MAX;

    std::shared_ptr<LoadData> m_LoadData;

    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_GpuTicket;
    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_DescriptorTicket;

    uint64_t m_GpuVersionChecksum = 0;
    uint32_t m_UploadRow = 0;
    uint32_t m_UploadChunkRows = 1;
    uint32_t m_UploadHeight = 0;

    std::atomic<std::shared_ptr<const TextureSnapshot>> m_Snapshot;
};

void Init();
void Cleanup();

VkDescriptorSetLayout GetBindlessSetLayout();
VkDescriptorSet GetBindlessSet();
void SyncSamplers();

}
