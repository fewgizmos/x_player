#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine::Applied::Render::Resources::Materials
{

constexpr uint32_t NO_TEXTURE              = 0xFFFFFFFFu;
constexpr uint32_t MATERIAL_TEXTURES_COUNT = 14;

enum class AlphaMode : uint32_t
{
    Opaque = 0,
    Mask   = 1
};

struct MaterialTexture
{
    ::Engine::Util::Applied::ResourceRef<
        ::Engine::Applied::Render::Resources::Textures::Texture> Target;

    ::Engine::Base::Render::SamplerCreateInfo Sampler;

    glm::vec2 UvOffset = glm::vec2(0.0f);
    glm::vec2 UvScale = glm::vec2(1.0f);
    float UvRotation = 0.0f;
};

struct MaterialCreateInfo
{
    ::Engine::Util::Applied::ResourceRef<
        ::Engine::Applied::Render::Resources::Shaders::Shader> TargetShader;
    VkDescriptorSet ExtraSet = VK_NULL_HANDLE;

    glm::vec4 BaseColor = glm::vec4(1.0f);
    glm::vec3 Emissive = glm::vec3(0.0f);

    float Metallic = 1.0f;
    float Roughness = 1.0f;
    float NormalScale = 1.0f;
    float OcclusionStrength = 1.0f;
    float AlphaCutoff = 0.5f;

    AlphaMode Alpha = AlphaMode::Opaque;
    bool DoubleSided = false;
    bool CastShadows = true;

    float Ior = 1.5f;
    float SpecularFactor = 1.0f;
    float EmissiveStrength = 1.0f;
    glm::vec3 SpecularColor = glm::vec3(1.0f);

    float DiffuseTransmissionFactor = 0.0f;
    glm::vec3 DiffuseTransmissionColor = glm::vec3(1.0f);

    float ClearcoatFactor = 0.0f;
    float ClearcoatRoughness = 0.0f;
    float ClearcoatNormalScale = 1.0f;

    glm::vec3 SheenColor = glm::vec3(0.0f);
    float SheenRoughness = 0.0f;

    MaterialTexture BaseColorTexture;
    MaterialTexture MetallicRoughnessTexture;
    MaterialTexture NormalTexture;
    MaterialTexture OcclusionTexture;
    MaterialTexture EmissiveTexture;
    MaterialTexture SpecularTexture;
    MaterialTexture SpecularColorTexture;
    MaterialTexture DiffuseTransmissionTexture;
    MaterialTexture DiffuseTransmissionColorTexture;
    MaterialTexture ClearcoatTexture;
    MaterialTexture ClearcoatRoughnessTexture;
    MaterialTexture ClearcoatNormalTexture;
    MaterialTexture SheenColorTexture;
    MaterialTexture SheenRoughnessTexture;
    MaterialTexture HeightTexture;

    float HeightScale = 0.0f;
    float HeightWorld = 0.0f;
};

struct MaterialData
{
    glm::vec4 BaseColor;

    glm::vec3 Emissive;
    float Metallic;

    float Roughness;
    float NormalScale;
    float OcclusionStrength;
    float AlphaCutoff;

    uint32_t BaseColorTexture;
    uint32_t BaseColorSampler;
    uint32_t MetallicRoughnessTexture;
    uint32_t MetallicRoughnessSampler;
    uint32_t NormalTexture;
    uint32_t NormalSampler;
    uint32_t OcclusionTexture;
    uint32_t OcclusionSampler;
    uint32_t EmissiveTexture;
    uint32_t EmissiveSampler;
    uint32_t AlphaMode;
    uint32_t SpecularTexture;
    uint32_t SpecularSampler;
    uint32_t SpecularColorTexture;
    uint32_t SpecularColorSampler;
    uint32_t DiffuseTransmissionTexture;
    uint32_t DiffuseTransmissionSampler;
    uint32_t DiffuseTransmissionColorTexture;
    uint32_t DiffuseTransmissionColorSampler;
    uint32_t ClearcoatTexture;
    uint32_t ClearcoatSampler;
    uint32_t ClearcoatRoughnessTexture;
    uint32_t ClearcoatRoughnessSampler;
    uint32_t ClearcoatNormalTexture;
    uint32_t ClearcoatNormalSampler;
    uint32_t SheenColorTexture;
    uint32_t SheenColorSampler;
    uint32_t SheenRoughnessTexture;
    uint32_t SheenRoughnessSampler;
    uint32_t HeightTexture;
    uint32_t HeightSampler;
    uint32_t Padding0;

    glm::vec4 UvTransforms[MATERIAL_TEXTURES_COUNT * 2];

    glm::vec3 SpecularColor;
    float SpecularFactor;
    float Ior;
    float EmissiveStrength;
    float ClearcoatFactor;
    float ClearcoatRoughness;
    glm::vec3 DiffuseTransmissionColor;
    float DiffuseTransmissionFactor;
    glm::vec3 SheenColor;
    float SheenRoughness;
    float ClearcoatNormalScale;
    float HeightScale;
    float HeightWorld;
    float Padding1;
};

struct MaterialSnapshot
{
    MaterialData Data = {};
    uint32_t Index = UINT32_MAX;
    uint64_t VersionChecksum = 0;
};

struct MaterialBufferSnapshot
{
    ::Engine::Base::Render::Buffer Buffer;
    VkDeviceAddress Address = 0;
    uint64_t Version = 0;
};

class Material final : public ::Engine::Util::Applied::Resource
{
public:
    explicit Material(MaterialCreateInfo p_CreateInfo);
    ~Material() override;

    std::string GetTypeName() const override;
    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    uint32_t GetIndex() const;
    const MaterialData& GetData() const;
    bool IsDoubleSided() const;
    bool CastsShadows() const;
    AlphaMode GetAlphaMode() const;
    const ::Engine::Util::Applied::ResourceRef<
        ::Engine::Applied::Render::Resources::Shaders::Shader>& GetShader() const;
    VkDescriptorSet GetExtraSet() const;
    void SetGpuTicket(std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> p_Ticket);
    std::shared_ptr<const MaterialSnapshot> GetSnapshot() const;

private:
    struct LoadData;

    MaterialCreateInfo CreateInfo;
    MaterialData Data = {};
    uint32_t Index = UINT32_MAX;
    std::shared_ptr<LoadData> LoadState;
    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_GpuTicket;
    std::atomic<std::shared_ptr<const MaterialSnapshot>> m_Snapshot;
};

void Init();
void Cleanup();
void Flush();

const std::vector<MaterialData>& GetData();
const ::Engine::Base::Render::Buffer& GetBuffer();
VkDeviceAddress GetBufferAddress();
std::shared_ptr<const MaterialBufferSnapshot> GetBufferSnapshot();

}
