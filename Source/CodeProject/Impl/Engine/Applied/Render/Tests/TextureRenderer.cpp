#include "X/Engine/Applied/Render/Tests/TextureRenderer.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Applied/Render/Resources/Shaders.hpp"
#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Applied/Render/Resources/Meshes.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/Vulkan/Commands.hpp"
#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Tests::TextureRenderer
{

namespace
{

using Mesh             = ::Engine::Applied::Render::Resources::Meshes::Mesh;
using Vertex           = ::Engine::Applied::Render::Resources::Meshes::Vertex;
using Texture          = ::Engine::Applied::Render::Resources::Textures::Texture;

using MeshSnapshot     = ::Engine::Applied::Render::Resources::Meshes::MeshSnapshot;
using ShaderSnapshot   = ::Engine::Applied::Render::Resources::Shaders::ShaderSnapshot;
using TextureSnapshot  = ::Engine::Applied::Render::Resources::Textures::TextureSnapshot;

using MeshReference    = ::Engine::Util::Applied::ResourceHandle<Mesh>;
using TextureReference = ::Engine::Util::Applied::ResourceHandle<Texture>;

struct PushConstants
{
    glm::mat4 Projection;
    uint32_t TextureIndex;
    uint32_t SamplerIndex;
};

Engine::Util::Applied::ResourceHandle<
    Engine::Applied::Render::Resources::Shaders::Shader> m_Shader;

std::unique_ptr<MeshReference> m_Mesh;

std::unique_ptr<TextureReference>      m_DefaultEngineLogo;
std::shared_ptr<const MeshSnapshot>    m_FrameMesh;
std::shared_ptr<const TextureSnapshot> m_FrameTexture;
std::shared_ptr<const ShaderSnapshot>  m_FrameShader;

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
        ::Engine::Applied::Render::Resources::Textures::GetBindlessSetLayout()
    };
    Info.PushConstantSize = sizeof(PushConstants);
    Info.PushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    Info.VertexBindings = {
        {
            0,
            sizeof(Vertex),
            VK_VERTEX_INPUT_RATE_VERTEX
        }
    };
    Info.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Position) },
        { 1, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvX) },
        { 2, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, UvY) }
    };

    ::Engine::Applied::Render::Resources::Shaders::ShaderCreateInfo ShaderInfo;
    ShaderInfo.VertexFile = "Tests/TextureTest.vert";
    ShaderInfo.FragmentFile = "Tests/TextureTest.frag";
    ShaderInfo.Graphics = std::move(Info);
    m_Shader = ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Shaders::Shader>(std::move(ShaderInfo));
}

void CreateMesh()
{
    std::vector<Vertex> Vertices = {
        { { -256.0f, -256.0f, 0.0f }, 0.0f, {}, 1.0f, {}, {} },
        { {  256.0f, -256.0f, 0.0f }, 1.0f, {}, 1.0f, {}, {} },
        { {  256.0f,  256.0f, 0.0f }, 1.0f, {}, 0.0f, {}, {} },
        { { -256.0f,  256.0f, 0.0f }, 0.0f, {}, 0.0f, {}, {} }
    };

    std::vector<uint32_t> Indices = { 0, 1, 2, 2, 3, 0 };

    m_Mesh = std::make_unique<MeshReference>(
        ::Engine::Applied::Render::Resources::Meshes::MeshCreateInfo{
            std::move(Vertices), std::move(Indices)
        }
    );
}

void CaptureFrameSnapshot()
{
    m_FrameMesh.reset();
    m_FrameTexture.reset();
    m_FrameShader.reset();

    if (m_Shader.IsReady()
        && m_Mesh
        && m_Mesh->IsReady()
        && m_DefaultEngineLogo
        && m_DefaultEngineLogo->IsReady()) {
        m_FrameMesh = (*m_Mesh)->GetSnapshot();
        m_FrameTexture = (*m_DefaultEngineLogo)->GetSnapshot();
        m_FrameShader = m_Shader->GetSnapshot();
    }
}

}

void Init()
{
    if constexpr (!::Engine::Configs::Static::Tests::TEXTURE_RENDERER_ENABLED) {

        return;
    }

    CreatePipeline();
    CreateMesh();
    m_DefaultEngineLogo = std::make_unique<Engine::Util::Applied::ResourceHandle<
        Engine::Applied::Render::Resources::Textures::Texture>>(
        Engine::Applied::Render::Resources::Textures::TextureCreateInfo{
            "Resources/Images/DefaultEngineLogo.png",
            VK_FORMAT_R8G8B8A8_SRGB
        }
    );

    ::Engine::Base::Render::RenderingPool::Register("texture_test", Render, 800);
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot(
        "texture_test_snapshot", CaptureFrameSnapshot, 800
    );
}

void Cleanup()
{
    m_DefaultEngineLogo.reset();

    m_Shader = {};
    m_Mesh.reset();
}

void Render()
{
    const std::shared_ptr<const MeshSnapshot>& MeshValue = m_FrameMesh;
    const std::shared_ptr<const TextureSnapshot>& TextureValue = m_FrameTexture;
    const std::shared_ptr<const ShaderSnapshot>& ShaderValue = m_FrameShader;
    if (!MeshValue || !TextureValue || !ShaderValue) {
        return;
    }

    const Engine::Base::Render::Pipeline& Pipeline = ShaderValue->Graphics;

    const ::Engine::Base::Render::FrameData& Frame =
        ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const ::Engine::Base::Render::SwapchainImageData& Image =
        ::Engine::Base::Render::Vk::Frame::GetCurrentImage();
    const vkb::Swapchain& Swapchain = ::Engine::Base::Render::Vk::Frame::GetSwapchain();
    const vkb::DispatchTable& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();

    VkRenderingAttachmentInfo Attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    Attachment.imageView = Image.ImageView;
    Attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    Attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo RenderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
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
        ),
        TextureValue->Index,
        0
    };

    VkViewport Viewport{};
    Viewport.width = Width;
    Viewport.height = Height;
    Viewport.maxDepth = 1.0f;

    VkRect2D Scissor{};
    Scissor.extent = Swapchain.extent;

    Disp.cmdSetViewport(Frame.CommandBuffer, 0, 1, &Viewport);
    Disp.cmdSetScissor(Frame.CommandBuffer, 0, 1, &Scissor);
    Disp.cmdBindPipeline(Frame.CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Handle);

    const VkDeviceSize Offset = 0;

    Disp.cmdBindVertexBuffers(
        Frame.CommandBuffer,
        0,
        1,
        &MeshValue->VertexBuffer.Handle,
        &Offset
    );

    Disp.cmdBindIndexBuffer(
        Frame.CommandBuffer,
        MeshValue->IndexBuffer.Handle,
        0,
        VK_INDEX_TYPE_UINT32
    );

    const VkDescriptorSet DescriptorSet =
        ::Engine::Applied::Render::Resources::Textures::GetBindlessSet();

    Disp.cmdBindDescriptorSets(
        Frame.CommandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Pipeline.Layout,
        0,
        1,
        &DescriptorSet,
        0,
        nullptr
    );

    ::Engine::Base::Render::Vk::Commands::PushConstants(
        Frame.CommandBuffer,
        Pipeline.Layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        sizeof(PushConstants),
        &Constants
    );

    Disp.cmdDrawIndexed(
        Frame.CommandBuffer,
        MeshValue->IndexCount,
        1,
        0,
        0,
        0
    );
    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}

#endif
