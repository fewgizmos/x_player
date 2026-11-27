#include <glm/ext/matrix_transform.hpp>

#include "X/Engine/Applied/Render/SceneRendering/Passes/LightPass.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/Descriptors.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace Engine::Applied::Render::SceneRendering::Passes::LightPass
{

namespace
{

namespace RenderBase = ::Engine::Base::Render;
namespace Vk         = RenderBase::Vk;

namespace Textures = ::Engine::Applied::Render::Resources::Textures;

namespace LightResources = ::Engine::Applied::Render::Resources;

using LightInstance = SceneList::LightInstance;
using LightType     = LightResources::LightType;

constexpr uint32_t FrameCount = Project::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT;

struct alignas(16) FrameData
{
    glm::mat4 InverseViewProjection;
    glm::uvec4 Counts;
};

struct alignas(16) GpuLight
{
    glm::vec4 PositionRange;
    glm::vec4 DirectionIntensity;

    glm::vec4 Color;

    glm::vec4 AxisXSize;
    glm::vec4 AxisYSize;

    glm::vec4 Shape;

    glm::vec4 SkyColor;
    glm::vec4 HorizonColor;
    glm::vec4 GroundColor;

    glm::uvec4 Options;
};

static_assert(sizeof(FrameData) == 80);
static_assert(sizeof(GpuLight)  == 160);

struct FrameResources
{
    std::vector<::Engine::Util::Applied::ResourceHandle<LightResources::Shaders::Shader>> Shaders;

    RenderBase::Buffer Buffer;

    VkDescriptorSet LightSet    = VK_NULL_HANDLE;
    VkDescriptorSet GeometrySet = VK_NULL_HANDLE;

    std::vector<::Engine::Util::Applied::ResourceHandle<Textures::Texture>> Textures;
};

std::array<FrameResources, FrameCount> m_Frames;

VkDescriptorPool m_Pool = VK_NULL_HANDLE;

VkSampler m_GeometrySampler = VK_NULL_HANDLE;

uint32_t m_HdriSampler = 0;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_light_pass_init", Init, 25);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterCleanup("scene_light_pass_cleanup", Cleanup, 79);

glm::vec3 NormalizeAxis(glm::vec3 p_Axis, glm::vec3 p_Fallback)
{
    const float LengthSquared = glm::dot(p_Axis, p_Axis);

    return LengthSquared > 0.000001f ? p_Axis / std::sqrt(LengthSquared) : p_Fallback;
}

GpuLight PackLight(const LightInstance& p_Instance, uint32_t p_TextureIndex)
{
    const auto& Light    = p_Instance.Light->GetInfo();
    const auto& Transform = p_Instance.AppliedTransform;

    const glm::vec3 Direction = NormalizeAxis(-glm::vec3(Transform[2]), {0.0f, 0.0f, -1.0f});

    const glm::vec3 WorldX(Transform[0]);

    const glm::vec3 Reference = std::abs(Direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);

    const glm::vec3 AxisX = NormalizeAxis(WorldX - Direction * glm::dot(WorldX, Direction),
        glm::normalize(glm::cross(Direction, Reference)));

    const glm::vec3 AxisY = glm::cross(AxisX, Direction);

    const float OuterAngle = std::clamp(Light.OuterConeAngle, 0.001f, 1.57079633f);
    const float InnerAngle = std::clamp(Light.InnerConeAngle, 0.0f, OuterAngle);

    const glm::vec3 Size = glm::vec3(glm::length(glm::vec3(Transform[0])), glm::length(glm::vec3(Transform[1])),
        glm::length(glm::vec3(Transform[2]))) * Project::Configs::Static::Scene::MetersPerUnit;

    return {
        glm::vec4(glm::vec3(Transform[3]) * Project::Configs::Static::Scene::MetersPerUnit, std::max(Light.Range, 0.0f)),

        glm::vec4(Direction, std::max(Light.Intensity, 0.0f)),

        glm::vec4(glm::max(Light.Color, glm::vec3(0.0f)), 0.0f),

        glm::vec4(AxisX, Size.x),
        glm::vec4(AxisY, Size.y),

        glm::vec4(std::cos(InnerAngle), std::cos(OuterAngle), (Light.Type == LightType::Tube ? Size.y : Size.x) * 0.5f, Size.x),

        glm::vec4(Light.SkyColor, 0.0f),
        glm::vec4(Light.HorizonColor, 0.0f),
        glm::vec4(Light.GroundColor, 0.0f),

        glm::uvec4(static_cast<uint32_t>(Light.Type), p_TextureIndex, Light.ShowBackground ? 1u : 0u, 0u)
    };
}

void Render(const SceneList::Frame&)
{
    const auto& Frame          = Vk::Frame::GetCurrentFrame();
    const auto& Disp  = Vk::Device::GetDisp();

    const auto& Resources = m_Frames[Vk::Frame::GetCurrentFrameIndex()];

    const VkDescriptorSet Sets[] = { Textures::GetBindlessSet(), Resources.LightSet, Resources.GeometrySet };

    for (uint32_t Index = 0; Index < Resources.Shaders.size(); ++Index) {
        const auto Shader = Resources.Shaders[Index]->GetSnapshot();

        if (!Shader) { continue; }

        const auto& Pipeline = Shader->Graphics;

        Disp.cmdBindPipeline(Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Handle);

        Disp.cmdBindDescriptorSets(
            Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Layout,
            0, 3, Sets, 0, nullptr
        );

        Disp.cmdDraw(Frame.CommandBuffer, 3, 1, 0, Index);
    }
}

}

void Init()
{
    m_Pool = Vk::Descriptors::CreatePool(2 * FrameCount, {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FrameCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * FrameCount}
    });

    for (auto& Frame : m_Frames) {
        Frame.LightSet    = Vk::Descriptors::AllocateSet(m_Pool, LightResources::GetLightSetLayout());
        Frame.GeometrySet = Vk::Descriptors::AllocateSet(m_Pool, LightResources::GetGeometrySetLayout());
    }

    RenderBase::SamplerCreateInfo Sampler;
    Sampler.WrapV      = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.Anisotropy = false;

    m_HdriSampler = Vk::Samplers::Make(Sampler);

    Sampler.MagFilter = VK_FILTER_NEAREST;
    Sampler.MinFilter = VK_FILTER_NEAREST;
    Sampler.MipMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Sampler.WrapU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    m_GeometrySampler = Vk::Samplers::Get(Vk::Samplers::Make(Sampler));

    SceneRenderPassPool::Register("light", SceneRenderPassPool::Phase::Light, Render, 900);
}

