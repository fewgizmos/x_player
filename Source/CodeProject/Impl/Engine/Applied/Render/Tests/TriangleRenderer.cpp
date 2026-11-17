#include "X/Engine/Applied/Render/Tests/TriangleRenderer.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Tests::TriangleRenderer
{

namespace
{

using MeshSnapshot   = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using ShaderSnapshot = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;

struct PushConstants
{
    glm::mat4 Projection;
};

::Engine::Util::Applied::ResourceHandle<Engine::Applied::Render::Resources::Shaders::Shader> m_Shader;

std::unique_ptr<::Engine::Util::Applied::ResourceHandle<
    ::Engine::Applied::Render::Resources::Meshes::Mesh>> m_Mesh;

std::shared_ptr<const MeshSnapshot>   m_FrameMesh;
std::shared_ptr<const ShaderSnapshot> m_FrameShader;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("triangle_test_renderer_init", Init, 40);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("triangle_test_renderer_cleanup", Cleanup, 80);

void CreatePipeline()
{
    Engine::Base::Render::GraphicsPipelineCreateInfo Info;

    Info.ColorFormats = {
        ::Engine::Base::Render::Vk::Frame::GetSwapchain().image_format
    };
    Info.CullMode = VK_CULL_MODE_NONE;
    Info.DepthWrite = false;
    Info.PushConstantSize = sizeof(PushConstants);
    Info.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    Info.VertexBindings = {
        { 0, sizeof(::Engine::Applied::Render::Resources::Meshes::Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
    };
    Info.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(::Engine::Applied::Render::Resources::Meshes::Vertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(::Engine::Applied::Render::Resources::Meshes::Vertex, Color) }
    };

    ::Engine::Applied::Render::Resources::Shaders::ShaderCreateInfo ShaderInfo;
    ShaderInfo.VertexFile = "Tests/TriangleTest.vert";
    ShaderInfo.FragmentFile = "Tests/TriangleTest.frag";
    ShaderInfo.Graphics = std::move(Info);
    m_Shader = ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader>(std::move(ShaderInfo));
}

void CreateMesh()
{
    using Mesh   = ::Engine::Applied::Render::Resources::Meshes::Mesh;
    using Vertex = ::Engine::Applied::Render::Resources::Meshes::Vertex;

    std::vector<Vertex> Vertices = {
        { { -1.0f, -1.0f, 0.0f }, 0.0f, {}, 0.0f, {}, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  1.0f, -1.0f, 0.0f }, 0.0f, {}, 0.0f, {}, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { {  0.0f,  1.0f, 0.0f }, 0.0f, {}, 0.0f, {}, { 0.0f, 0.0f, 1.0f, 1.0f } }
    };
    std::vector<uint32_t> Indices = { 0, 1, 2 };
    m_Mesh = std::make_unique<::Engine::Util::Applied::ResourceHandle<Mesh>>(
        ::Engine::Applied::Render::Resources::Meshes::MeshCreateInfo{
            std::move(Vertices), std::move(Indices)
        }
    );
}

void CaptureFrameSnapshot()
{
    m_FrameMesh.reset();
    m_FrameShader.reset();

    if (m_Shader.IsReady() && m_Mesh && m_Mesh->IsReady()) {
        m_FrameMesh = (*m_Mesh)->GetSnapshot();
        m_FrameShader = m_Shader->GetSnapshot();
    }
}

}

void Init()
{
    if constexpr (!::Engine::Configs::Static::Tests::TRIANGLE_RENDERER_ENABLED) {
        return;
    }

    CreatePipeline();
    CreateMesh();

    ::Engine::Base::Render::RenderingPool::Register("triangle_test", Render, 700);
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot(
        "triangle_test_snapshot", CaptureFrameSnapshot, 700
    );
}

void Cleanup()
{
    m_Shader = {};

    m_Mesh.reset();
}

void Render()
{
    const auto& Frame = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Image = ::Engine::Base::Render::Vk::Frame::GetCurrentImage();
    const auto& Swapchain = ::Engine::Base::Render::Vk::Frame::GetSwapchain();
    const auto& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    if (!m_FrameMesh || !m_FrameShader) {
        return;
    }

    const std::shared_ptr<const MeshSnapshot>& Mesh = m_FrameMesh;
    const std::shared_ptr<const ShaderSnapshot>& Shader = m_FrameShader;

    const ::Engine::Base::Render::Pipeline& Pipeline = Shader->Graphics;

    VkRenderingAttachmentInfo Attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    Attachment.imageView = Image.ImageView;
    Attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo RenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    RenderingInfo.renderArea.extent = Swapchain.extent;
    RenderingInfo.layerCount = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments = &Attachment;

    Disp.cmdBeginRendering(Frame.CommandBuffer, &RenderingInfo);

    const VkExtent2D Extent = Swapchain.extent;
    const PushConstants Constants{
        glm::ortho(-1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f)
    };

    VkViewport Viewport{};
    Viewport.width = static_cast<float>(Extent.width);
    Viewport.height = static_cast<float>(Extent.height);
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = Extent;

    Disp.cmdSetViewport(Frame.CommandBuffer, 0, 1, &Viewport);
    Disp.cmdSetScissor(Frame.CommandBuffer, 0, 1, &Scissor);
    Disp.cmdBindPipeline(Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Handle);

    const VkDeviceSize Offset = 0;
    Disp.cmdBindVertexBuffers(Frame.CommandBuffer, 0, 1, &Mesh->VertexBuffer.Handle, &Offset);
    Disp.cmdBindIndexBuffer(Frame.CommandBuffer, Mesh->IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);

    ::Engine::Base::Render::Vk::Commands::PushConstants(
        Frame.CommandBuffer,
        Pipeline.Layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(PushConstants),
        &Constants
    );

    Disp.cmdDrawIndexed(Frame.CommandBuffer, Mesh->IndexCount, 1, 0, 0, 0);
    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}

#endif
