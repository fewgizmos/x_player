#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine::Applied::Render::Resources::Meshes
{

struct MeshSnapshot
{
    ::Engine::Base::Render::Buffer VertexBuffer;
    ::Engine::Base::Render::Buffer IndexBuffer;

    uint32_t VertexCount = 0;
    uint32_t IndexCount  = 0;

    glm::vec3 BoundsMin = glm::vec3(0.0f);
    glm::vec3 BoundsMax = glm::vec3(0.0f);

    uint64_t VersionChecksum = 0;
};

inline bool KEEP_CPU_DATA = false;

struct Vertex
{
    glm::vec3 Position;
    float UvX;
    glm::vec3 Normal;
    float UvY;

    glm::vec4 Tangent;
    glm::vec4 Color;

    glm::uvec4 Joints;
    glm::vec4 Weights;
};

struct MeshCreateInfo
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
};

struct MeshSkeletonCreateInfo
{
    std::vector<int32_t> ParentIndices;
    std::vector<glm::mat4> InverseBindMatrices;
};

struct MeshMorphTargetCreateInfo
{
    std::string Name;

    std::vector<glm::vec3> PositionDeltas;
    std::vector<glm::vec3> NormalDeltas;

    float Weight = 0.0f;
};

struct MeshMorphTargetSnapshot
{
    ::Engine::Base::Render::Buffer PositionBuffer;
    ::Engine::Base::Render::Buffer NormalBuffer;
    uint32_t VertexCount = 0;
    uint64_t VersionChecksum = 0;
};

struct Cluster
{
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;

    glm::vec3 BoundsMin = glm::vec3(0.0f);
    glm::vec3 BoundsMax = glm::vec3(0.0f);
};

class Mesh final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = MeshCreateInfo;

    explicit Mesh(CreateInfo p_CreateInfo);
    ~Mesh() override;

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const ::Engine::Base::Render::Buffer& GetVertexBuffer() const;
    const ::Engine::Base::Render::Buffer& GetIndexBuffer() const;

    uint32_t GetVertexCount() const;
    uint32_t GetIndexCount() const;

    const glm::vec3& GetBoundsMin() const;
    const glm::vec3& GetBoundsMax() const;

    std::shared_ptr<const MeshSnapshot> GetSnapshot() const;

private:
    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<Cluster> m_Clusters;
    std::vector<Cluster> m_ClusterGroups;

    ::Engine::Base::Render::Buffer m_VertexBuffer;
    ::Engine::Base::Render::Buffer m_IndexBuffer;

    uint32_t m_VertexCount = 0;
    uint32_t m_IndexCount = 0;

    glm::vec3 m_BoundsMin = glm::vec3(0.0f);
    glm::vec3 m_BoundsMax = glm::vec3(0.0f);

    struct LoadData;
    std::shared_ptr<LoadData> LoadDataState;

    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_GpuTicket;
    uint64_t m_GpuVersionChecksum = 0;

    std::atomic<std::shared_ptr<const MeshSnapshot>> m_Snapshot;
};

class MeshSkeleton final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = MeshSkeletonCreateInfo;

    explicit MeshSkeleton(CreateInfo p_CreateInfo);
    ~MeshSkeleton() override;

    void BeginLoad() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const CreateInfo& GetData() const;

private:
    CreateInfo m_CreateInfo;
};

class MeshMorphTarget final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = MeshMorphTargetCreateInfo;

    explicit MeshMorphTarget(CreateInfo p_CreateInfo);
    ~MeshMorphTarget() override;

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const CreateInfo& GetData() const;
    std::shared_ptr<const MeshMorphTargetSnapshot> GetSnapshot() const;

private:
    CreateInfo m_CreateInfo;

    struct LoadData;
    std::shared_ptr<LoadData> m_LoadData;

    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> m_GpuTicket;
    std::atomic<std::shared_ptr<const MeshMorphTargetSnapshot>> m_Snapshot;
};

}
