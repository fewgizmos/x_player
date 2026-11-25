#include "X/Engine/Applied/Render/SceneRendering/Passes/ModelPass.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Applied/Render/SceneRendering/Passes/LightPass.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"
#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"

#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"

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
using MeshSnapshot     = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using MaterialSnapshot = ::Engine::Applied::Render::Resources::Materials::MaterialSnapshot;
using ShaderSnapshot   = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;
using AlphaMode        = ::Engine::Applied::Render::Resources::Materials::AlphaMode;
using DrawConstants    = ::Engine::Applied::Render::Resources::Models::DrawConstants;
using Instance         = ::Engine::Applied::Render::Resources::Models::DrawInstance;
using Vertex = ::Engine::Applied::Render::Resources::Meshes::Vertex;

static_assert(sizeof(Instance) == 7 * sizeof(glm::vec4));
static_assert(sizeof(DrawConstants) == 96);
static_assert(offsetof(DrawConstants, Materials) == 64);
static_assert(offsetof(DrawConstants, Vertices) == 72);
static_assert(offsetof(DrawConstants, ExtraTexCoords) == 80);
static_assert(offsetof(DrawConstants, MaterialIndex) == 88);

static_assert(sizeof(Vertex) == 96);
static_assert(offsetof(Vertex, Normal) == 16);
static_assert(offsetof(Vertex, Tangent) == 32);
static_assert(offsetof(Vertex, Color) == 48);
static_assert(offsetof(Vertex, Joints) == 64);
static_assert(offsetof(Vertex, Weights) == 80);

struct Draw
{
    std::shared_ptr<const ShaderSnapshot>   Shader;
    std::shared_ptr<const MeshSnapshot>     Mesh;
    std::shared_ptr<const MaterialSnapshot> Material;

    Instance Data;

    AlphaMode Alpha;

    bool DoubleSided;
    bool Mirrored;
    bool DynamicFaces;
    bool Instanced;
    bool SceneLights;

    float Depth;

    auto GetBatchKey() const
    {
        return std::tuple(
            Alpha, Shader->Graphics.Handle, Mesh->VertexBuffer.Handle, Mesh->IndexBuffer.Handle,
            Material->Index, DoubleSided, Mirrored
        );
    }
};

struct InstanceBuffers
{
    RenderBase::Buffer Geometry;
    RenderBase::Buffer Transparent;
};

std::array<InstanceBuffers, Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT> m_InstanceBuffers;



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
    Result.Normal[0] = glm::vec4(Sign * CofactorX, Sign);
    Result.Normal[1] = glm::vec4(Sign * glm::cross(AxisZ, AxisX), 0.0f);
    Result.Normal[2] = glm::vec4(Sign * glm::cross(AxisX, AxisY), 0.0f);

    return Result;
}

void CollectDraws(const SceneList::Frame& p_Scene, std::vector<Draw>& p_Draws, bool p_Transparent)
{
    const auto& Camera = *p_Scene.ActiveCamera;

    for (const SceneList::ModelEntry& Entry : p_Scene.Models) {
        const auto& Model = Entry.Snapshot;

        if (!Model) { continue; }

        const glm::mat4 ModelClip = Camera.ViewProjection * Entry.AppliedTransform;

        if (IsOutsideFrustum(ModelClip, Model->BoundsCenter, Model->BoundsExtent)) { continue; }

        for (const ModelPart& Part : Model->Parts) {
            const AlphaMode Alpha = Part.Material->GetAlphaMode();

            if ((Alpha == AlphaMode::Blend) != p_Transparent) { continue; }

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
            Item.Alpha        = Alpha;
            Item.DoubleSided  = Part.Material->IsDoubleSided();
            Item.DynamicFaces = Graphics.DynamicFaces;
            Item.Instanced    = Graphics.ModelInstanceTransforms;
            Item.SceneLights  = p_Transparent && Graphics.SetLayouts.size() > 1 && Graphics.SetLayouts[1] == ::Engine::Applied::Render::Resources::GetLightSetLayout();
            Item.Depth        = -(Camera.View * World * glm::vec4(Center, 1.0f)).z;
        }
    }
}

void Cleanup()
{
    for (auto& Buffers : m_InstanceBuffers) {
        RenderBase::ResourceLoader::DestroyBuffer(Buffers.Geometry);
        RenderBase::ResourceLoader::DestroyBuffer(Buffers.Transparent);
    }
}

