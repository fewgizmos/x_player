#pragma once

#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace Engine::Applied::Render::SceneRendering::Resources
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

struct LightPoint
{
    using Texture = ::Engine::Applied::Render::Resources::Textures::Texture;

    LightType Type = LightType::Point;
    bool Enabled   = true;

    glm::vec3 Color = glm::vec3(1.0f);
    float Intensity = 1.0f;
    float Range     = 0.0f;

    float InnerConeAngle = 0.0f;
    float OuterConeAngle = 0.78539816f;

    glm::vec2 Size = glm::vec2(1.0f);
    float Radius   = 0.5f;
    float Length   = 1.0f;

    ::Engine::Util::Applied::ResourceHandle<Texture> Hdri;
    glm::vec3 SkyColor     = {0.22f, 0.42f, 0.80f};
    glm::vec3 HorizonColor = {0.65f, 0.72f, 0.82f};
    glm::vec3 GroundColor  = {0.15f, 0.12f, 0.09f};
    bool ShowBackground    = false;

    void LoadHdri(std::string p_Path);
};

struct LightInstance
{
    LightPoint Light;
    glm::mat4 AppliedTransform{1.0f};
};

}
