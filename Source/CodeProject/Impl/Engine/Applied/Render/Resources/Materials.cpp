#include "X/Engine/Applied/Render/Resources/Materials.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"

#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

#include <glm/vec4.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Engine::Applied::Render::Resources::Materials
{

struct Material::LoadData
{
    std::mutex   Mutex;
    MaterialData Data = {};

    uint64_t VersionChecksum = 0;

    std::string FailureReason;

    bool Finished = false;
    bool Failed   = false;
};

namespace
{

const std::string ValidatorShaderPath = "Materials/Service/MaterialValidator.comp";

static_assert(std::is_standard_layout_v<MaterialData>);
static_assert(sizeof(MaterialTextureData)      == 48);
static_assert(sizeof(MaterialData)             == 1200);
static_assert(offsetof(MaterialData, Textures) == 192);

std::once_flag m_Validated;

void ValidateLayout()
{
    constexpr uint32_t OpTypeRuntimeArray    = 29;
    constexpr uint32_t OpDecorate            = 71;
    constexpr uint32_t OpMemberDecorate      = 72;
    constexpr uint32_t DecorationArrayStride = 6;
    constexpr uint32_t DecorationOffset      = 35;

    const std::string ValidateShaderSource = ::Engine::Util::File::ReadRaw(
        std::string(::Project::Configs::Static::Contents::SHADERS_DIR) + ValidatorShaderPath
    );

    const std::vector<uint32_t> ValidateShader = ::Engine::Base::Render::Vk::Shader::CompileGlsl(
        ValidateShaderSource, ::Engine::Base::Render::ShaderStage::Compute, ValidatorShaderPath, {}, false
    );

    const std::size_t Offsets[] = {
        offsetof(MaterialData, BaseColor),
        offsetof(MaterialData, Emissive),
        offsetof(MaterialData, Metallic),
        offsetof(MaterialData, Roughness),
        offsetof(MaterialData, NormalScale),
        offsetof(MaterialData, OcclusionStrength),
        offsetof(MaterialData, AlphaCutoff),

        offsetof(MaterialData, SpecularColor),
        offsetof(MaterialData, SpecularFactor),

        offsetof(MaterialData,  DiffuseTransmissionColor),
        offsetof(MaterialData, DiffuseTransmissionFactor),

        offsetof(MaterialData, SheenColor),
        offsetof(MaterialData, SheenRoughness),

        offsetof(MaterialData, AttenuationColor),
        offsetof(MaterialData, AttenuationDistance),

        offsetof(MaterialData, Ior),

        offsetof(MaterialData, EmissiveStrength),

        offsetof(MaterialData, ClearcoatFactor),
        offsetof(MaterialData, ClearcoatRoughness),
        offsetof(MaterialData, ClearcoatNormalScale),

        offsetof(MaterialData, HeightScale),
        offsetof(MaterialData, HeightWorld),

        offsetof(MaterialData, TransmissionFactor),
        offsetof(MaterialData, ThicknessFactor),

        offsetof(MaterialData, IridescenceFactor),
        offsetof(MaterialData, IridescenceIor),
        offsetof(MaterialData, IridescenceThicknessMin),
        offsetof(MaterialData, IridescenceThicknessMax),

        offsetof(MaterialData, AnisotropyStrength),
        offsetof(MaterialData, AnisotropyRotation),

        offsetof(MaterialData, AlphaMode),
        offsetof(MaterialData, Options),
        offsetof(MaterialData, Textures)
    };

    std::unordered_map<uint32_t, uint32_t> ArrayElements;
    std::unordered_map<uint32_t, uint32_t> ArrayStrides;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> MemberOffsets;

    for (std::size_t Position = 5; Position < ValidateShader.size();) {
        const uint32_t Count = ValidateShader[Position] >> 16;
        const uint32_t Opcode = ValidateShader[Position] & 0xFFFF;

        X_ASSERT(Count == 0 || Count > ValidateShader.size() - Position, "invalid material spir-v instruction");

        if (Opcode == OpTypeRuntimeArray && Count == 3) {
            ArrayElements[ValidateShader[Position + 1]] = ValidateShader[Position + 2];
        }

        if (Opcode == OpDecorate && Count == 4 && ValidateShader[Position + 2] == DecorationArrayStride) {
            ArrayStrides[ValidateShader[Position + 1]] = ValidateShader[Position + 3];
        }

        if (Opcode == OpMemberDecorate && Count == 5 && ValidateShader[Position + 3] == DecorationOffset) {
            MemberOffsets[ValidateShader[Position + 1]][ValidateShader[Position + 2]] = ValidateShader[Position + 4];
        }

        Position += Count;
    }

    bool Matched = false;

    for (const auto& [Array, Element] : ArrayElements) {
        const auto Members = MemberOffsets.find(Element);

        if (Members == MemberOffsets.end() || Members->second.size() != sizeof(Offsets) / sizeof(Offsets[0])) {
            continue;
        }

        X_ASSERT(ArrayStrides[Array] != sizeof(MaterialData), "MaterialData GPU array stride mismatch");

        for (uint32_t I = 0; I < sizeof(Offsets) / sizeof(Offsets[0]); ++I) {
            const auto Offset = Members->second.find(I);

            X_ASSERT(Offset == Members->second.end() || Offset->second != Offsets[I], "MaterialData GPU member offset mismatch");
        }

        Matched = true;
    }

    X_ASSERT(!Matched, "MaterialData layout was not found in SPIR-V");
}

void ValidateAbi()
{
    std::call_once(m_Validated, ValidateLayout);
}

std::mutex                m_RegistryMutex;
std::vector<MaterialData> m_Data;
std::vector<uint32_t>     m_FreeIndices;

struct ErasedMaterial
{
    uint32_t Index = UINT32_MAX;
    int FramesLeft = 0;
};

std::vector<ErasedMaterial> m_ErasedMaterials;

::Engine::Base::Render::Buffer m_Buffers[::Project::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT];

uint64_t m_Version = 0;
uint64_t m_BufferVersions[::Project::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT] = {};

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("materials_init", Init, 30);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("materials_cleanup", Cleanup, 75);

std::array<const MaterialTexture*, MATERIAL_TEXTURES_COUNT> GetTextures(const MaterialCreateInfo& p_Info)
{
    return {
        &p_Info.BaseColorTexture,
        &p_Info.MetallicRoughnessTexture,
        &p_Info.NormalTexture,
        &p_Info.OcclusionTexture,
        &p_Info.EmissiveTexture,

        &p_Info.SpecularTexture,
        &p_Info.SpecularColorTexture,

        &p_Info.DiffuseTransmissionTexture,
        &p_Info.DiffuseTransmissionColorTexture,

        &p_Info.ClearcoatTexture,
        &p_Info.ClearcoatRoughnessTexture,
        &p_Info.ClearcoatNormalTexture,

        &p_Info.SheenColorTexture,
        &p_Info.SheenRoughnessTexture,

        &p_Info.HeightTexture,

        &p_Info.TransmissionTexture,

        &p_Info.ThicknessTexture,

        &p_Info.IridescenceTexture,
        &p_Info.IridescenceThicknessTexture,

        &p_Info.AnisotropyTexture,
        &p_Info.PackedNormalMetallicRoughnessTexture
    };
}

std::string MakeKey(const MaterialCreateInfo& p_CreateInfo)
{
    std::string Key;

    const std::string ShaderKey = p_CreateInfo.TargetShader ? p_CreateInfo.TargetShader->GetKey() : std::string();

    Key += std::to_string(ShaderKey.size()) + ":" + ShaderKey;

    std::ostringstream ExtraSetKey;

    ExtraSetKey << p_CreateInfo.ExtraSet;
    Key += ":" + ExtraSetKey.str();

    const auto Add = [&Key](float p_Value) {
        Key += ":" + std::to_string(std::bit_cast<uint32_t>(p_Value));
    };

    const auto AddVector3 = [&Add](const glm::vec3& p_Value) {
        Add(p_Value.x);
        Add(p_Value.y);
        Add(p_Value.z);
    };

    const auto AddVector4 = [&Add](const glm::vec4& p_Value) {
        Add(p_Value.x);
        Add(p_Value.y);
        Add(p_Value.z);
        Add(p_Value.w);
    };

    AddVector4(p_CreateInfo.BaseColor);

    AddVector3(p_CreateInfo.Emissive);
    Add(p_CreateInfo.Metallic);
    Add(p_CreateInfo.Roughness);
    Add(p_CreateInfo.NormalScale);
    Add(p_CreateInfo.OcclusionStrength);

    Add(p_CreateInfo.AlphaCutoff);

    Key += ":" + std::to_string(static_cast<uint32_t>(p_CreateInfo.Alpha));
    Key += ":" + std::to_string(p_CreateInfo.DoubleSided);
    Key += ":" + std::to_string(p_CreateInfo.CastShadows);
    Key += ":" + std::to_string(p_CreateInfo.Unlit);

    Add(p_CreateInfo.Ior);

    Add(p_CreateInfo.SpecularFactor);

    Add(p_CreateInfo.EmissiveStrength);

    AddVector3(p_CreateInfo.SpecularColor);

    Add(p_CreateInfo.DiffuseTransmissionFactor);
    AddVector3(p_CreateInfo.DiffuseTransmissionColor);

    Add(p_CreateInfo.ClearcoatFactor);
    Add(p_CreateInfo.ClearcoatRoughness);
    Add(p_CreateInfo.ClearcoatNormalScale);

    AddVector3(p_CreateInfo.SheenColor);
    Add(p_CreateInfo.SheenRoughness);

    Add(p_CreateInfo.HeightScale);
    Add(p_CreateInfo.HeightWorld);

    Add(p_CreateInfo.TransmissionFactor);

    Add(p_CreateInfo.ThicknessFactor);

    Add(p_CreateInfo.AttenuationDistance);
    AddVector3(p_CreateInfo.AttenuationColor);

    Add(p_CreateInfo.IridescenceFactor);
    Add(p_CreateInfo.IridescenceIor);
    Add(p_CreateInfo.IridescenceThicknessMin);
    Add(p_CreateInfo.IridescenceThicknessMax);
   
    Add(p_CreateInfo.AnisotropyStrength);
    Add(p_CreateInfo.AnisotropyRotation);

    for (const MaterialTexture* Texture : GetTextures(p_CreateInfo)) {
        const std::string Path = Texture->Target ? Texture->Target->GetKey() : std::string();
        Key += ":" + std::to_string(Path.size()) + ":" + Path;
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.MagFilter));
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.MinFilter));
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.MipMode));
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.WrapU));
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.WrapV));
        Key += ":" + std::to_string(static_cast<int>(Texture->Sampler.WrapW));
        Key += ":" + std::to_string(Texture->Sampler.Anisotropy);

        Add(Texture->UvOffset.x);
        Add(Texture->UvOffset.y);
        Add(Texture->UvScale.x);
        Add(Texture->UvScale.y);
        Add(Texture->UvRotation);
        Key += ":" + std::to_string(Texture->TexCoord);
    }

    return Key;
}

