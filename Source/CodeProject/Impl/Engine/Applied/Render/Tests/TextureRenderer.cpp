#include "X/Configs/PreCompiled/Dynamic.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

#include "X/Engine/Applied/Render/Tests/TextureRenderer.hpp"

#include "X/Configs/Static.hpp"
#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

namespace Engine::Applied::Render::Tests::TextureRenderer
{

namespace
{

struct Vertex
{
    glm::vec2 Position;
    glm::vec2 UV;
};

struct PushConstants
{
    glm::mat4 Projection;
};

Engine::Base::Render::Pipeline m_Pipeline;
Engine::Base::Render::Buffer m_VertexBuffer;
Engine::Base::Render::Buffer m_IndexBuffer;
std::unique_ptr<Engine::Applied::Render::Resources::Ref<
    Engine::Applied::Render::Resources::Textures::Texture>> m_DefaultEngineLogo;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("texture_test_renderer_init", Init, 30);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("texture_test_renderer_cleanup", Cleanup, 80);

void CreatePipeline()
{
    Engine::Base::Render::GraphicsPipelineCreateInfo Info;

    Info.ColorFormats = {
        ::Engine::Base::Render::Vk::Frame::GetSwapchain().image_format
    };
    Info.CullMode = VK_CULL_MODE_NONE;
    Info.DepthWrite = false;
    Info.SetLayouts = {
        ::Engine::Applied::Render::Resources::Textures::GetSetLayout()
    };
    Info.PushConstantSize = sizeof(PushConstants);
    Info.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
    Info.VertexBindings = {
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }
    };
    Info.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, Position) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, UV) }
    };

    m_Pipeline = ::Engine::Base::Render::Vk::Shader::CreateGraphicsPipeline(
        "Tests/TextureTest.vert",
        "Tests/TextureTest.frag",
        Info
    );
}

void CreateBuffers()
{
    constexpr std::array<Vertex, 4> Vertices = {{
        { { -256.0f, -256.0f }, { 0.0f, 1.0f } },
        { {  256.0f, -256.0f }, { 1.0f, 1.0f } },
        { {  256.0f,  256.0f }, { 1.0f, 0.0f } },
        { { -256.0f,  256.0f }, { 0.0f, 0.0f } }
    }};

    constexpr std::array<uint32_t, 6> Indices = { 0, 1, 2, 2, 3, 0 };

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

    ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(m_VertexBuffer, Vertices.data(), sizeof(Vertices));
    ::Engine::Base::Render::Vk::GpuResources::UploadBuffer(m_IndexBuffer, Indices.data(), sizeof(Indices));
}

}

void Init()
{
    if constexpr (!::Engine::Configs::Static::Tests::TEXTURE_RENDERER_ENABLED) {
        return;
    }

    CreatePipeline();
    CreateBuffers();
    m_DefaultEngineLogo = std::make_unique<Engine::Applied::Render::Resources::Ref<
        Engine::Applied::Render::Resources::Textures::Texture>>(
        "Resources/Images/DefaultEngineLogo.png"
    );

    ::Engine::Base::Render::RenderingPool::Register("texture_test", Render, 5);
}

void Cleanup()
{
    m_DefaultEngineLogo.reset();

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
    if (!m_DefaultEngineLogo || !m_DefaultEngineLogo->IsReady()) {
        return;
    }

    const auto& Texture = *m_DefaultEngineLogo->Get();

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

    const float Width = static_cast<float>(Swapchain.extent.width);
    const float Height = static_cast<float>(Swapchain.extent.height);
    const PushConstants Constants{
        glm::translate(
            glm::ortho(0.0f, Width, Height, 0.0f, -1.0f, 1.0f),
            { Width * 0.5f, Height * 0.5f, 0.0f }
        )
    };

    VkViewport Viewport{};
    Viewport.width = Width;
    Viewport.height = Height;
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = Swapchain.extent;

    Disp.cmdSetViewport(Frame.CommandBuffer, 0, 1, &Viewport);
    Disp.cmdSetScissor(Frame.CommandBuffer, 0, 1, &Scissor);
    Disp.cmdBindPipeline(Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.Handle);

    const VkDeviceSize Offset = 0;
    Disp.cmdBindVertexBuffers(Frame.CommandBuffer, 0, 1, &m_VertexBuffer.Handle, &Offset);
    Disp.cmdBindIndexBuffer(Frame.CommandBuffer, m_IndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
    const VkDescriptorSet DescriptorSet = Texture.GetDescriptorSet();
    Disp.cmdBindDescriptorSets(
        Frame.CommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_Pipeline.Layout,
        0,
        1,
        &DescriptorSet,
        0,
        nullptr
    );

    ::Engine::Base::Render::Vk::Commands::PushConstants(
        Frame.CommandBuffer,
        m_Pipeline.Layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        sizeof(PushConstants),
        &Constants
    );

    Disp.cmdDrawIndexed(Frame.CommandBuffer, 6, 1, 0, 0, 0);
    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}

#endif
