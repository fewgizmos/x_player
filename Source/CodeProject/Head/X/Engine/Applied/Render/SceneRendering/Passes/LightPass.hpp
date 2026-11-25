#pragma once

#include <vulkan/vulkan_core.h>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

struct Frame;

}

namespace Engine::Applied::Render::SceneRendering::Passes::LightPass
{

void Init();
void Cleanup();

void Prepare(const SceneList::Frame& p_Frame, VkImageView p_Albedo, VkImageView p_Normal, VkImageView p_Depth);

VkDescriptorSet GetSet();

}
