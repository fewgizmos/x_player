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
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Engine::Applied::Render::Resources::Materials
{

constexpr uint32_t NO_TEXTURE              = 0xFFFFFFFFu;
constexpr uint32_t MATERIAL_TEXTURES_COUNT = 21;

enum MaterialFlags : uint32_t
{
    MaterialUnlit      = 1u << 0,
    MaterialDoubleSided = 1u << 1,
    MaterialCastShadows = 1u << 2
};

enum class AlphaMode : uint32_t
{
    Opaque = 0,
    Mask   = 1,
    Blend  = 2
};

struct MaterialTexture
{
    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Textures::Texture> Target;

    ::Engine::Base::Render::SamplerCreateInfo Sampler;

    glm::vec2 UvOffset = glm::vec2(0.0f);
    glm::vec2 UvScale = glm::vec2(1.0f);
    float UvRotation = 0.0f;
    uint32_t TexCoord = 0;
};

struct MaterialCreateInfo
{
    ::Engine::Util::Applied::ResourceHandle<
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
    bool Unlit = false;

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

    float TransmissionFactor = 0.0f;
    float ThicknessFactor = 0.0f;
    float AttenuationDistance = std::numeric_limits<float>::infinity();
    glm::vec3 AttenuationColor = glm::vec3(1.0f);
    float IridescenceFactor = 0.0f;
    float IridescenceIor = 1.3f;
    float IridescenceThicknessMin = 100.0f;
    float IridescenceThicknessMax = 400.0f;
    float AnisotropyStrength = 0.0f;
    float AnisotropyRotation = 0.0f;

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
    MaterialTexture TransmissionTexture;
    MaterialTexture ThicknessTexture;
    MaterialTexture IridescenceTexture;
    MaterialTexture IridescenceThicknessTexture;
    MaterialTexture AnisotropyTexture;
    MaterialTexture PackedNormalMetallicRoughnessTexture;

    float HeightScale = 0.0f;
    float HeightWorld = 0.0f;
};

struct alignas(16) MaterialTextureData
{
    glm::vec4 UvTransformRow0;
    glm::vec4 UvTransformRow1;

    uint32_t Texture = NO_TEXTURE;
    uint32_t Sampler = 0;
    uint32_t TexCoord = 0;
    uint32_t Padding = 0;
};

struct alignas(16) MaterialData
{
    glm::vec4 BaseColor;

    glm::vec3 Emissive;
    float Metallic;

    float Roughness;
    float NormalScale;
    float OcclusionStrength;
    float AlphaCutoff;

    glm::vec3 SpecularColor;
    float SpecularFactor;
    glm::vec3 DiffuseTransmissionColor;
    float DiffuseTransmissionFactor;
    glm::vec3 SheenColor;
    float SheenRoughness;
    glm::vec3 AttenuationColor;
    float AttenuationDistance;

    float Ior;
    float EmissiveStrength;
    float ClearcoatFactor;
    float ClearcoatRoughness;
    float ClearcoatNormalScale;
    float HeightScale;
    float HeightWorld;
    float TransmissionFactor;
    float ThicknessFactor;
    float IridescenceFactor;
    float IridescenceIor;
    float IridescenceThicknessMin;
    float IridescenceThicknessMax;
    float AnisotropyStrength;
    float AnisotropyRotation;
    uint32_t AlphaMode;

    glm::uvec4 Options = glm::uvec4(0);
    MaterialTextureData Textures[MATERIAL_TEXTURES_COUNT];
};

struct MaterialSnapshot
{
    MaterialData Data = {};
    uint32_t Index = UINT32_MAX;
    uint64_t VersionChecksum = 0;
};

class Material final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = MaterialCreateInfo;

    explicit Material(MaterialCreateInfo p_CreateInfo);

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    uint32_t GetIndex() const;
    const MaterialData& GetData() const;
    bool IsDoubleSided() const;
    bool CastsShadows() const;
    AlphaMode GetAlphaMode() const;
    const ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader>& GetShader() const;
    VkDescriptorSet GetExtraSet() const;
    std::shared_ptr<const MaterialSnapshot> GetSnapshot() const;

private:
    struct LoadData;

    MaterialCreateInfo m_CreateInfo;
    MaterialData Data = {};
    uint32_t Index = UINT32_MAX;
    std::shared_ptr<LoadData> LoadState;
    std::atomic<std::shared_ptr<const MaterialSnapshot>> m_Snapshot;
};

void Init();
void Cleanup();
void Flush();

const std::vector<MaterialData>& GetData();
const ::Engine::Base::Render::Buffer& GetBuffer();
VkDeviceAddress GetBufferAddress();

}
