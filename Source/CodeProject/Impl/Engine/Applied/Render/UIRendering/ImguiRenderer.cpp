#include "X/Engine/Applied/Render/UIRendering/ImguiRenderer.hpp"

#include "X/Engine/Applied/Render/UIRendering/Resources/Fonts.hpp"
#include "X/Engine/Applied/Render/UIRendering/ImGuiScaler.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include "X/Engine/Base/Render/RenderManager.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/Viewport/Surface.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Engine::Applied::Render::UI::ImguiRenderer
{

namespace
{

VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
std::mutex m_FrameMutex;

void UploadFontTexture();

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("imgui_renderer_init", Init, 50);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterFontUpload("imgui_font_upload", UploadFontTexture, 40);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("imgui_renderer_cleanup", Cleanup, 70);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterBeginFrame(::Engine::Base::Runtime::EngineThreadID::GameTh, "imgui_begin_frame", BeginFrame, 50);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterEndFrame(::Engine::Base::Runtime::EngineThreadID::GameTh, "imgui_end_frame", EndFrame, 40);

void CheckVulkanResult(VkResult p_Result)
{
    X_ASSERT(p_Result != VK_SUCCESS, "ImGui Vulkan backend failed");
}

void UploadFontTexture()
{
    ImGuiIO& Io = ImGui::GetIO();

    if (Io.Fonts->TexData != nullptr) {
        ::Engine::Base::Render::Vk::Device::ExecuteQueueOperation(
            [&Io] {
                ImGui_ImplVulkan_UpdateTexture(Io.Fonts->TexData);
            }
        );
    }
}

std::vector<uint32_t> CompileVertexShader()
{
    return ::Engine::Base::Render::Vk::Shader::CompileGlsl(
        ::Engine::Util::File::ReadRaw(
            std::string(::Engine::Configs::Static::Contents::SHADERS_DIR) + "UI/ImGui.vert"
        ),
        ::Engine::Base::Render::ShaderStage::Vertex,
        "UI/ImGui.vert"
    );
}

}

void Init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForVulkan(::Engine::Base::Viewport::Surface::GetNative());

    ImGui_ImplVulkan_InitInfo Info = {};
    Info.ApiVersion = VK_API_VERSION_1_3;
    Info.Instance = ::Engine::Base::Render::It::GetInstance().instance;
    Info.PhysicalDevice = ::Engine::Base::Render::Vk::Device::GetPhysical().physical_device;
    Info.Device = ::Engine::Base::Render::Vk::Device::GetDevice().device;
    Info.QueueFamily = ::Engine::Base::Render::Vk::Device::GetQueues().GraphicsFamily;
    Info.Queue = ::Engine::Base::Render::Vk::Device::GetQueues().Graphics;
    Info.DescriptorPoolSize = 128;
    Info.MinImageCount = ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT;
    Info.ImageCount = ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT;
    Info.UseDynamicRendering = true;
    Info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    Info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    Info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats =
        &::Engine::Base::Render::Vk::Frame::GetSwapchain().image_format;
    Info.CheckVkResultFn = CheckVulkanResult;

    const std::vector<uint32_t> VertexShader = CompileVertexShader();

    Info.CustomShaderVertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    Info.CustomShaderVertCreateInfo.codeSize = VertexShader.size() * sizeof(uint32_t);
    Info.CustomShaderVertCreateInfo.pCode = VertexShader.data();

    X_ASSERT(!ImGui_ImplVulkan_Init(&Info), "failed to initialize ImGui Vulkan backend");

    ::Engine::Applied::Render::UI::ImGuiScaler::Init();

    ::Engine::Base::Render::RenderingPool::Register("imgui", Render, 1000);
}

void Cleanup()
{
    ::Engine::Applied::Render::UI::Resources::Fonts::Cleanup();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void BeginFrame()
{
    m_FrameMutex.lock();
    ::Engine::Applied::Render::UI::ImGuiScaler::Update();

    for (const SDL_Event& Event : ::Engine::Base::Platform::SDL3App::GetEvents()) {
        ImGui_ImplSDL3_ProcessEvent(&Event);
    }

    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void EndFrame()
{
    ImGui::Render();

    m_FrameMutex.unlock();
}

void Render()
{
    std::lock_guard<std::mutex> Lock(m_FrameMutex);

    ImDrawData* DrawData = ImGui::GetDrawData();

    if (DrawData == nullptr || DrawData->TotalVtxCount == 0) {
        return;
    }

    const auto& Frame = ::Engine::Base::Render::Vk::Frame::GetCurrentFrame();
    const auto& Image = ::Engine::Base::Render::Vk::Frame::GetCurrentImage();
    const auto& Disp = ::Engine::Base::Render::Vk::Device::GetDisp();
    const auto& Swapchain = ::Engine::Base::Render::Vk::Frame::GetSwapchain();

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

    ImGui_ImplVulkan_RenderDrawData(DrawData, Frame.CommandBuffer);

    Disp.cmdEndRendering(Frame.CommandBuffer);
}

}
