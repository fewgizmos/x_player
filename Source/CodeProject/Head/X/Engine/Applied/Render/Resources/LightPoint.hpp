#pragma once

#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace Engine::Applied::Render::Resources
{

enum class LightType : uint32_t
{
    Point,
    Spot,
    Directional,
    Rectangle,
    Disk,
    Sphere,
    Tube,
    Hdri,
    Sky
};

struct LightPointCreateInfo
{
    using Texture = ::Engine::Applied::Render::Resources::Textures::Texture;

    ::Engine::Util::Applied::ResourceHandle<Shaders::Shader> Shader;
    LightType Type = LightType::Point;
    bool Enabled   = true;

    glm::vec3 Color = glm::vec3(1.0f);
    float Intensity = 1.0f;
    float Range     = 0.0f;

    float InnerConeAngle = 0.0f;
    float OuterConeAngle = 0.78539816f;

    ::Engine::Util::Applied::ResourceHandle<Texture> Hdri;
    glm::vec3 SkyColor     = {0.22f, 0.42f, 0.80f};
    glm::vec3 HorizonColor = {0.65f, 0.72f, 0.82f};
    glm::vec3 GroundColor  = {0.15f, 0.12f, 0.09f};
    bool ShowBackground    = false;

    void LoadHdri(std::string p_Path);
};

class LightPoint final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = LightPointCreateInfo;

    explicit LightPoint(CreateInfo p_Info);

    const CreateInfo& GetInfo() const { return m_Info; }

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override { return "LightPoint"; }

    bool AllowsCaching() const override { return false; }

private:
    CreateInfo m_Info;
};

VkDescriptorSetLayout GetLightSetLayout();
VkDescriptorSetLayout GetGeometrySetLayout();

::Engine::Util::Applied::ResourceHandle<Shaders::Shader> MakeStdLightShader();

}