void WriteUvTransform(const MaterialTexture& p_Source, MaterialTextureData& p_Target)
{
    const float CosRotation = std::cos(p_Source.UvRotation);
    const float SinRotation = std::sin(p_Source.UvRotation);

    p_Target.TexCoord = p_Source.TexCoord;
    p_Target.UvTransformRow0 = glm::vec4(
        CosRotation  * p_Source.UvScale.x,
        -SinRotation * p_Source.UvScale.y,
        p_Source.UvOffset.x,
        0.0f
    );

    p_Target.UvTransformRow1 = glm::vec4(
        SinRotation * p_Source.UvScale.x,
        CosRotation * p_Source.UvScale.y,
        p_Source.UvOffset.y,
        0.0f
    );
}

MaterialData BuildBaseData(const MaterialCreateInfo& p_CreateInfo)
{
    MaterialData Result = {};

    Result.BaseColor = p_CreateInfo.BaseColor;
    Result.Emissive  = p_CreateInfo.Emissive;
    Result.Metallic  = p_CreateInfo.Metallic;
    Result.Roughness = p_CreateInfo.Roughness;

    Result.NormalScale       = p_CreateInfo.NormalScale;
    Result.OcclusionStrength = p_CreateInfo.OcclusionStrength;

    Result.AlphaCutoff = p_CreateInfo.AlphaCutoff;
    Result.AlphaMode   = static_cast<uint32_t>(p_CreateInfo.Alpha);

    const auto Textures = GetTextures(p_CreateInfo);
    for (uint32_t I = 0; I < MATERIAL_TEXTURES_COUNT; ++I) {
        WriteUvTransform(*Textures[I], Result.Textures[I]);
    }

    Result.SpecularColor  = p_CreateInfo.SpecularColor;
    Result.SpecularFactor = p_CreateInfo.SpecularFactor;

    Result.DiffuseTransmissionColor  = p_CreateInfo.DiffuseTransmissionColor;
    Result.DiffuseTransmissionFactor = p_CreateInfo.DiffuseTransmissionFactor;

    Result.SheenColor     = p_CreateInfo.SheenColor;
    Result.SheenRoughness = p_CreateInfo.SheenRoughness;

    Result.AttenuationColor    = p_CreateInfo.AttenuationColor;
    Result.AttenuationDistance = p_CreateInfo.AttenuationDistance;

    Result.Ior = p_CreateInfo.Ior;

    Result.EmissiveStrength = p_CreateInfo.EmissiveStrength;

    Result.ClearcoatFactor      = p_CreateInfo.ClearcoatFactor;
    Result.ClearcoatRoughness   = p_CreateInfo.ClearcoatRoughness;
    Result.ClearcoatNormalScale = p_CreateInfo.ClearcoatNormalScale;

    Result.HeightScale = p_CreateInfo.HeightScale;
    Result.HeightWorld = p_CreateInfo.HeightWorld;

    Result.TransmissionFactor = p_CreateInfo.TransmissionFactor;

    Result.ThicknessFactor = p_CreateInfo.ThicknessFactor;

    Result.IridescenceFactor       = p_CreateInfo.IridescenceFactor;
    Result.IridescenceIor          = p_CreateInfo.IridescenceIor;
    Result.IridescenceThicknessMin = p_CreateInfo.IridescenceThicknessMin;
    Result.IridescenceThicknessMax = p_CreateInfo.IridescenceThicknessMax;

    Result.AnisotropyStrength = p_CreateInfo.AnisotropyStrength;
    Result.AnisotropyRotation = p_CreateInfo.AnisotropyRotation;

    Result.Options.x = (p_CreateInfo.Unlit ? MaterialUnlit : 0u)
        | (p_CreateInfo.DoubleSided ? MaterialDoubleSided : 0u)
        | (p_CreateInfo.CastShadows ? MaterialCastShadows : 0u);

    return Result;
}

