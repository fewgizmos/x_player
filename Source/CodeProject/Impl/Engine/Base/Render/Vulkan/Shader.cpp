#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Configs/Static.hpp"

#include "X/Engine/Base/Render/Vulkan/Device.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include <shaderc/shaderc.hpp>
#include <vkb/VkBootstrap.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

namespace Engine::Base::Render::Vk::Shader
{

namespace
{

constexpr const char* FRAGMENT_STAGE_DEFINE = "X_FRAGMENT_STAGE";

shaderc_shader_kind GetShadercKind(ShaderStage p_Stage)
{
    switch (p_Stage) {
        case ShaderStage::Vertex:   return shaderc_glsl_vertex_shader;
        case ShaderStage::Fragment: return shaderc_glsl_fragment_shader;
        case ShaderStage::Compute:  return shaderc_glsl_compute_shader;
    }

    return shaderc_glsl_infer_from_source;
}

ShaderStage GetStageFromFile(const std::string& p_FileName)
{
    const std::string Extension = std::filesystem::path(p_FileName).extension().string();

    if (Extension == ".vert") {
        return ShaderStage::Vertex;
    }

    if (Extension == ".frag") {
        return ShaderStage::Fragment;
    }

    if (Extension == ".comp") {
        return ShaderStage::Compute;
    }

    X_ASSERT(true, "unknown shader extension: " + p_FileName);

    return ShaderStage::Vertex;
}

std::string ResolveInclude(const std::string& p_RequestedSource, const std::string& p_RequestingSource)
{
    std::filesystem::path Directory = std::filesystem::path(p_RequestingSource).parent_path();

    while (true) {
        const std::filesystem::path Candidate = (Directory / p_RequestedSource).lexically_normal();

        if (std::filesystem::exists(std::filesystem::path(Configs::Static::Graphics::SHADERS_DIR) / Candidate)) {
            return Candidate.generic_string();
        }

        if (Directory.empty()) {
            return p_RequestedSource;
        }

        Directory = Directory.parent_path();
    }
}

struct IncludeData
{
    shaderc_include_result Result = {};

    std::string Name;
    std::string Content;
};

class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface
{
public:
    shaderc_include_result* GetInclude(
        const char* p_RequestedSource,
        shaderc_include_type p_Type,
        const char* p_RequestingSource,
        std::size_t p_IncludeDepth
    ) override
    {
        std::unique_ptr<IncludeData> Data = std::make_unique<IncludeData>();

        try {
            Data->Name    = ResolveInclude(p_RequestedSource, p_RequestingSource);
            Data->Content = Util::File::ReadRaw(std::string(Configs::Static::Graphics::SHADERS_DIR) + Data->Name);
        }

        catch (const std::exception& Error) {
            Data->Content = Error.what();
        }

        Data->Result.source_name        = Data->Name.c_str();
        Data->Result.source_name_length = Data->Name.size();
        Data->Result.content            = Data->Content.c_str();
        Data->Result.content_length     = Data->Content.size();
        Data->Result.user_data          = Data.get();

        shaderc_include_result* Result = &Data->Result;

        m_Includes.push_back(std::move(Data));

        return Result;
    }

