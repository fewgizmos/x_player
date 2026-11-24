#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Materials.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine::Applied::Render::Resources::Models
{

struct ModelCreateInfo
{
    std::string GltfPath;
    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader> Shader;
    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader> BlendedShader{};
};

struct ModelPart
{
    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Meshes::Mesh> Mesh;

    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Materials::Material> Material;

    ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Meshes::MeshSkeleton> Skeleton;

    std::vector<::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Meshes::MeshMorphTarget>> MorphTargets;

    std::vector<float> MorphWeights;
    std::vector<glm::mat4> BindPoseMatrices;

    glm::mat4 PrivateTransform = glm::mat4(1.0f);
};

struct PunctualLight
{
    enum class Kind
    {
        Point,
        Spot,
        Directional
    };

    Kind Type = Kind::Point;

    glm::vec3 Color = glm::vec3(1.0f);
   
    float Intensity = 1.0f;
    float Range = 0.0f;
    float InnerConeAngle = 0.0f;
    float OuterConeAngle = 0.78539816f;

    glm::mat4 NodeTransform{1.0f};
};

struct ModelSnapshot
{
    std::vector<ModelPart> Parts;
    std::vector<PunctualLight> Lights;

    glm::vec3 BoundsCenter = glm::vec3(0.0f);
    glm::vec3 BoundsExtent = glm::vec3(0.0f);

    uint64_t VersionChecksum = 0;
};

class Model final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = ModelCreateInfo;

    explicit Model(CreateInfo p_CreateInfo);
    ~Model() override;

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const std::vector<ModelPart>& GetParts() const;
    std::shared_ptr<const ModelSnapshot> GetSnapshot() const;
    const ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader>& GetShader() const;

private:
    CreateInfo m_CreateInfo;
    std::vector<ModelPart> m_Parts;
    std::atomic<std::shared_ptr<const ModelSnapshot>> m_Snapshot;

    struct LoadData;
    std::shared_ptr<LoadData> m_LoadData;
};

}
