#pragma once

#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Engine::Applied::Render::Resources::Textures
{

class Texture final : public Resource
{
public:
    struct LoadData;

    Texture(std::string p_Path, VkFormat p_Format);
    ~Texture() override;

    std::string GetTypeName() const override;
    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    const Engine::Base::Render::Image& GetImage() const;
    uint32_t GetIndex() const;
    VkFormat GetFormat() const;

private:
    std::string Path;
    VkFormat Format;
    Engine::Base::Render::Image m_Image;
    uint32_t m_Index = UINT32_MAX;
    std::shared_ptr<LoadData> m_LoadData;
    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_GpuTicket;
};

void Init();
void Cleanup();

VkDescriptorSetLayout GetBindlessSetLayout();
VkDescriptorSet GetBindlessSet();
void SyncSamplers();

}
