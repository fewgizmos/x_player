#pragma once

#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <memory>
#include <string>

namespace Engine::Applied::Render::Resources::Shaders
{

enum class RenderQueue
{
    Opaque,
    Skybox,
    Blended
};

struct ShaderCreateInfo
{
    std::string VertexFile;
    std::string FragmentFile;
    std::string ComputeFile;

    ::Engine::Base::Render::GraphicsPipelineCreateInfo Graphics;
    ::Engine::Base::Render::ComputePipelineCreateInfo Compute;

    RenderQueue Queue = RenderQueue::Opaque;
};

class Shader final : public Resource
{
public:
    explicit Shader(ShaderCreateInfo p_CreateInfo);
    ~Shader() override;

    std::string GetTypeName() const override;
    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    const ::Engine::Base::Render::Pipeline& GetGraphicsPipeline() const;
    const ::Engine::Base::Render::Pipeline& GetComputePipeline() const;
    RenderQueue GetRenderQueue() const;

private:
    struct LoadData;
    std::shared_ptr<LoadData> LoadState;
    ShaderCreateInfo CreateInfo;

    ::Engine::Base::Render::Pipeline Graphics;
    ::Engine::Base::Render::Pipeline Compute;
};

}