void Cleanup()
{
    Vk::Descriptors::DestroyPool(m_Pool);

    for (auto& Frame : m_Frames) {
        RenderBase::ResourceLoader::DestroyBuffer(Frame.Buffer);

        Frame = {};
    }

    m_Pool = VK_NULL_HANDLE;
   
    m_GeometrySampler = VK_NULL_HANDLE;
}

void Prepare(const SceneList::Frame& p_Frame, VkImageView p_Albedo, VkImageView p_Normal, VkImageView p_Depth)
{
    auto& Frame = m_Frames[Vk::Frame::GetCurrentFrameIndex()];
    Frame.Textures.clear();
    Frame.Shaders.clear();

    std::vector<GpuLight> Lights;
    Lights.reserve(p_Frame.Lights.size());

    for (const LightInstance& Instance : p_Frame.Lights) {
        uint32_t TextureIndex = UINT32_MAX;

        if (!Instance.Light.IsReady()) { continue; }

        const auto& Light = Instance.Light->GetInfo();

        if (!Light.Shader.IsReady()) { continue; }

        if (Light.Type == LightType::Hdri) {
            const auto& Hdri = Light.Hdri;

            if (!Hdri.IsReady()) { continue; }

            const auto Texture = Hdri->GetSnapshot();

            if (!Texture) { continue; }

            TextureIndex = Texture->Index;
            Frame.Textures.push_back(Hdri);
        }

        Frame.Shaders.push_back(Light.Shader);
        Lights.push_back(PackLight(Instance, TextureIndex));
    }

    const glm::mat4 WorldToMeters = glm::scale(glm::mat4(1.0f), glm::vec3(Project::Configs::Static::Scene::MetersPerUnit));
    const FrameData Header{
        WorldToMeters * p_Frame.ActiveCamera->InverseViewProjection,
        glm::uvec4(static_cast<uint32_t>(Lights.size()), m_HdriSampler, glm::floatBitsToUint(Project::Configs::Static::Scene::MetersPerUnit), 0u)
    };

    const VkDeviceSize LightBytes = Lights.size() * sizeof(GpuLight);
    const VkDeviceSize Bytes      = sizeof(Header) + LightBytes;
    const VkDeviceSize MaxSize    = Vk::Device::GetPhysical().properties.limits.maxStorageBufferRange;

    X_ASSERT(Bytes > MaxSize, "scene lights exceed the storage buffer range");

    if (Frame.Buffer.Size < Bytes) {
        const RenderBase::Buffer Buffer = RenderBase::ResourceLoader::CreateMappedBuffer(
            std::min(MaxSize, std::max(Bytes, Frame.Buffer.Size * 2)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );

        RenderBase::ResourceLoader::DestroyBuffer(Frame.Buffer);

        Frame.Buffer = Buffer;

        Vk::Descriptors::WriteBuffer(Frame.LightSet, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Frame.Buffer);
    }

    uint8_t* Destination = Vk::GpuResources::GetMappedData(Frame.Buffer);
    std::memcpy(Destination, &Header, sizeof(Header));

    if (!Lights.empty()) { std::memcpy(Destination + sizeof(Header), Lights.data(), LightBytes); }

    Vk::GpuResources::FlushMappedBuffer(Frame.Buffer, 0, Bytes);

    const VkImageView Views[] = {p_Albedo, p_Normal, p_Depth};

    for (uint32_t Index = 0; Index < 3; ++Index) {
        Vk::Descriptors::WriteImage(
            Frame.GeometrySet, Index, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            Views[Index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_GeometrySampler
        );
    }
}

VkDescriptorSet GetSet() { return m_Frames[Vk::Frame::GetCurrentFrameIndex()].LightSet; }

}