uint32_t TakeIndex()
{
    std::lock_guard<std::mutex> Lock(m_RegistryMutex);

    if (!m_FreeIndices.empty()) {
        const uint32_t Index = m_FreeIndices.back();

        m_FreeIndices.pop_back();

        return Index;
    }

    X_ASSERT(m_Data.size() >= ::Project::Configs::Static::Graphics::MAX_MATERIALS, "materials limit exceeded");

    m_Data.push_back({});

    return static_cast<uint32_t>(m_Data.size() - 1);
}

void ReleaseIndex(uint32_t p_Index)
{
    if (p_Index == UINT32_MAX) { return; }

    std::lock_guard<std::mutex> Lock(m_RegistryMutex);

    m_ErasedMaterials.push_back({ p_Index, ::Project::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT });
}

bool TextureFailed(
    const MaterialTexture& p_Texture,
    std::string& p_Reason,
    bool& p_Rejected
)
{
    if (!p_Texture.Target || p_Texture.Target.IsReady()) { return false; }

    if (p_Texture.Target.GetState() == ::Engine::Util::Applied::ResourceState::Failed
        || p_Texture.Target.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        p_Reason = p_Texture.Target->GetKey() + ": " + p_Texture.Target->GetFailureReason();

        p_Rejected = p_Texture.Target.GetState() == ::Engine::Util::Applied::ResourceState::Rejected;

        return true;
    }

    return false;
}