VkBuffer UploadInstances(const std::vector<Draw>& p_Draws, bool p_Transparent)
{
    auto& Buffers   = m_InstanceBuffers[RenderBase::Vk::Frame::GetCurrentFrameIndex()];
    auto& Buffer = p_Transparent ? Buffers.Transparent : Buffers.Geometry;

    const VkDeviceSize Bytes = p_Draws.size() * sizeof(Instance);

    if (Buffer.Size < Bytes) {
        const RenderBase::Buffer Replacement = RenderBase::ResourceLoader::CreateMappedBuffer(
            std::max(Bytes, Buffer.Size * 2),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        );

        RenderBase::ResourceLoader::DestroyBuffer(Buffer);

        Buffer = Replacement;
    }

    uint8_t* Destination = RenderBase::Vk::GpuResources::GetMappedData(Buffer);

    for (const auto& Entry : p_Draws) {
        std::memcpy(Destination, &Entry.Data, sizeof(Instance));

        Destination += sizeof(Instance);
    }

    RenderBase::Vk::GpuResources::FlushMappedBuffer(Buffer, 0, Bytes);

    return Buffer.Handle;
}

void Render(const SceneList::Frame& p_Scene, bool p_Transparent)
{
    std::vector<Draw> Draws;
    Draws.reserve(p_Scene.Models.size());

    CollectDraws(p_Scene, Draws, p_Transparent);

    if (Draws.empty()) { return; }

    std::stable_sort(Draws.begin(), Draws.end(), [](const Draw& p_Left, const Draw& p_Right) {
        if (p_Left.Alpha == AlphaMode::Blend && p_Right.Alpha == AlphaMode::Blend) {
            return p_Left.Depth > p_Right.Depth;
        }

        return p_Left.GetBatchKey() < p_Right.GetBatchKey();
    });

    const VkCommandBuffer Cmd = RenderBase::Vk::Frame::GetCurrentFrame().CommandBuffer;
    const auto& Disp = RenderBase::Vk::Device::GetDisp();
    const VkBuffer InstanceBuffer = UploadInstances(Draws, p_Transparent);
    const VkDeviceSize Offset = 0;
    const VkDeviceAddress Materials = ::Engine::Applied::Render::Resources::Materials::GetBufferAddress();

    Disp.cmdBindVertexBuffers(Cmd, 1, 1, &InstanceBuffer, &Offset);

    RenderBase::Pipeline BoundPipeline{};
    VkBuffer IndexBuffer = VK_NULL_HANDLE;

    const VkDescriptorSet Sets[] = {
        ::Engine::Applied::Render::Resources::Textures::GetBindlessSet(), LightPass::GetSet()
    };

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
                0, Entry.SceneLights ? 2 : 1, Sets, 0, nullptr
            );
        }

        BoundPipeline = Pipeline;

        if (Entry.DynamicFaces) {
            Disp.cmdSetCullMode(Cmd, Entry.DoubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            Disp.cmdSetFrontFace(Cmd, Entry.Mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        }

        if (IndexBuffer != Entry.Mesh->IndexBuffer.Handle) {
            IndexBuffer = Entry.Mesh->IndexBuffer.Handle;

            Disp.cmdBindIndexBuffer(Cmd, IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        glm::mat4 Transform                        = p_Scene.ActiveCamera->ViewProjection;

        if (!Entry.Instanced) { Transform *= Entry.Data.World; }

        const DrawConstants Constants{
            Transform,
            Materials,
            Entry.Mesh->VertexBuffer.Address,
            Entry.Mesh->ExtraTexCoordCount == 0 ? 0 : Entry.Mesh->VertexBuffer.Address
                + sizeof(Vertex) * Entry.Mesh->VertexCount,
            Entry.Material->Index,
            Entry.Mesh->ExtraTexCoordCount
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

void Init()
{
    SceneRenderPassPool::Register("model", SceneRenderPassPool::Phase::Geometry,
        [](const SceneList::Frame& p_Frame) { Render(p_Frame, false); }, 900);

    SceneRenderPassPool::Register("transparent_model", SceneRenderPassPool::Phase::Transparent,
        [](const SceneList::Frame& p_Frame) { Render(p_Frame, true); }, 900);
}

}
