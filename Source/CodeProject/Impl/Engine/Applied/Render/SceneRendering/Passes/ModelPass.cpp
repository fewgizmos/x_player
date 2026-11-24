#include "X/Engine/Applied/Render/SceneRendering/Passes/ModelPass.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"

#include "X/Engine/Applied/Render/Resources/Materials.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"
#include "X/Engine/Applied/Render/Resources/Models.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>

#include <glm/matrix.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace Engine::Applied::Render::SceneRendering::Passes::ModelPass
{

namespace
{

namespace RenderBase = ::Engine::Base::Render;

using ModelPart        = ::Engine::Applied::Render::Resources::Models::ModelPart;
using ModelSnapshot    = ::Engine::Applied::Render::Resources::Models::ModelSnapshot;
using MeshSnapshot     = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using MaterialSnapshot = ::Engine::Applied::Render::Resources::Materials::MaterialSnapshot;
using ShaderSnapshot   = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;
using AlphaMode        = ::Engine::Applied::Render::Resources::Materials::AlphaMode;

struct alignas(16) PushConstants
{
    glm::mat4 Transform;

    glm::vec4 BaseColor;

    uint32_t TextureIndex;
    uint32_t SamplerIndex;

    float AlphaCutoff;
    uint32_t AlphaMode;

    glm::vec4 UvTransformRow0;
    glm::vec4 UvTransformRow1;
};

struct Instance
{
    glm::mat4 World;
    glm::vec4 Normal[3];
};

static_assert(sizeof(Instance) == 7 * sizeof(glm::vec4));
static_assert(sizeof(PushConstants) == 128);

struct Draw
{
    std::shared_ptr<const ShaderSnapshot> Shader;
    std::shared_ptr<const MeshSnapshot> Mesh;
    std::shared_ptr<const MaterialSnapshot> Material;
    Instance Data;
    AlphaMode Alpha;
    bool DoubleSided;
    bool Mirrored;
    bool DynamicFaces;
    bool Instanced;
    float Depth;

    auto GetBatchKey() const
    {
        return std::tuple(
            Alpha, Shader->Graphics.Handle, Mesh->VertexBuffer.Handle, Mesh->IndexBuffer.Handle,
            Material->Index, DoubleSided, Mirrored
        );
    }
};

std::array<RenderBase::Buffer, Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT> m_InstanceBuffers;

void Cleanup();

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("scene_model_pass_init", Init, 30);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterCleanup("model_instance_buffers_cleanup", Cleanup, 80);

bool IsOutsideFrustum(const glm::mat4& p_Clip, glm::vec3 p_Center, glm::vec3 p_Extent)
{
    const glm::mat4 Rows = glm::transpose(p_Clip);
    const glm::vec4 Planes[] = {
        Rows[3] + Rows[0], Rows[3] - Rows[0],
        Rows[3] + Rows[1], Rows[3] - Rows[1],
        Rows[2], Rows[3] - Rows[2]
    };

    for (const glm::vec4& Plane : Planes) {
        const glm::vec3 Normal(Plane);
        const float Distance = glm::dot(Normal, p_Center) + Plane.w;
        const float Radius = glm::dot(glm::abs(Normal), p_Extent);
        if (Distance + Radius < 0.0f) { return true; }
    }

    return false;
}

Instance BuildInstance(const glm::mat4& p_World, bool& p_Mirrored)
{
    const glm::vec3 AxisX(p_World[0]);
    const glm::vec3 AxisY(p_World[1]);
    const glm::vec3 AxisZ(p_World[2]);

    const glm::vec3 CofactorX = glm::cross(AxisY, AxisZ);
    p_Mirrored = glm::dot(AxisX, CofactorX) < 0.0f;
    const float Sign = p_Mirrored ? -1.0f : 1.0f;

    Instance Result;
    Result.World = p_World;
    Result.Normal[0] = glm::vec4(Sign * CofactorX, 0.0f);
    Result.Normal[1] = glm::vec4(Sign * glm::cross(AxisZ, AxisX), 0.0f);
    Result.Normal[2] = glm::vec4(Sign * glm::cross(AxisX, AxisY), 0.0f);

    return Result;
}

void CollectDraws(const SceneList::Frame& p_Scene, std::vector<Draw>& p_Draws)
{
    const auto& Camera = *p_Scene.ActiveCamera;

    for (const SceneList::ModelEntry& Entry : p_Scene.Models) {
        const std::shared_ptr<const ModelSnapshot> Model = Entry.Target->GetSnapshot();

        if (!Model) { continue; }

        const glm::mat4 ModelClip = Camera.ViewProjection * Entry.AppliedTransform;

        if (IsOutsideFrustum(ModelClip, Model->BoundsCenter, Model->BoundsExtent)) { continue; }

        for (const ModelPart& Part : Model->Parts) {
            const std::shared_ptr<const MeshSnapshot> Mesh = Part.Mesh->GetSnapshot();
            const std::shared_ptr<const MaterialSnapshot> Material = Part.Material->GetSnapshot();

            const auto& Target = Part.Material->GetShader();

            const std::shared_ptr<const ShaderSnapshot> Shader = Target->GetSnapshot();

            if (!Mesh || !Material || !Shader || Shader->Graphics.Handle == VK_NULL_HANDLE) { continue; }

            const glm::mat4 World = Entry.AppliedTransform * Part.PrivateTransform;
            const glm::vec3 Center = (Mesh->BoundsMax + Mesh->BoundsMin) * 0.5f;
            const glm::vec3 Extent = (Mesh->BoundsMax - Mesh->BoundsMin) * 0.5f;

            if (Model->Parts.size() > 1 && IsOutsideFrustum(ModelClip * Part.PrivateTransform, Center, Extent)) {
                continue;
            }

            const auto& Graphics = Target->GetCreateInfo().Graphics;

            Draw& Item = p_Draws.emplace_back();
            Item.Shader       = Shader;
            Item.Mesh         = Mesh;
            Item.Material     = Material;
            Item.Data         = BuildInstance(World, Item.Mirrored);
            Item.Alpha        = Part.Material->GetAlphaMode();
            Item.DoubleSided  = Part.Material->IsDoubleSided();
            Item.DynamicFaces = Graphics.DynamicFaces;
            Item.Instanced    = Graphics.ModelInstanceTransforms;
            Item.Depth        = -(Camera.View * World * glm::vec4(Center, 1.0f)).z;
        }
    }
}

void Cleanup()
{
    for (auto& Buffer : m_InstanceBuffers) { RenderBase::ResourceLoader::DestroyBuffer(Buffer); }
}

VkBuffer UploadInstances(const std::vector<Draw>& p_Draws)
{
    auto& Buffer = m_InstanceBuffers[RenderBase::Vk::Frame::GetCurrentFrameIndex()];
    const VkDeviceSize Bytes = p_Draws.size() * sizeof(Instance);

    if (Buffer.Size < Bytes) {
        const RenderBase::Buffer Replacement = RenderBase::ResourceLoader::CreateBuffer(
            std::max(Bytes, Buffer.Size * 2),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        RenderBase::ResourceLoader::DestroyBuffer(Buffer);
        Buffer = Replacement;
    }

    const VmaAllocator Allocator = RenderBase::Vk::GpuResources::GetAllocator();
    VmaAllocationInfo Info{};
    vmaGetAllocationInfo(Allocator, Buffer.Allocation, &Info);
    auto* Destination = static_cast<unsigned char*>(Info.pMappedData);

    X_ASSERT(!Destination, "instance buffer is not mapped");

    for (const auto& Entry : p_Draws) {
        std::memcpy(Destination, &Entry.Data, sizeof(Instance));
        Destination += sizeof(Instance);
    }

    X_ASSERT(vmaFlushAllocation(Allocator, Buffer.Allocation, 0, Bytes) != VK_SUCCESS,
        "failed to flush instances"
    );

    return Buffer.Handle;
}

}

void Init()
{
    SceneRenderPassPool::Register("model", Render, 900);
}

void Render()
{
    const std::shared_ptr<const SceneList::Frame> Scene = SceneList::Acquire();

    if (!Scene || !Scene->ActiveCamera) { return; }

    std::vector<Draw> Draws;
    Draws.reserve(Scene->Models.size());

    CollectDraws(*Scene, Draws);

    if (Draws.empty()) { return; }

    std::stable_sort(Draws.begin(), Draws.end(), [](const Draw& p_Left, const Draw& p_Right) {
        if (p_Left.Alpha == AlphaMode::Blend && p_Right.Alpha == AlphaMode::Blend) {
            return p_Left.Depth > p_Right.Depth;
        }

        return p_Left.GetBatchKey() < p_Right.GetBatchKey();
    });

    const VkCommandBuffer Cmd = RenderBase::Vk::Frame::GetCurrentFrame().CommandBuffer;
    const auto& Disp = RenderBase::Vk::Device::GetDisp();
    const VkBuffer InstanceBuffer = UploadInstances(Draws);
    const VkDeviceSize Offset = 0;

    Disp.cmdBindVertexBuffers(Cmd, 1, 1, &InstanceBuffer, &Offset);

    RenderBase::Pipeline BoundPipeline{};
    VkBuffer VertexBuffer = VK_NULL_HANDLE;
    VkBuffer IndexBuffer = VK_NULL_HANDLE;

    const VkDescriptorSet DescriptorSet = ::Engine::Applied::Render::Resources::Textures::GetBindlessSet();

    for (std::size_t First = 0; First < Draws.size();) {
        const Draw& Entry = Draws[First];
        std::size_t End = First + 1;

        while (Entry.Instanced && End < Draws.size() && Draws[End].GetBatchKey() == Entry.GetBatchKey()) {
            ++End;
        }

        const auto& Pipeline = Entry.Shader->Graphics;

        if (BoundPipeline.Handle != Pipeline.Handle) {
            Disp.cmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Handle);
        }

        if (BoundPipeline.Layout != Pipeline.Layout) {
            Disp.cmdBindDescriptorSets(
                Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Layout,
                0, 1, &DescriptorSet, 0, nullptr
            );
        }

        BoundPipeline = Pipeline;

        if (Entry.DynamicFaces) {
            Disp.cmdSetCullMode(Cmd, Entry.DoubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            Disp.cmdSetFrontFace(Cmd, Entry.Mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }

        if (VertexBuffer != Entry.Mesh->VertexBuffer.Handle) {
            VertexBuffer = Entry.Mesh->VertexBuffer.Handle;
            Disp.cmdBindVertexBuffers(Cmd, 0, 1, &VertexBuffer, &Offset);
        }

        if (IndexBuffer != Entry.Mesh->IndexBuffer.Handle) {
            IndexBuffer = Entry.Mesh->IndexBuffer.Handle;
            Disp.cmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        const auto& Material = Entry.Material->Data;
        glm::mat4 Transform = Scene->ActiveCamera->ViewProjection;
        if (!Entry.Instanced) { Transform *= Entry.Data.World; }

        const PushConstants Constants{
            Transform,
            Material.BaseColor,
            Material.BaseColorTexture,
            Material.BaseColorSampler,
            Material.AlphaCutoff,
            Material.AlphaMode,
            Material.UvTransforms[0],
            Material.UvTransforms[1]
        };

        RenderBase::Vk::Commands::PushConstants(
            Cmd,
            Pipeline.Layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            sizeof(Constants),
            &Constants
        );
        Disp.cmdDrawIndexed(
            Cmd,
            Entry.Mesh->IndexCount,
            static_cast<uint32_t>(End - First),
            0,
            0,
            Entry.Instanced ? static_cast<uint32_t>(First) : 0
        );

        First = End;
    }
}

}