bool TexturesReady(
    const MaterialCreateInfo& p_CreateInfo,
    std::string& p_Reason,
    bool& p_Rejected
)
{
    bool Ready = true;

    for (const MaterialTexture* Texture : GetTextures(p_CreateInfo)) {
        if (TextureFailed(*Texture, p_Reason, p_Rejected)) {
            return false;
        }

        if (Texture->Target && !Texture->Target.IsReady()) {
            Ready = false;
        }
    }

    return Ready;
}

void SetTexture(MaterialTextureData& p_Target, const MaterialTexture& p_Source)
{
    if (!p_Source.Target) { return; }

    const auto Snapshot = p_Source.Target->GetSnapshot();

    if (!Snapshot) { return; }

    p_Target.Texture = Snapshot->Index;
    p_Target.Sampler = ::Engine::Base::Render::Vk::Samplers::Make(p_Source.Sampler);
}

}

Material::Material(MaterialCreateInfo p_CreateInfo)
    : Resource(MakeKey(p_CreateInfo))
    , m_CreateInfo(std::move(p_CreateInfo))
    , LoadState(std::make_shared<LoadData>())
{
}

std::string Material::GetTypeName() const
{
    return "Material";
}

void Material::BeginLoad()
{
    ReleaseIndex(Index);
    Index = UINT32_MAX;

    AdvanceVersionChecksum();

    LoadState = std::make_shared<LoadData>();
    m_Snapshot.store(nullptr, std::memory_order_release);

    if (!m_CreateInfo.TargetShader) {
        SetFailed("material requires shader resource via handler");

        return;
    }

    const std::shared_ptr<LoadData> DataState = LoadState;

    const MaterialCreateInfo Info = m_CreateInfo;

    DataState->VersionChecksum = GetVersionChecksum();

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "material_load_" + GetKey(),
        [DataState, Info] {
            try {
                ValidateAbi();
                DataState->Data = BuildBaseData(Info);
            }

            catch (const std::exception& Error) {
                DataState->FailureReason = Error.what();
                DataState->Failed = true;
            }

            catch (...) {
                DataState->FailureReason = "unknown material preparation failure";
                DataState->Failed = true;
            }

            std::lock_guard<std::mutex> Lock(DataState->Mutex);

            DataState->Finished = true;
        }
    );
}

