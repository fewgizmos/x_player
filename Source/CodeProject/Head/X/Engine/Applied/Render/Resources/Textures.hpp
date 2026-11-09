#pragma once

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <vulkan/vulkan_core.h>

namespace Engine::Applied::Render::Resources::Textures
{

struct Texture
{
    Engine::Base::Render::Image Image;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    bool Ready = false;
};

void Init();
void Cleanup();
void Update();

const Texture& GetDefaultEngineLogo();
VkDescriptorSetLayout GetSetLayout();

}
