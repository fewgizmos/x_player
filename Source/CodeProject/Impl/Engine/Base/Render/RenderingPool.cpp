#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace Engine::Base::Render::RenderingPool
{

namespace
{

struct Entry
{
    std::string Name;
    RenderCallback Callback;
    VkFormat Format;
    VkExtent2D Extent;
    Image Storage;
    int Order;
};

struct SnapshotEntry
{
    std::string Name;
    SnapshotCallback Callback;
    int Order;
};

std::vector<Entry> m_Entries;
std::vector<RenderTarget> m_Targets;
std::vector<SnapshotEntry> m_Snapshots;

bool m_Sealed = false;

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("rendering_pool_seal", Seal, -100);
const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("rendering_pool_clear", Clear, 79);

}

void Register(std::string p_Name, RenderCallback p_Callback, VkFormat p_Format,
    VkExtent2D p_Extent, int p_Order)
{
    X_ASSERT(m_Sealed, "Rendering pool is already sealed");

    X_ASSERT(!p_Callback, "Empty renderer callback: " + p_Name);

    X_ASSERT(std::any_of(m_Entries.begin(), m_Entries.end(),
        [&](const Entry& Item) {
            return Item.Name == p_Name;
        }), "Duplicate renderer: " + p_Name);

    X_ASSERT(p_Format == VK_FORMAT_UNDEFINED || ((p_Extent.width == 0) != (p_Extent.height == 0)),
        "Invalid render target: " + p_Name);
    VkFormatProperties Properties{};
    vkGetPhysicalDeviceFormatProperties(Vk::Device::GetPhysical().physical_device, p_Format, &Properties);
    constexpr VkFormatFeatureFlags Required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
        | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    X_ASSERT((Properties.optimalTilingFeatures & Required) != Required,
        "Unsupported format for composited render target: " + p_Name);
    m_Entries.push_back({std::move(p_Name), std::move(p_Callback), p_Format, p_Extent, {}, p_Order});
}

void RegisterSnapshot(std::string p_Name, SnapshotCallback p_Callback, int p_Order)
{
    X_ASSERT(m_Sealed, "Rendering pool is already sealed");
    X_ASSERT(!p_Callback, "Empty renderer snapshot callback: " + p_Name);

    X_ASSERT(std::any_of(m_Snapshots.begin(), m_Snapshots.end(),
        [&](const SnapshotEntry& Item) {
            return Item.Name == p_Name;
        }), "Duplicate renderer snapshot: " + p_Name);

    m_Snapshots.push_back({std::move(p_Name), std::move(p_Callback), p_Order});
}

void Seal()
{
    std::stable_sort(m_Entries.begin(), m_Entries.end(),
        [](const Entry& p_A, const Entry& B) {
            return p_A.Order > B.Order;
        });

    std::stable_sort(m_Snapshots.begin(), m_Snapshots.end(),
        [](const SnapshotEntry& p_A, const SnapshotEntry& p_B) {
            return p_A.Order > p_B.Order;
        });

    ResizeTargets(Vk::Frame::GetSwapchain().extent);
    m_Sealed = true;
}

void CaptureFrameSnapshot()
{
    for (const SnapshotEntry& Item : m_Snapshots) {
        Item.Callback();
    }
}

void ResizeTargets(VkExtent2D p_WindowExtent)
{
    m_Targets.clear();
    for (Entry& Item : m_Entries) {
        const VkExtent2D Extent = Item.Extent.width ? Item.Extent : p_WindowExtent;
        Image& Storage = Item.Storage;
        if (Storage.Extent.width != Extent.width || Storage.Extent.height != Extent.height) {
            ResourceLoader::DestroyImage(Storage);
            Storage = ResourceLoader::CreateImage({Extent.width, Extent.height, 1}, Item.Format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }
        m_Targets.push_back({Storage.Handle, Storage.View, Storage.Format, Extent});
    }
}

std::span<const RenderTarget> RenderAll(VkCommandBuffer p_Cmd)
{
    X_ASSERT(!m_Sealed, "Rendering pool must be sealed before rendering");
    const VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkClearColorValue Transparent{};
    for (std::size_t I = 0; I < m_Entries.size(); ++I) {
        const RenderTarget& Target = m_Targets[I];
        // The previous frame may still be sampling this layer on the graphics queue.
        Vk::Commands::TransitionImage(p_Cmd, Target.Image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        Vk::Device::GetDisp().cmdClearColorImage(p_Cmd, Target.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Transparent, 1, &Range);
        Vk::Commands::TransitionImage(p_Cmd, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        m_Entries[I].Callback(Target);

        Vk::Commands::TransitionImage(p_Cmd, Target.Image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    return m_Targets;
}

void Clear()
{
    for (Entry& Item : m_Entries) {
        ResourceLoader::DestroyImage(Item.Storage);
    }
    m_Targets.clear();
    m_Entries.clear();
    m_Snapshots.clear();
    m_Sealed = false;
}

}
