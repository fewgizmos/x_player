#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include "X/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include <cstdint>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Resources::Shaders
{

struct Shader::LoadData
{
    std::mutex Mutex;
    std::vector<uint32_t> Vertex;
    std::vector<uint32_t> Fragment;
    std::vector<uint32_t> Compute;
    std::string FailureReason;
    bool Finished = false;
};

namespace
{

template<class T>
void Append(std::string& p_Key, const T& p_Value)
{
    p_Key.append(reinterpret_cast<const char*>(&p_Value), sizeof(T));
}

void Append(std::string& p_Key, const std::string& p_Value)
{
    Append(p_Key, p_Value.size());
    p_Key += p_Value;
}

std::string MakeKey(const ShaderCreateInfo& p_Info)
{
    std::string Key;
    Append(Key, p_Info.VertexFile);
    Append(Key, p_Info.FragmentFile);
    Append(Key, p_Info.ComputeFile);
    Append(Key, p_Info.Queue);

    const auto& Graphics = p_Info.Graphics;
    Append(Key, Graphics.ColorFormats.size());
    for (const auto Format : Graphics.ColorFormats) { Append(Key, Format); }
    Append(Key, Graphics.DepthFormat);
    Append(Key, Graphics.DepthCompare);
    Append(Key, Graphics.CullMode);
    Append(Key, Graphics.FrontFace);
    Append(Key, Graphics.DepthWrite);
    Append(Key, Graphics.DepthClamp);
    Append(Key, Graphics.AlphaBlend);
    Append(Key, Graphics.DepthBiasConstant);
    Append(Key, Graphics.DepthBiasSlope);
    Append(Key, Graphics.SetLayouts.size());
    for (const auto Layout : Graphics.SetLayouts) { Append(Key, Layout); }
    Append(Key, Graphics.PushConstantSize);
    Append(Key, Graphics.PushConstantStages);
    Append(Key, Graphics.VertexBindings.size());
    for (const auto& Binding : Graphics.VertexBindings) {
        Append(Key, Binding.binding);
        Append(Key, Binding.stride);
        Append(Key, Binding.inputRate);
    }
    Append(Key, Graphics.VertexAttributes.size());
    for (const auto& Attribute : Graphics.VertexAttributes) {
        Append(Key, Attribute.location);
        Append(Key, Attribute.binding);
        Append(Key, Attribute.format);
        Append(Key, Attribute.offset);
    }
    Append(Key, Graphics.Defines.size());
    for (const auto& Define : Graphics.Defines) { Append(Key, Define); }
    Append(Key, p_Info.Compute.SetLayouts.size());
    for (const auto Layout : p_Info.Compute.SetLayouts) { Append(Key, Layout); }
    Append(Key, p_Info.Compute.PushConstantSize);
    Append(Key, p_Info.Compute.Defines.size());
    for (const auto& Define : p_Info.Compute.Defines) { Append(Key, Define); }
    std::string Encoded;
    constexpr char Digits[] = "0123456789abcdef";
    Encoded.reserve(Key.size() * 2);
    for (const unsigned char Value : Key) {
        Encoded += Digits[Value >> 4];
        Encoded += Digits[Value & 15];
    }
    return Encoded;
}

std::vector<uint32_t> Compile(const std::string& p_File, ::Engine::Base::Render::ShaderStage p_Stage,
    const std::vector<std::string>& p_Defines)
{
    if (p_File.empty()) {
        return {};
    }

    return ::Engine::Base::Render::Vk::Shader::CompileGlsl(
        ::Engine::Util::File::ReadRaw(std::string(::Engine::Configs::Static::Graphics::SHADERS_DIR) + p_File),
        p_Stage, p_File, p_Defines
    );
}

}

Shader::Shader(ShaderCreateInfo p_CreateInfo)
    : Resource(MakeKey(p_CreateInfo))
    , CreateInfo(std::move(p_CreateInfo))
    , LoadState(std::make_shared<LoadData>())
{
}

Shader::~Shader() = default;

std::string Shader::GetTypeName() const { return "Shader"; }

void Shader::BeginLoad()
{
    const auto State = LoadState;
    const auto Info = CreateInfo;

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineCore::ResourceLoaderTh, "shader_load",
        [State, Info] {
            try {
                X_ASSERT(Info.VertexFile.empty() && Info.ComputeFile.empty(), "Shader requires vertex or compute stage");
                X_ASSERT(Info.VertexFile.empty() && !Info.FragmentFile.empty(), "Fragment stage requires vertex stage");
                State->Vertex = Compile(Info.VertexFile, ::Engine::Base::Render::ShaderStage::Vertex, Info.Graphics.Defines);
                State->Fragment = Compile(Info.FragmentFile, ::Engine::Base::Render::ShaderStage::Fragment, Info.Graphics.Defines);
                State->Compute = Compile(Info.ComputeFile, ::Engine::Base::Render::ShaderStage::Compute, Info.Compute.Defines);
            }
            catch (const std::exception& Error) {
                State->FailureReason = Error.what();
            }
            catch (...) {
                State->FailureReason = "Unknown shader compilation failure";
            }

            std::lock_guard<std::mutex> Lock(State->Mutex);
            State->Finished = true;
        }
    );
}

void Shader::Update()
{
    if (GetState() != ResourceState::Loading) {
        return;
    }

    std::lock_guard<std::mutex> Lock(LoadState->Mutex);
    if (!LoadState->Finished) {
        return;
    }
    if (!LoadState->FailureReason.empty()) {
        SetFailed(LoadState->FailureReason);
        return;
    }

    try {
        if (!LoadState->Vertex.empty()) {
            Graphics = ::Engine::Base::Render::Vk::Shader::CreateGraphicsPipeline(
                LoadState->Vertex, LoadState->Fragment, CreateInfo.Graphics);
        }
        if (!LoadState->Compute.empty()) {
            Compute = ::Engine::Base::Render::Vk::Shader::CreateComputePipeline(LoadState->Compute, CreateInfo.Compute);
        }
        LoadState->Vertex.clear();
        LoadState->Fragment.clear();
        LoadState->Compute.clear();
        SetState(ResourceState::Ready);
    }
    catch (const std::exception& Error) {
        Unload();
        SetFailed(Error.what());
    }
    catch (...) {
        Unload();
        SetFailed("Unknown shader pipeline failure");
    }
}

void Shader::Unload()
{
    ::Engine::Base::Render::Vk::Shader::DestroyPipeline(Graphics);
    ::Engine::Base::Render::Vk::Shader::DestroyPipeline(Compute);
    Graphics = {};
    Compute = {};
}

const ::Engine::Base::Render::Pipeline& Shader::GetGraphicsPipeline() const { return Graphics; }
const ::Engine::Base::Render::Pipeline& Shader::GetComputePipeline() const { return Compute; }
RenderQueue Shader::GetRenderQueue() const { return CreateInfo.Queue; }

}
