#include "X/Configs/PreCompiled/Dynamic.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

#include "X/Engine/Applied/Render/Tests/TriangleRenderer.hpp"

#include "X/Configs/Static.hpp"
#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine::Applied::Render::Tests::TriangleRenderer
{

namespace
{

struct Vertex
{
    glm::vec2 Position;
    glm::vec3 Color;
};

struct PushConstants
{
    glm::mat4 Projection;
};

Engine::Base::Render::Pipeline m_Pipeline;
Engine::Base::Render::Buffer m_VertexBuffer;
Engine::Base::Render::Buffer m_IndexBuffer;

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
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
    };
    Info.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Color) }
    };

    m_Pipeline = ::Engine::Base::Render::Vk::Shader::CreateGraphicsPipeline(
        "Tests/TriangleTest.vert",
        "Tests/TriangleTest.frag",
        Info
    );
}

void CreateBuffers()
{
    constexpr std::array<Vertex, 3> Vertices = {{
        { { -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
        { {  1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        { {  0.0f,  1.0f }, { 0.0f, 0.0f, 1.0f } }
    }};

    constexpr std::array<uint32_t, 3> Indices = { 0, 1, 2 };

    m_VertexBuffer = ::Engine::Base::Render::Vk::GpuResources::CreateBuffer(
        sizeof(Vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    m_IndexBuffer = ::Engine::Base::Render::Vk::GpuResources::CreateBuffer(
        sizeof(Indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(
        m_VertexBuffer,
        Vertices.data(),
        sizeof(Vertices)
    );
    ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(
        m_IndexBuffer,
        Indices.data(),
        sizeof(Indices)
    );
}

}

void Init()
{
    if constexpr (!::Engine::Configs::Static::Tests::TRIANGLE_RENDERER_ENABLED) {
        return;
    }

    CreatePipeline();
    CreateBuffers();

    ::Engine::Base::Render::RenderingPool::Register("triangle_test", Render, 10);
}

void Cleanup()
{
    if (m_Pipeline.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::Shader::DestroyPipeline(m_Pipeline);
        m_Pipeline = {};
    }

    if (m_VertexBuffer.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_VertexBuffer);
    }

    if (m_IndexBuffer.Handle != VK_NULL_HANDLE) {
        ::Engine::Base::Render::Vk::GpuResources::DestroyBuffer(m_IndexBuffer);
    }
}

void Render()
{
    const auto& Frame = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Image = ::Engine::Base::Render::Vk::Frame::GetCurrentImage();
    const auto& Swapchain = ::Engine::Base::Render::Vk::Frame::GetSwapchain();
    const auto& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

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
    Disp.cmdBindPipeline(Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.Handle);

    const VkDeviceSize Offset = 0;
    Disp.cmdBindVertexBuffers(Frame.CommandBuffer, 0, 1, &m_VertexBuffer.Handle, &Offset);
    Disp.cmdBindIndexBuffer(Frame.CommandBuffer, m_IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);

    ::Engine::Base::Render::Vk::Commands::PushConstants(
        Frame.CommandBuffer,
        m_Pipeline.Layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(PushConstants),
        &Constants
    );

    Disp.cmdDrawIndexed(Frame.CommandBuffer, 3, 1, 0, 0, 0);
    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}

#endif
