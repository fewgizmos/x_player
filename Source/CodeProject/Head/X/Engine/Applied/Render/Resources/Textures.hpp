#pragma once

#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <vulkan/vulkan_core.h>

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
    VkDescriptorSet GetDescriptorSet() const;

private:
    Engine::Base::Render::Image m_Image;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    std::shared_ptr<LoadData> m_LoadData;
};

void Init();
void Cleanup();
void Update();

VkDescriptorSetLayout GetSetLayout();

}
