#include "X/Engine/Applied/Render/Resources/LightPoint.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/Vulkan/Descriptors.hpp"

#include <utility>

namespace Engine::Applied::Render::Resources
{

void LightPointCreateInfo::LoadHdri(std::string p_Path)
{
    using TextureInfo = ::Engine::Applied::Render::Resources::Textures::TextureCreateInfo;

    Hdri = ::Engine::Util::Applied::ResourceHandle<Texture>(
        TextureInfo{std::move(p_Path), VK_FORMAT_R16G16B16A16_SFLOAT}
    );
}

namespace
{

namespace Vk = ::Engine::Base::Render::Vk;

using namespace ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration;

VkDescriptorSetLayout m_LightLayout{};
VkDescriptorSetLayout m_GeometryLayout{};

void InitLayouts()
{
    m_LightLayout = Vk::Descriptors::CreateSetLayout({
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    });

    m_GeometryLayout = Vk::Descriptors::CreateSetLayout({
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    });
}

void CleanupLayouts()
{
    Vk::Descriptors::DestroySetLayout(m_GeometryLayout);
    Vk::Descriptors::DestroySetLayout(m_LightLayout);
}

const InitRegistrar RegisterLayouts("light_layouts_init", InitLayouts, 26);
const CleanRegistrar CleanupRegistration("light_layouts_cleanup", CleanupLayouts, 78);
}

VkDescriptorSetLayout GetLightSetLayout() { return m_LightLayout; }
VkDescriptorSetLayout GetGeometrySetLayout() { return m_GeometryLayout; }

LightPoint::LightPoint(CreateInfo p_Info) : Resource("light"), m_Info(std::move(p_Info))
{
}

void LightPoint::BeginLoad()
{
    if (!m_Info.Shader) {
        SetFailed("light requires a shader resource");

        return;
    }

    Update();
}

void LightPoint::Update()
{
    using State = ::Engine::Util::Applied::ResourceState;
    if (GetState() != State::LoadingCpu) { return; }

    switch (m_Info.Shader.GetState()) {
        case State::Ready:    SetState(State::Ready); break;
        case State::Failed:   SetFailed(m_Info.Shader->GetFailureReason()); break;
        case State::Rejected: SetRejected(m_Info.Shader->GetFailureReason()); break;

        default: break;
    }
}

void LightPoint::Unload() { m_Info = {}; }

}
