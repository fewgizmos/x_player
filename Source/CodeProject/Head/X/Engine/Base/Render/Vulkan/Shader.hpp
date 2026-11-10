#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Base::Render
{

enum class ShaderStage
{
    Vertex,
    Fragment,
    Compute
};

struct GraphicsPipelineCreateInfo
{
    std::vector<VkFormat> ColorFormats;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;

    VkCompareOp DepthCompare = VK_COMPARE_OP_GREATER;

    VkCullModeFlags CullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace FrontFace    = VK_FRONT_FACE_CLOCKWISE;

    bool DepthWrite = true;
    bool DepthClamp = false;
    bool AlphaBlend = false;

    float DepthBiasConstant = 0.0f;
    float DepthBiasSlope    = 0.0f;

    std::vector<VkDescriptorSetLayout> SetLayouts;

    uint32_t PushConstantSize = 0;
    VkShaderStageFlags PushConstantStages = 0;

    std::vector<VkVertexInputBindingDescription> VertexBindings;
    std::vector<VkVertexInputAttributeDescription> VertexAttributes;

    std::vector<std::string> Defines;
};

struct ComputePipelineCreateInfo
{
    std::vector<VkDescriptorSetLayout> SetLayouts;

    uint32_t PushConstantSize = 0;

    std::vector<std::string> Defines;
};

struct Pipeline
{
    VkPipeline Handle = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
};

}

namespace Engine::Base::Render::Vk::Shader
{

std::vector<uint32_t> CompileGlsl(const std::string& p_Source, ShaderStage p_Stage, const std::string& p_Name,
    const std::vector<std::string>& p_Defines = {}, bool p_Optimize = true);

VkShaderModule CreateModule(const std::vector<uint32_t>& p_Spirv);
void DestroyModule(VkShaderModule p_Module);

VkShaderModule LoadModule(const std::string& p_FileName);

Pipeline CreateGraphicsPipeline(
    const std::string& p_VertFile,
    const std::string& p_FragFile,
    const GraphicsPipelineCreateInfo& p_CreateInfo
);

Pipeline CreateComputePipeline(const std::string& p_CompFile, const ComputePipelineCreateInfo& p_CreateInfo);

Pipeline CreateGraphicsPipeline(const std::vector<uint32_t>& p_Vertex, const std::vector<uint32_t>& p_Fragment,
    const GraphicsPipelineCreateInfo& p_CreateInfo);
Pipeline CreateComputePipeline(const std::vector<uint32_t>& p_Compute, const ComputePipelineCreateInfo& p_CreateInfo);

void DestroyPipeline(const Pipeline& p_Target);

}