    void ReleaseInclude(shaderc_include_result* p_Result) override
    {
        std::erase_if(m_Includes, [p_Result](const std::unique_ptr<IncludeData>& Stored) {
            return Stored.get() == p_Result->user_data;
        });
    }

private:
    std::vector<std::unique_ptr<IncludeData>> m_Includes;
};

}

std::vector<uint32_t> CompileGlsl(const std::string& p_Source, ShaderStage p_Stage, const std::string& p_Name, const std::vector<std::string>& p_Defines)
{
    shaderc::Compiler Compiler;
    shaderc::CompileOptions Options;

    for (const std::string& Define : p_Defines) {
        const std::size_t Separator = Define.find('=');

        if (Separator == std::string::npos) {
            Options.AddMacroDefinition(Define);
        } else {
            Options.AddMacroDefinition(Define.substr(0, Separator), Define.substr(Separator + 1));
        }
    }

    if (p_Stage == ShaderStage::Fragment) {
        Options.AddMacroDefinition(FRAGMENT_STAGE_DEFINE);
    }

    Options.SetSourceLanguage(shaderc_source_language_glsl);
    Options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    Options.SetTargetSpirv(shaderc_spirv_version_1_6);
    Options.SetIncluder(std::make_unique<ShaderIncluder>());

#ifdef NDEBUG
    Options.SetOptimizationLevel(shaderc_optimization_level_performance);
#else
    Options.SetOptimizationLevel(shaderc_optimization_level_zero);
    Options.SetGenerateDebugInfo();
#endif

    const shaderc::SpvCompilationResult Result = Compiler.CompileGlslToSpv(
        p_Source,
        GetShadercKind(p_Stage),
        p_Name.c_str(),
        Options
    );

    X_ASSERT(
        Result.GetCompilationStatus() != shaderc_compilation_status_success,
        "failed to compile shader " + p_Name + "\n" + Result.GetErrorMessage()
    );

    return { Result.cbegin(), Result.cend() };
}

VkShaderModule CreateModule(const std::vector<uint32_t>& p_Spirv)
{
    VkShaderModuleCreateInfo CreateInfo = {};

    CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    CreateInfo.codeSize = p_Spirv.size() * sizeof(uint32_t);
    CreateInfo.pCode = p_Spirv.data();

    VkShaderModule ShaderModule = VK_NULL_HANDLE;

    const VkResult Result = Device::GetDisp().createShaderModule(&CreateInfo, nullptr, &ShaderModule);

    X_ASSERT(Result != VK_SUCCESS, "failed to create shader module");

    return ShaderModule;
}

void DestroyModule(VkShaderModule p_Module)
{
    Device::GetDisp().destroyShaderModule(p_Module, nullptr);
}

VkShaderModule LoadModule(const std::string& p_FileName)
{
    const std::string Source = Util::File::ReadRaw(std::string(Configs::Static::Graphics::SHADERS_DIR) + p_FileName);

    return CreateModule(CompileGlsl(Source, GetStageFromFile(p_FileName), p_FileName));
}

Pipeline CreateGraphicsPipeline(
    const std::string& p_VertFile,
    const std::string& p_FragFile,
    const GraphicsPipelineCreateInfo& p_CreateInfo
)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    const uint32_t ColorCount = static_cast<uint32_t>(p_CreateInfo.ColorFormats.size());

    const std::vector<uint32_t> VertSpirv = CompileGlsl(
        Util::File::ReadRaw(std::string(Configs::Static::Graphics::SHADERS_DIR) + p_VertFile),
        ShaderStage::Vertex,
        p_VertFile,
        p_CreateInfo.Defines
    );

    const std::vector<uint32_t> FragSpirv = p_FragFile.empty() ? std::vector<uint32_t>() : CompileGlsl(
        Util::File::ReadRaw(std::string(Configs::Static::Graphics::SHADERS_DIR) + p_FragFile),
        ShaderStage::Fragment,
        p_FragFile,
        p_CreateInfo.Defines
    );

    VkShaderModule VertModule = CreateModule(VertSpirv);
    VkShaderModule FragModule = p_FragFile.empty() ? VK_NULL_HANDLE : CreateModule(FragSpirv);

    VkPipelineShaderStageCreateInfo VertStageCreateInfo = {};

    VertStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    VertStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    VertStageCreateInfo.module = VertModule;
    VertStageCreateInfo.pName = "main";

    std::vector<VkPipelineShaderStageCreateInfo> ShaderStages = { VertStageCreateInfo };

    if (FragModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo FragStageCreateInfo = {};

        FragStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        FragStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        FragStageCreateInfo.module = FragModule;
        FragStageCreateInfo.pName = "main";

        ShaderStages.push_back(FragStageCreateInfo);
    }

