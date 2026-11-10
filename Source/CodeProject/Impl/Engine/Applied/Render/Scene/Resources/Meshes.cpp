#include "X/Engine/Applied/Render/Scene/Resources/Meshes.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Scene::Resources::Meshes
{

struct Mesh::LoadData
{
    std::mutex Mutex;

    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;

    std::vector<uint32_t> SortedIndices;
    std::vector<Cluster> Clusters;
    std::vector<Cluster> ClusterGroups;

    glm::vec3 BoundsMin = glm::vec3(0.0f);
    glm::vec3 BoundsMax = glm::vec3(0.0f);

    std::string FailureReason;
    bool Finished = false;
    bool Failed = false;
};

namespace
{

constexpr uint32_t TRIANGLE_INDICES = 3;
constexpr uint32_t CLUSTER_TRIANGLES = 128;
constexpr std::size_t GROUP_CLUSTERS = 16;
constexpr float MORTON_GRID = 1023.0f;

template<class Type>
std::size_t HashBytes(const std::vector<Type>& p_Data)
{
    const auto* Bytes = reinterpret_cast<const unsigned char*>(p_Data.data());
    std::size_t Hash = 1469598103934665603ull;

    for (std::size_t I = 0; I < p_Data.size() * sizeof(Type); ++I) {
        Hash ^= Bytes[I];
        Hash *= 1099511628211ull;
    }

    return Hash;
}

uint32_t SpreadBits(uint32_t p_Value)
{
    p_Value = (p_Value | (p_Value << 16)) & 0x030000FF;
    p_Value = (p_Value | (p_Value << 8)) & 0x0300F00F;
    p_Value = (p_Value | (p_Value << 4)) & 0x030C30C3;
    return (p_Value | (p_Value << 2)) & 0x09249249;
}

uint32_t MortonCode(const glm::vec3& p_Point, const glm::vec3& p_Min, const glm::vec3& p_Size)
{
    const glm::vec3 Cell = glm::clamp(
        (p_Point - p_Min) / glm::max(p_Size, glm::vec3(std::numeric_limits<float>::min())),
        0.0f,
        1.0f
    ) * MORTON_GRID;

    return SpreadBits(static_cast<uint32_t>(Cell.x))
        | (SpreadBits(static_cast<uint32_t>(Cell.y)) << 1)
        | (SpreadBits(static_cast<uint32_t>(Cell.z)) << 2);
}

}

Mesh::Mesh(std::vector<Vertex> p_Vertices, std::vector<uint32_t> p_Indices)
    : Resource(std::to_string(HashBytes(p_Vertices)) + ":" + std::to_string(HashBytes(p_Indices)))
    , m_Vertices(std::move(p_Vertices))
    , m_Indices(std::move(p_Indices))
    , m_VertexCount(static_cast<uint32_t>(m_Vertices.size()))
    , m_IndexCount(static_cast<uint32_t>(m_Indices.size()))
    , LoadDataState(std::make_shared<LoadData>())
{
}

Mesh::~Mesh() = default;

std::string Mesh::GetTypeName() const
{
    return "Mesh";
}

void Mesh::BeginLoad()
{
    const std::shared_ptr<LoadData> Data = LoadDataState;

    Data->Vertices = std::move(m_Vertices);
    Data->Indices = std::move(m_Indices);

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineCore::ResourceLoaderTh,
        "mesh_load_" + GetKey(),
        [Data] {
            try {
                if (Data->Vertices.empty() || Data->Indices.empty() || Data->Indices.size() % TRIANGLE_INDICES != 0) {
                    Data->FailureReason = "mesh data is empty or has incomplete triangles";
                    Data->Failed = true;
                } else {
                    Data->BoundsMin = glm::vec3(std::numeric_limits<float>::max());
                    Data->BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

                    for (const Vertex& VertexValue : Data->Vertices) {
                        Data->BoundsMin = glm::min(Data->BoundsMin, VertexValue.Position);
                        Data->BoundsMax = glm::max(Data->BoundsMax, VertexValue.Position);
                    }

                    for (const uint32_t Index : Data->Indices) {
                        if (Index >= Data->Vertices.size()) {
                            Data->FailureReason = "mesh index is out of range";
                            Data->Failed = true;
                            break;
                        }
                    }

                    if (!Data->Failed) {
                        std::vector<std::pair<uint32_t, uint32_t>> Keys(Data->Indices.size() / TRIANGLE_INDICES);

                        for (uint32_t I = 0; I < Keys.size(); ++I) {
                            const glm::vec3 Centroid = (
                                Data->Vertices[Data->Indices[I * TRIANGLE_INDICES]].Position
                                + Data->Vertices[Data->Indices[I * TRIANGLE_INDICES + 1]].Position
                                + Data->Vertices[Data->Indices[I * TRIANGLE_INDICES + 2]].Position
                            ) / 3.0f;

                            Keys[I] = { MortonCode(Centroid, Data->BoundsMin, Data->BoundsMax - Data->BoundsMin), I };
                        }

                        std::sort(Keys.begin(), Keys.end());

                        Data->SortedIndices.reserve(Data->Indices.size());

                        for (const auto& Key : Keys) {
                            for (uint32_t Corner = 0; Corner < TRIANGLE_INDICES; ++Corner) {
                                Data->SortedIndices.push_back(Data->Indices[Key.second * TRIANGLE_INDICES + Corner]);
                            }
                        }

                        const uint32_t ClusterIndexCount = CLUSTER_TRIANGLES * TRIANGLE_INDICES;

                        for (uint32_t First = 0; First < Data->SortedIndices.size(); First += ClusterIndexCount) {
                            Cluster Value;
                            Value.FirstIndex = First;
                            Value.IndexCount = std::min(ClusterIndexCount, static_cast<uint32_t>(Data->SortedIndices.size()) - First);
                            Value.BoundsMin = glm::vec3(std::numeric_limits<float>::max());
                            Value.BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

                            for (uint32_t I = First; I < First + Value.IndexCount; ++I) {
                                Value.BoundsMin = glm::min(Value.BoundsMin, Data->Vertices[Data->SortedIndices[I]].Position);
                                Value.BoundsMax = glm::max(Value.BoundsMax, Data->Vertices[Data->SortedIndices[I]].Position);
                            }

                            Data->Clusters.push_back(Value);
                        }

                        for (std::size_t First = 0; First < Data->Clusters.size(); First += GROUP_CLUSTERS) {
                            const std::size_t Last = std::min(First + GROUP_CLUSTERS, Data->Clusters.size());
                            Cluster Group = Data->Clusters[First];
                            Group.IndexCount = Data->Clusters[Last - 1].FirstIndex
                                + Data->Clusters[Last - 1].IndexCount - Group.FirstIndex;

                            for (std::size_t I = First + 1; I < Last; ++I) {
                                Group.BoundsMin = glm::min(Group.BoundsMin, Data->Clusters[I].BoundsMin);
                                Group.BoundsMax = glm::max(Group.BoundsMax, Data->Clusters[I].BoundsMax);
                            }

                            Data->ClusterGroups.push_back(Group);
                        }
                    }
                }
            }
            catch (const std::exception& Error) {
                Data->FailureReason = Error.what();
                Data->Failed = true;
            }
            catch (...) {
                Data->FailureReason = "unknown mesh preparation failure";
                Data->Failed = true;
            }

            std::lock_guard<std::mutex> Lock(Data->Mutex);
            Data->Finished = true;
        }
    );
}

