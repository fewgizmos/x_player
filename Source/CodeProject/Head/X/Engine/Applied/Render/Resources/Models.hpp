#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Materials.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include <glm/mat4x4.hpp>

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

struct ModelSnapshot
{
    std::vector<ModelPart> Parts;
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

private:
    CreateInfo m_CreateInfo;
    std::vector<ModelPart> m_Parts;
    std::shared_ptr<const ModelSnapshot> m_Snapshot;

    struct LoadData;
    std::shared_ptr<LoadData> m_LoadData;
};

}