    VkPipelineVertexInputStateCreateInfo VertexInputCreateInfo = {};

    VertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VertexInputCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(p_CreateInfo.VertexBindings.size());
    VertexInputCreateInfo.pVertexBindingDescriptions = p_CreateInfo.VertexBindings.data();
    VertexInputCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(p_CreateInfo.VertexAttributes.size());
    VertexInputCreateInfo.pVertexAttributeDescriptions = p_CreateInfo.VertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = {};

    InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    InputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo ViewportState = {};

    ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportState.viewportCount = 1;
    ViewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo Rasterizer = {};

    Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Rasterizer.depthClampEnable = p_CreateInfo.DepthClamp ? VK_TRUE : VK_FALSE;
    Rasterizer.rasterizerDiscardEnable = VK_FALSE;
    Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterizer.lineWidth = 1.0f;
    Rasterizer.cullMode = p_CreateInfo.CullMode;
    Rasterizer.frontFace = p_CreateInfo.FrontFace;
    Rasterizer.depthBiasEnable = p_CreateInfo.DepthBiasConstant != 0.0f || p_CreateInfo.DepthBiasSlope != 0.0f ? VK_TRUE : VK_FALSE;
    Rasterizer.depthBiasConstantFactor = p_CreateInfo.DepthBiasConstant;
    Rasterizer.depthBiasSlopeFactor = p_CreateInfo.DepthBiasSlope;

    VkPipelineMultisampleStateCreateInfo Multisampling = {};

    Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisampling.sampleShadingEnable = VK_FALSE;
    Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DepthStencil = {};

    DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencil.depthTestEnable = VK_TRUE;
    DepthStencil.depthWriteEnable = p_CreateInfo.DepthWrite ? VK_TRUE : VK_FALSE;
    DepthStencil.depthCompareOp = p_CreateInfo.DepthCompare;

    VkPipelineColorBlendAttachmentState ColorBlendAttachment = {};

    ColorBlendAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    ColorBlendAttachment.blendEnable = p_CreateInfo.AlphaBlend ? VK_TRUE : VK_FALSE;
    ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    ColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo ColorBlending = {};

    ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlending.logicOpEnable = VK_FALSE;
    ColorBlending.logicOp = VK_LOGIC_OP_COPY;

    const std::vector<VkPipelineColorBlendAttachmentState> ColorBlendAttachments(ColorCount, ColorBlendAttachment);

    ColorBlending.attachmentCount = ColorCount;
    ColorBlending.pAttachments = ColorCount > 0 ? ColorBlendAttachments.data() : nullptr;

    VkPushConstantRange PushRange = {};

    PushRange.stageFlags = p_CreateInfo.PushConstantStages;
    PushRange.offset = 0;
    PushRange.size = p_CreateInfo.PushConstantSize;

    VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo = {};

    PipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(p_CreateInfo.SetLayouts.size());
    PipelineLayoutCreateInfo.pSetLayouts = p_CreateInfo.SetLayouts.data();
    PipelineLayoutCreateInfo.pushConstantRangeCount = p_CreateInfo.PushConstantSize > 0 ? 1 : 0;
    PipelineLayoutCreateInfo.pPushConstantRanges = &PushRange;

    Pipeline ResultPipeline;

    VkResult Result = Disp.createPipelineLayout(&PipelineLayoutCreateInfo, nullptr, &ResultPipeline.Layout);

    X_ASSERT(Result != VK_SUCCESS, "failed to create Pipeline layout");

    const VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo DynamicCreateInfo = {};

    DynamicCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicCreateInfo.dynamicStateCount = 2;
    DynamicCreateInfo.pDynamicStates = DynamicStates;

    VkPipelineRenderingCreateInfo RenderingCreateInfo = {};

    RenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    RenderingCreateInfo.colorAttachmentCount = ColorCount;
    RenderingCreateInfo.pColorAttachmentFormats = ColorCount > 0 ? p_CreateInfo.ColorFormats.data() : nullptr;
    RenderingCreateInfo.depthAttachmentFormat = p_CreateInfo.DepthFormat;
    RenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo PipelineCreateInfo = {};

    PipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipelineCreateInfo.pNext = &RenderingCreateInfo;
    PipelineCreateInfo.stageCount = static_cast<uint32_t>(ShaderStages.size());
    PipelineCreateInfo.pStages = ShaderStages.data();
    PipelineCreateInfo.pVertexInputState = &VertexInputCreateInfo;
    PipelineCreateInfo.pInputAssemblyState = &InputAssembly;
    PipelineCreateInfo.pViewportState = &ViewportState;
    PipelineCreateInfo.pRasterizationState = &Rasterizer;
    PipelineCreateInfo.pMultisampleState = &Multisampling;
    PipelineCreateInfo.pDepthStencilState = p_CreateInfo.DepthFormat != VK_FORMAT_UNDEFINED ? &DepthStencil : nullptr;
    PipelineCreateInfo.pColorBlendState = &ColorBlending;
    PipelineCreateInfo.pDynamicState = &DynamicCreateInfo;
    PipelineCreateInfo.layout = ResultPipeline.Layout;
    PipelineCreateInfo.renderPass = VK_NULL_HANDLE;
    PipelineCreateInfo.subpass = 0;
    PipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

    Result = Disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &PipelineCreateInfo, nullptr, &ResultPipeline.Handle);

    if (FragModule != VK_NULL_HANDLE) {
        DestroyModule(FragModule);
    }

    DestroyModule(VertModule);

    X_ASSERT(Result != VK_SUCCESS, "failed to create graphics Pipeline");

    return ResultPipeline;
}

Pipeline CreateComputePipeline(const std::string& p_CompFile, const ComputePipelineCreateInfo& p_CreateInfo)
{
    const vkb::DispatchTable& Disp = Device::GetDisp();

    const std::string CompSource = Util::File::ReadRaw(std::string(Configs::Static::Graphics::SHADERS_DIR) + p_CompFile);

    VkShaderModule CompModule = CreateModule(CompileGlsl(CompSource, ShaderStage::Compute, p_CompFile, p_CreateInfo.Defines));

    VkPushConstantRange PushRange = {};

    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset = 0;
    PushRange.size = p_CreateInfo.PushConstantSize;

    VkPipelineLayoutCreateInfo LayoutCreateInfo = {};

    LayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(p_CreateInfo.SetLayouts.size());
    LayoutCreateInfo.pSetLayouts = p_CreateInfo.SetLayouts.data();
    LayoutCreateInfo.pushConstantRangeCount = p_CreateInfo.PushConstantSize > 0 ? 1 : 0;
    LayoutCreateInfo.pPushConstantRanges = &PushRange;

    Pipeline ResultPipeline;

    VkResult Result = Disp.createPipelineLayout(&LayoutCreateInfo, nullptr, &ResultPipeline.Layout);

    X_ASSERT(Result != VK_SUCCESS, "failed to create compute Pipeline layout");

    VkPipelineShaderStageCreateInfo StageCreateInfo = {};

    StageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    StageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    StageCreateInfo.module = CompModule;
    StageCreateInfo.pName = "main";

    VkComputePipelineCreateInfo PipelineCreateInfo = {};

    PipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipelineCreateInfo.stage = StageCreateInfo;
    PipelineCreateInfo.layout = ResultPipeline.Layout;

    Result = Disp.createComputePipelines(VK_NULL_HANDLE, 1, &PipelineCreateInfo, nullptr, &ResultPipeline.Handle);

    DestroyModule(CompModule);

    X_ASSERT(Result != VK_SUCCESS, "failed to create compute Pipeline");

    return ResultPipeline;
}

void DestroyPipeline(const Pipeline& p_Target)
{
    Device::GetDisp().destroyPipeline(p_Target.Handle, nullptr);
    Device::GetDisp().destroyPipelineLayout(p_Target.Layout, nullptr);
}

}
