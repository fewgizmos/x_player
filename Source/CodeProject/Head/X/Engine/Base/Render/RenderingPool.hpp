#pragma once

#include <vulkan/vulkan_core.h>

#include <functional>
#include <span>
#include <string>

namespace Engine::Base::Render::RenderingPool
{

struct RenderTarget
{
    VkImage Image;
    VkImageView View;
    VkFormat Format;
    VkExtent2D Extent;
};

// A zero extent follows the swapchain. Nonzero dimensions stay fixed on resize.
inline constexpr VkExtent2D WindowExtent{0, 0};

// Targets start transparent in COLOR_ATTACHMENT_OPTIMAL. Store premultiplied RGBA
// and leave the image in that layout when the callback returns.
using RenderCallback = std::function<void(const RenderTarget&)>;
using SnapshotCallback = std::function<void()>;

void Register(std::string p_Name, RenderCallback p_Callback, VkFormat p_Format,
    VkExtent2D p_Extent, int p_Order = 50);
void RegisterSnapshot(std::string p_Name, SnapshotCallback p_Callback, int p_Order = 50);
void Seal();
void CaptureFrameSnapshot();
void ResizeTargets(VkExtent2D p_WindowExtent);
std::span<const RenderTarget> RenderAll(VkCommandBuffer p_Cmd);
void Clear();

}
