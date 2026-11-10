#pragma once

#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"

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

    explicit Texture(std::string p_Path);
    ~Texture() override;

    std::string GetTypeName() const override;
    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    const Engine::Base::Render::Image& GetImage() const;
    uint32_t GetIndex() const;

private:
    Engine::Base::Render::Image m_Image;
    uint32_t m_Index = UINT32_MAX;
    std::shared_ptr<LoadData> m_LoadData;
};

void Init();
void Cleanup();

VkDescriptorSetLayout GetBindlessSetLayout();
VkDescriptorSet GetBindlessSet();
void SyncSamplers();

}