void Material::Update()
{
    if (GetState() != ::Engine::Util::Applied::ResourceState::LoadingCpu
        && GetState() != ::Engine::Util::Applied::ResourceState::QueuedGpu
        && GetState() != ::Engine::Util::Applied::ResourceState::Uploading) {

        return;
    }

    if (m_CreateInfo.TargetShader.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
        SetFailed("shader dependency failed: " + m_CreateInfo.TargetShader->GetFailureReason());

        return;
    }

    if (m_CreateInfo.TargetShader.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        SetRejected("shader dependency rejected: " + m_CreateInfo.TargetShader->GetFailureReason());

        return;
    }

    if (!m_CreateInfo.TargetShader.IsReady()) {
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(LoadState->Mutex);

        if (!LoadState->Finished) {
            return;
        }

        if (LoadState->VersionChecksum != GetVersionChecksum()) {
            SetRejected("material cpu job version is stale");

            return;
        }

        if (LoadState->Failed) {
            SetFailed(LoadState->FailureReason);

            return;
        }

        Data = LoadState->Data;
    }

    std::string FailureReason;

    bool DependencyRejected = false;

    if (!TexturesReady(m_CreateInfo, FailureReason, DependencyRejected)) {
        if (!FailureReason.empty()) {
            if (DependencyRejected) {
                SetRejected("texture dependency rejected: " + FailureReason);
            } else {
                SetFailed("texture dependency failed: " + FailureReason);
            }
        }

        return;
    }

    const auto Textures = GetTextures(m_CreateInfo);
    for (uint32_t I = 0; I < MATERIAL_TEXTURES_COUNT; ++I) {
        SetTexture(Data.Textures[I], *Textures[I]);
    }
    ::Engine::Applied::Render::Resources::Textures::SyncSamplers();

    Index = TakeIndex();

    {
        std::lock_guard<std::mutex> Lock(m_RegistryMutex);
        m_Data[Index] = Data;
        ++m_Version;
    }

    auto Snapshot = std::make_shared<MaterialSnapshot>();
    Snapshot->Data = Data;
    Snapshot->Index = Index;
    Snapshot->VersionChecksum = GetVersionChecksum();
    m_Snapshot.store(std::move(Snapshot), std::memory_order_release);
    SetState(::Engine::Util::Applied::ResourceState::Ready);
}

