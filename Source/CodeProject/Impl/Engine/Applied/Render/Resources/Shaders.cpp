#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

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

    ::Engine::Base::Render::Pipeline Graphics;
    ::Engine::Base::Render::Pipeline ComputePipeline;
    uint64_t VersionChecksum = 0;

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

void DestroyPipelineAsync(::Engine::Base::Render::Pipeline& p_Target)
{
    if (p_Target.Handle == VK_NULL_HANDLE && p_Target.Layout == VK_NULL_HANDLE) {
        return;
    }

    const ::Engine::Base::Render::Pipeline Target = p_Target;
    p_Target = {};

    ::Engine::Base::Render::ResourceLoader::PushDeferredGpuCallback(
        [Target] {
            ::Engine::Base::Render::Vk::Shader::DestroyPipeline(Target);
        },
        ::Engine::Configs::Static::ResourceLoader::GPU_OPS_HIGH_ORDER
    );
}

}

Shader::Shader(ShaderCreateInfo p_CreateInfo)
    : Resource(MakeKey(p_CreateInfo))
    , m_CreateInfo(std::move(p_CreateInfo))
    , LoadState(std::make_shared<LoadData>())
{
}

Shader::~Shader() = default;

std::string Shader::GetTypeName() const { return "Shader"; }

void Shader::BeginLoad()
{
    AdvanceVersionChecksum();

    LoadState = std::make_shared<LoadData>();
    GpuTicket.reset();

    const std::shared_ptr<LoadData> State = LoadState;
    const ShaderCreateInfo Info = m_CreateInfo;
    State->VersionChecksum = GetVersionChecksum();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh, "shader_load",
        [State, Info] {
            try {
                X_ASSERT(Info.VertexFile.empty() && Info.ComputeFile.empty(), "shader requires vertex or compute stage");
                X_ASSERT(Info.VertexFile.empty() && !Info.FragmentFile.empty(), "fragment stage requires vertex stage");

                State->Vertex = Compile(Info.VertexFile, ::Engine::Base::Render::ShaderStage::Vertex, Info.Graphics.Defines);
                State->Fragment = Compile(Info.FragmentFile, ::Engine::Base::Render::ShaderStage::Fragment, Info.Graphics.Defines);
                State->Compute = Compile(Info.ComputeFile, ::Engine::Base::Render::ShaderStage::Compute, Info.Compute.Defines);
            }

            catch (const std::exception& Error) {
                State->FailureReason = Error.what();
            }

            catch (...) {
                State->FailureReason = "unknown shader compilation failure";
            }

            std::lock_guard<std::mutex> Lock(State->Mutex);
            State->Finished = true;
        }
    );
}

void Shader::Update()
{
    if (GetState() != ::Engine::Util::Applied::ResourceState::LoadingCpu
        && GetState() != ::Engine::Util::Applied::ResourceState::QueuedGpu
        && GetState() != ::Engine::Util::Applied::ResourceState::Uploading) {

        return;
    }

    if (GpuTicket) {
        if (GetVersionChecksum() != GpuTicket->VersionChecksum) {
            SetRejected("shader GPU job version is stale");

            return;
        }

        if (!GpuTicket->Done.load(std::memory_order_acquire)) {
            SetState(::Engine::Util::Applied::ResourceState::Uploading);

            return;
        }

        if (GpuTicket->Rejected.load(std::memory_order_acquire)) {
            SetRejected(GpuTicket->FailureReason);

            return;
        }

        if (GpuTicket->Failed.load(std::memory_order_acquire)) {
            SetFailed(GpuTicket->FailureReason);

            return;
        }

        const ::Engine::Base::Render::Pipeline PreviousGraphics = Graphics;
        const ::Engine::Base::Render::Pipeline PreviousCompute = Compute;

        Graphics = LoadState->Graphics;
        Compute = LoadState->ComputePipeline;

        const std::shared_ptr<ShaderSnapshot> Published = std::make_shared<ShaderSnapshot>();

        Published->Graphics = Graphics;
        Published->Compute = Compute;
        Published->VersionChecksum = GpuTicket->VersionChecksum;

        Snapshot.store(
            std::shared_ptr<const ShaderSnapshot>(Published),
            std::memory_order_release
        );

        ::Engine::Base::Render::Pipeline OldGraphics = PreviousGraphics;
        ::Engine::Base::Render::Pipeline OldCompute = PreviousCompute;

        DestroyPipelineAsync(OldGraphics);
        DestroyPipelineAsync(OldCompute);

        SetState(::Engine::Util::Applied::ResourceState::Ready);

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

    const std::shared_ptr<LoadData> Data = LoadState;
    const ShaderCreateInfo Info = m_CreateInfo;
    const uint64_t VersionChecksum = GetVersionChecksum();

    SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);

    GpuTicket = ::Engine::Base::Render::ResourceLoader::PushFrameJobCallback(
        [Data, Info, VersionChecksum] {
            std::lock_guard<std::mutex> Lock(Data->Mutex);

            if (Data->VersionChecksum != VersionChecksum) {
                return;
            }

            try {
                if (!Data->Vertex.empty()) {
                    Data->Graphics = ::Engine::Base::Render::Vk::Shader::CreateGraphicsPipeline(
                        Data->Vertex,
                        Data->Fragment,
                        Info.Graphics
                    );
                }

                if (!Data->Compute.empty()) {
                    Data->ComputePipeline = ::Engine::Base::Render::Vk::Shader::CreateComputePipeline(
                        Data->Compute,
                        Info.Compute
                    );
                }
            }

            catch (...) {
                ::Engine::Base::Render::Vk::Shader::DestroyPipeline(Data->Graphics);
                ::Engine::Base::Render::Vk::Shader::DestroyPipeline(Data->ComputePipeline);

                Data->Graphics = {};
                Data->ComputePipeline = {};

                throw;
            }
        },
        ::Engine::Configs::Static::ResourceLoader::GPU_OPS_HIGH_ORDER,
        { 0, 0, 1, 0, 0 }
    );

    GpuTicket->VersionChecksum = VersionChecksum;
}

void Shader::Unload()
{
    AdvanceVersionChecksum();

    {
        std::lock_guard<std::mutex> Lock(LoadState->Mutex);
        LoadState->VersionChecksum = GetVersionChecksum();
    }

    if (GetState() == ::Engine::Util::Applied::ResourceState::Unloading) {
        return;
    }

    SetState(::Engine::Util::Applied::ResourceState::Unloading);
    DestroyPipelineAsync(Graphics);
    DestroyPipelineAsync(Compute);
}

const ::Engine::Base::Render::Pipeline& Shader::GetGraphicsPipeline() const { return Graphics; }
const ::Engine::Base::Render::Pipeline& Shader::GetComputePipeline() const { return Compute; }
RenderQueue Shader::GetRenderQueue() const { return m_CreateInfo.Queue; }

std::shared_ptr<const ShaderSnapshot> Shader::GetSnapshot() const
{
    return Snapshot.load(std::memory_order_acquire);
}

}