void Mesh::Update()
{
    if (GetState() != ::Engine::Applied::Render::Resources::ResourceState::Loading) {
        return;
    }

    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<Cluster> Clusters;
    std::vector<Cluster> ClusterGroups;
    glm::vec3 BoundsMin;
    glm::vec3 BoundsMax;
    std::string FailureReason;

    {
        std::lock_guard<std::mutex> Lock(LoadDataState->Mutex);

        if (!LoadDataState->Finished) {
            return;
        }

        if (LoadDataState->Failed) {
            FailureReason = std::move(LoadDataState->FailureReason);
        } else {
            Vertices = std::move(LoadDataState->Vertices);
            Indices = std::move(LoadDataState->SortedIndices);
            Clusters = std::move(LoadDataState->Clusters);
            ClusterGroups = std::move(LoadDataState->ClusterGroups);
            BoundsMin = LoadDataState->BoundsMin;
            BoundsMax = LoadDataState->BoundsMax;
        }
    }

    if (!FailureReason.empty()) {
        SetFailed(std::move(FailureReason));
        return;
    }

    try {
        m_Vertices = std::move(Vertices);
        m_Indices = std::move(Indices);
        m_Clusters = std::move(Clusters);
        m_ClusterGroups = std::move(ClusterGroups);
        m_BoundsMin = BoundsMin;
        m_BoundsMax = BoundsMax;

        m_VertexBuffer = ::Engine::Base::Render::Vk::GpuResources::CreateBuffer(
            sizeof(Vertex) * m_Vertices.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );
        m_IndexBuffer = ::Engine::Base::Render::Vk::GpuResources::CreateBuffer(
            sizeof(uint32_t) * m_Indices.size(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );

        ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(m_VertexBuffer, m_Vertices.data(), m_VertexBuffer.Size);
        ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(m_IndexBuffer, m_Indices.data(), m_IndexBuffer.Size);

        if (!KEEP_CPU_DATA) {
            m_Vertices.clear();
            m_Vertices.shrink_to_fit();
            m_Indices.clear();
            m_Indices.shrink_to_fit();
        }

        SetState(::Engine::Applied::Render::Resources::ResourceState::Ready);
    }
    catch (const std::exception& Error) {
        if (m_VertexBuffer.Handle != VK_NULL_HANDLE) {
            ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_VertexBuffer);
        }

        if (m_IndexBuffer.Handle != VK_NULL_HANDLE) {
            ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_IndexBuffer);
        }

        SetFailed(Error.what());
    }
    catch (...) {
        if (m_VertexBuffer.Handle != VK_NULL_HANDLE) {
            ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_VertexBuffer);
        }

        if (m_IndexBuffer.Handle != VK_NULL_HANDLE) {
            ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_IndexBuffer);
        }

        SetFailed("unknown mesh GPU upload failure");
    }
}

void Mesh::Unload()
{
    if (m_VertexBuffer.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_VertexBuffer);
    }

    if (m_IndexBuffer.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_IndexBuffer);
    }
}

const ::Engine::Base::Render::Buffer& Mesh::GetVertexBuffer() const { return m_VertexBuffer; }
const ::Engine::Base::Render::Buffer& Mesh::GetIndexBuffer() const { return m_IndexBuffer; }
uint32_t Mesh::GetVertexCount() const { return m_VertexCount; }
uint32_t Mesh::GetIndexCount() const { return m_IndexCount; }
const glm::vec3& Mesh::GetBoundsMin() const { return m_BoundsMin; }
const glm::vec3& Mesh::GetBoundsMax() const { return m_BoundsMax; }

}
