#pragma once

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Engine::Applied::Render::Resources::Shaders
{

struct ShaderSnapshot
{
    ::Engine::Base::Render::Pipeline Graphics;
    ::Engine::Base::Render::Pipeline Compute;

    uint64_t VersionChecksum = 0;
};

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

class Shader final : public ::Engine::Util::Applied::Resource
{
public:
    using CreateInfo = ShaderCreateInfo;

    explicit Shader(ShaderCreateInfo p_CreateInfo);
    ~Shader() override;

    void BeginLoad() override;
    void Update() override;
    void Unload() override;

    std::string GetTypeName() const override;

    const ::Engine::Base::Render::Pipeline& GetGraphicsPipeline() const;
    const ::Engine::Base::Render::Pipeline& GetComputePipeline() const;
    RenderQueue GetRenderQueue() const;
    std::shared_ptr<const ShaderSnapshot> GetSnapshot() const;

private:
    struct LoadData;

    std::shared_ptr<LoadData> LoadState;
    ShaderCreateInfo m_CreateInfo;

    ::Engine::Base::Render::Pipeline Graphics;
    ::Engine::Base::Render::Pipeline Compute;

    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> GpuTicket;
    std::atomic<std::shared_ptr<const ShaderSnapshot>> Snapshot;
};

}
