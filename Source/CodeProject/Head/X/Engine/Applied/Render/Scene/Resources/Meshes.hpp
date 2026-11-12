#pragma once

#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Engine::Applied::Render::Scene::Resources::Meshes
{

struct MeshSnapshot
{
    ::Engine::Base::Render::Buffer VertexBuffer;
    ::Engine::Base::Render::Buffer IndexBuffer;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
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
};

struct Cluster
{
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    glm::vec3 BoundsMin = glm::vec3(0.0f);
    glm::vec3 BoundsMax = glm::vec3(0.0f);
};

class Mesh final : public ::Engine::Applied::Render::Resources::Resource
{
public:
    Mesh(std::vector<Vertex> p_Vertices, std::vector<uint32_t> p_Indices);
    ~Mesh() override;

    std::string GetTypeName() const override;
    void BeginLoad() override;
    void Update() override;
    void Unload() override;

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

}