void Material::Unload()
{
    AdvanceVersionChecksum();

    if (GetState() == ::Engine::Util::Applied::ResourceState::Unloading) {
        return;
    }

    SetState(::Engine::Util::Applied::ResourceState::Unloading);

    ReleaseIndex(Index);
    Index = UINT32_MAX;
}

std::shared_ptr<const MaterialSnapshot> Material::GetSnapshot() const
{
    return m_Snapshot.load(std::memory_order_acquire);
}

uint32_t Material::GetIndex() const { return Index; }

const MaterialData& Material::GetData() const { return Data; }

bool Material::IsDoubleSided() const { return m_CreateInfo.DoubleSided; }
bool Material::CastsShadows() const { return m_CreateInfo.CastShadows; }

AlphaMode Material::GetAlphaMode() const { return m_CreateInfo.Alpha; }

const ::Engine::Util::Applied::ResourceHandle<::Engine::Applied::Render::Resources::Shaders::Shader>& Material::GetShader() const { return m_CreateInfo.TargetShader; }

VkDescriptorSet Material::GetExtraSet() const { return m_CreateInfo.ExtraSet; }

void Init()
{
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot("scene_materials_flush", Flush, 899);
}

void Cleanup()
{
    for (auto& Buffer : m_Buffers) { ::Engine::Base::Render::ResourceLoader::DestroyBuffer(Buffer); }

    std::lock_guard<std::mutex> Lock(m_RegistryMutex);

    m_Data.clear();
    m_FreeIndices.clear();
    m_ErasedMaterials.clear();

    m_Version = 0;

    for (auto& Version : m_BufferVersions) { Version = 0; }
}

void Flush()
{
    std::lock_guard<std::mutex> Lock(m_RegistryMutex);

    for (auto It = m_ErasedMaterials.begin(); It != m_ErasedMaterials.end();) {
        if (It->FramesLeft > 0) {
            It->FramesLeft--;
            ++It;

            continue;
        }

        m_Data[It->Index] = {};
        m_FreeIndices.push_back(It->Index);

        It = m_ErasedMaterials.erase(It);

        ++m_Version;
    }

    namespace RenderBase = ::Engine::Base::Render;
    const uint32_t Frame = RenderBase::Vk::Frame::GetCurrentFrameIndex();

    if (m_BufferVersions[Frame] == m_Version || m_Data.empty()) { return; }

    auto& Buffer = m_Buffers[Frame];
    const VkDeviceSize Bytes = sizeof(MaterialData) * m_Data.size();

    if (Buffer.Size < Bytes) {
        const auto Replacement = RenderBase::ResourceLoader::CreateMappedBuffer(
            std::max(Bytes, Buffer.Size * 2),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        );

        RenderBase::ResourceLoader::DestroyBuffer(Buffer);

        Buffer = Replacement;
    }

    std::memcpy(RenderBase::Vk::GpuResources::GetMappedData(Buffer), m_Data.data(), Bytes);
    RenderBase::Vk::GpuResources::FlushMappedBuffer(Buffer, 0, Bytes);

    m_BufferVersions[Frame] = m_Version;
}

const std::vector<MaterialData>& GetData()
{
    return m_Data;
}

const ::Engine::Base::Render::Buffer& GetBuffer()
{
    return m_Buffers[::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex()];
}

VkDeviceAddress GetBufferAddress()
{
    return GetBuffer().Address;
}

}
