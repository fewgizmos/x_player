#include "X/Engine/Applied/Render/Resources/Materials.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Util/Base/Assert.hpp"
#include "X/Engine/Util/File.hpp"

#include "X/Engine/Applied/Render/Resources/Textures.hpp"
#include "X/Engine/Applied/Render/Resources/Shaders.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Render/RenderingPool.hpp"
#include "X/Engine/Base/Render/ResourceLoader.hpp"
#include "X/Engine/Base/Render/Vulkan/Frame.hpp"
#include "X/Engine/Base/Render/Vulkan/GpuResources.hpp"
#include "X/Engine/Base/Render/Vulkan/Samplers.hpp"
#include "X/Engine/Base/Render/Vulkan/Shader.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include <glm/vec4.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    std::mutex Mutex;
    MaterialData Data = {};

    uint64_t VersionChecksum = 0;

    std::string FailureReason;

    bool Finished = false;
    bool Failed = false;
};

namespace
{

static_assert(std::is_standard_layout_v<MaterialData>);
static_assert(sizeof(MaterialData) == 704);
static_assert(alignof(MaterialData) == alignof(glm::vec4));
static_assert(offsetof(MaterialData, UvTransforms) == 176);
static_assert(offsetof(MaterialData, SpecularColor) == 624);
static_assert(offsetof(MaterialData, Padding1) == 700);

std::once_flag m_Validated;

void ValidateLayout()
{
    constexpr uint32_t OpTypeRuntimeArray = 29;
    constexpr uint32_t OpDecorate = 71;
    constexpr uint32_t OpMemberDecorate = 72;
    constexpr uint32_t DecorationArrayStride = 6;
    constexpr uint32_t DecorationOffset = 35;

    const std::string ValidateShaderSource = ::Engine::Util::File::ReadRaw(
        std::string(::Engine::Configs::Static::Graphics::SHADERS_DIR) + "Util/Materials/Validate.comp"
    );
    const std::vector<uint32_t> ValidateShader = ::Engine::Base::Render::Vk::Shader::CompileGlsl(
        ValidateShaderSource, ::Engine::Base::Render::ShaderStage::Compute, "Util/Materials/Validate.comp", {}, false
    );

    const std::size_t Offsets[] = {
        offsetof(MaterialData, BaseColor),
        offsetof(MaterialData, Emissive),
        offsetof(MaterialData, Metallic),
        offsetof(MaterialData, Roughness),
        offsetof(MaterialData, NormalScale),
        offsetof(MaterialData, OcclusionStrength),
        offsetof(MaterialData, AlphaCutoff),
        offsetof(MaterialData, BaseColorTexture),
        offsetof(MaterialData, BaseColorSampler),
        offsetof(MaterialData, MetallicRoughnessTexture),
        offsetof(MaterialData, MetallicRoughnessSampler),
        offsetof(MaterialData, NormalTexture),
        offsetof(MaterialData, NormalSampler),
        offsetof(MaterialData, OcclusionTexture),
        offsetof(MaterialData, OcclusionSampler),
        offsetof(MaterialData, EmissiveTexture),
        offsetof(MaterialData, EmissiveSampler),
        offsetof(MaterialData, AlphaMode),
        offsetof(MaterialData, SpecularTexture),
        offsetof(MaterialData, SpecularSampler),
        offsetof(MaterialData, SpecularColorTexture),
        offsetof(MaterialData, SpecularColorSampler),
        offsetof(MaterialData, DiffuseTransmissionTexture),
        offsetof(MaterialData, DiffuseTransmissionSampler),
        offsetof(MaterialData, DiffuseTransmissionColorTexture),
        offsetof(MaterialData, DiffuseTransmissionColorSampler),
        offsetof(MaterialData, ClearcoatTexture),
        offsetof(MaterialData, ClearcoatSampler),
        offsetof(MaterialData, ClearcoatRoughnessTexture),
        offsetof(MaterialData, ClearcoatRoughnessSampler),
        offsetof(MaterialData, ClearcoatNormalTexture),
        offsetof(MaterialData, ClearcoatNormalSampler),
        offsetof(MaterialData, SheenColorTexture),
        offsetof(MaterialData, SheenColorSampler),
        offsetof(MaterialData, SheenRoughnessTexture),
        offsetof(MaterialData, SheenRoughnessSampler),
        offsetof(MaterialData, HeightTexture),
        offsetof(MaterialData, HeightSampler),
        offsetof(MaterialData, Padding0),
        offsetof(MaterialData, UvTransforms),
        offsetof(MaterialData, SpecularColor),
        offsetof(MaterialData, SpecularFactor),
        offsetof(MaterialData, Ior),
        offsetof(MaterialData, EmissiveStrength),
        offsetof(MaterialData, ClearcoatFactor),
        offsetof(MaterialData, ClearcoatRoughness),
        offsetof(MaterialData, DiffuseTransmissionColor),
        offsetof(MaterialData, DiffuseTransmissionFactor),
        offsetof(MaterialData, SheenColor),
        offsetof(MaterialData, SheenRoughness),
        offsetof(MaterialData, ClearcoatNormalScale),
        offsetof(MaterialData, HeightScale),
        offsetof(MaterialData, HeightWorld),
        offsetof(MaterialData, Padding1)
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

std::mutex m_RegistryMutex;
std::vector<MaterialData> m_Data;
std::vector<uint32_t> m_FreeIndices;

struct ErasedMaterial
{
    uint32_t Index = UINT32_MAX;
    int FramesLeft = 0;
};

std::vector<ErasedMaterial> m_ErasedMaterials;
std::vector<Material*> m_PendingGpuMaterials;

::Engine::Base::Render::Buffer m_Buffers[::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT];

std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket>
    m_BufferTickets[::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT];

std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket>
    m_BufferCreateTickets[::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT];

std::atomic<std::shared_ptr<const MaterialBufferSnapshot>> m_BufferSnapshot;
std::shared_ptr<const MaterialBufferSnapshot> m_FrameBufferSnapshot;

uint64_t m_Version = 0;
uint64_t m_BufferVersions[::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT] = {};

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("materials_init", Init, 30);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("materials_cleanup", Cleanup, 75);

std::string TextureKey(const MaterialTexture& p_Texture)
{
    return p_Texture.Target ? p_Texture.Target->GetKey() : std::string();
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

    const MaterialTexture* Textures[MATERIAL_TEXTURES_COUNT + 1] = {
        &p_CreateInfo.BaseColorTexture,
        &p_CreateInfo.MetallicRoughnessTexture,
        &p_CreateInfo.NormalTexture,
        &p_CreateInfo.OcclusionTexture,
        &p_CreateInfo.EmissiveTexture,
        &p_CreateInfo.SpecularTexture,
        &p_CreateInfo.SpecularColorTexture,
        &p_CreateInfo.DiffuseTransmissionTexture,
        &p_CreateInfo.DiffuseTransmissionColorTexture,
        &p_CreateInfo.ClearcoatTexture,
        &p_CreateInfo.ClearcoatRoughnessTexture,
        &p_CreateInfo.ClearcoatNormalTexture,
        &p_CreateInfo.SheenColorTexture,
        &p_CreateInfo.SheenRoughnessTexture,
        &p_CreateInfo.HeightTexture
    };

    for (const MaterialTexture* Texture : Textures) {
        const std::string Path = TextureKey(*Texture);
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
    }

    return Key;
}

void WriteUvTransform(const MaterialTexture& p_Source, glm::vec4& p_Row0, glm::vec4& p_Row1)
{
    const float CosRotation = std::cos(p_Source.UvRotation);
    const float SinRotation = std::sin(p_Source.UvRotation);

    p_Row0 = glm::vec4(
        CosRotation * p_Source.UvScale.x,
        -SinRotation * p_Source.UvScale.y,
        p_Source.UvOffset.x,
        0.0f
    );
    p_Row1 = glm::vec4(
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
    Result.Emissive = p_CreateInfo.Emissive;
    Result.Metallic = p_CreateInfo.Metallic;
    Result.Roughness = p_CreateInfo.Roughness;
    Result.NormalScale = p_CreateInfo.NormalScale;
    Result.OcclusionStrength = p_CreateInfo.OcclusionStrength;
    Result.AlphaCutoff = p_CreateInfo.AlphaCutoff;
    Result.AlphaMode = static_cast<uint32_t>(p_CreateInfo.Alpha);

    const MaterialTexture* Textures[MATERIAL_TEXTURES_COUNT] = {
        &p_CreateInfo.BaseColorTexture,
        &p_CreateInfo.MetallicRoughnessTexture,
        &p_CreateInfo.NormalTexture,
        &p_CreateInfo.OcclusionTexture,
        &p_CreateInfo.EmissiveTexture,
        &p_CreateInfo.SpecularTexture,
        &p_CreateInfo.SpecularColorTexture,
        &p_CreateInfo.DiffuseTransmissionTexture,
        &p_CreateInfo.DiffuseTransmissionColorTexture,
        &p_CreateInfo.ClearcoatTexture,
        &p_CreateInfo.ClearcoatRoughnessTexture,
        &p_CreateInfo.ClearcoatNormalTexture,
        &p_CreateInfo.SheenColorTexture,
        &p_CreateInfo.SheenRoughnessTexture
    };

    for (uint32_t I = 0; I < MATERIAL_TEXTURES_COUNT; I++) {
        WriteUvTransform(*Textures[I], Result.UvTransforms[I * 2], Result.UvTransforms[I * 2 + 1]);
    }

    Result.SpecularColor = p_CreateInfo.SpecularColor;
    Result.SpecularFactor = p_CreateInfo.SpecularFactor;
    Result.Ior = p_CreateInfo.Ior;
    Result.EmissiveStrength = p_CreateInfo.EmissiveStrength;
    Result.ClearcoatFactor = p_CreateInfo.ClearcoatFactor;
    Result.ClearcoatRoughness = p_CreateInfo.ClearcoatRoughness;
    Result.DiffuseTransmissionColor = p_CreateInfo.DiffuseTransmissionColor;
    Result.DiffuseTransmissionFactor = p_CreateInfo.DiffuseTransmissionFactor;
    Result.SheenColor = p_CreateInfo.SheenColor;
    Result.SheenRoughness = p_CreateInfo.SheenRoughness;
    Result.ClearcoatNormalScale = p_CreateInfo.ClearcoatNormalScale;
    Result.HeightScale = p_CreateInfo.HeightScale;
    Result.HeightWorld = p_CreateInfo.HeightWorld;

    Result.BaseColorTexture = NO_TEXTURE;
    Result.MetallicRoughnessTexture = NO_TEXTURE;
    Result.NormalTexture = NO_TEXTURE;
    Result.OcclusionTexture = NO_TEXTURE;
    Result.EmissiveTexture = NO_TEXTURE;
    Result.SpecularTexture = NO_TEXTURE;
    Result.SpecularColorTexture = NO_TEXTURE;
    Result.DiffuseTransmissionTexture = NO_TEXTURE;
    Result.DiffuseTransmissionColorTexture = NO_TEXTURE;
    Result.ClearcoatTexture = NO_TEXTURE;
    Result.ClearcoatRoughnessTexture = NO_TEXTURE;
    Result.ClearcoatNormalTexture = NO_TEXTURE;
    Result.SheenColorTexture = NO_TEXTURE;
    Result.SheenRoughnessTexture = NO_TEXTURE;
    Result.HeightTexture = NO_TEXTURE;

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

    X_ASSERT(m_Data.size() >= ::Engine::Configs::Static::Graphics::MAX_MATERIALS, "materials limit exceeded");
    m_Data.push_back({});
    return static_cast<uint32_t>(m_Data.size() - 1);
}

void ReleaseIndex(uint32_t p_Index)
{
    if (p_Index == UINT32_MAX) {
        return;
    }

    std::lock_guard<std::mutex> Lock(m_RegistryMutex);
    m_ErasedMaterials.push_back({ p_Index, ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT });
}

bool TextureFailed(
    const MaterialTexture& p_Texture,
    std::string& p_Reason,
    bool& p_Rejected
)
{
    if (!p_Texture.Target || p_Texture.Target.IsReady()) {
        return false;
    }

    if (p_Texture.Target.GetState() == ::Engine::Util::Applied::ResourceState::Failed
        || p_Texture.Target.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        p_Reason = p_Texture.Target->GetKey() + ": " + p_Texture.Target->GetFailureReason();

        p_Rejected = p_Texture.Target.GetState()
            == ::Engine::Util::Applied::ResourceState::Rejected;

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
    const MaterialTexture* Textures[MATERIAL_TEXTURES_COUNT + 1] = {
        &p_CreateInfo.BaseColorTexture,
        &p_CreateInfo.MetallicRoughnessTexture,
        &p_CreateInfo.NormalTexture,
        &p_CreateInfo.OcclusionTexture,
        &p_CreateInfo.EmissiveTexture,
        &p_CreateInfo.SpecularTexture,
        &p_CreateInfo.SpecularColorTexture,
        &p_CreateInfo.DiffuseTransmissionTexture,
        &p_CreateInfo.DiffuseTransmissionColorTexture,
        &p_CreateInfo.ClearcoatTexture,
        &p_CreateInfo.ClearcoatRoughnessTexture,
        &p_CreateInfo.ClearcoatNormalTexture,
        &p_CreateInfo.SheenColorTexture,
        &p_CreateInfo.SheenRoughnessTexture,
        &p_CreateInfo.HeightTexture
    };

    bool Ready = true;

    for (const MaterialTexture* Texture : Textures) {
        if (TextureFailed(*Texture, p_Reason, p_Rejected)) {
            return false;
        }

        if (Texture->Target && !Texture->Target.IsReady()) {
            Ready = false;
        }
    }

    return Ready;
}

void SetTexture(MaterialData& p_Data, uint32_t p_Slot, const MaterialTexture& p_Source, uint32_t& p_TextureIndex, uint32_t& p_SamplerIndex)
{
    if (!p_Source.Target) {
        p_TextureIndex = NO_TEXTURE;
        p_SamplerIndex = 0;

        return;
    }

    const std::shared_ptr<const ::Engine::Applied::Render::Resources::Textures::TextureSnapshot> Snapshot =
        p_Source.Target->GetSnapshot();

    if (!Snapshot) {
        p_TextureIndex = NO_TEXTURE;
        p_SamplerIndex = 0;

        return;
    }

    p_TextureIndex = Snapshot->Index;
    p_SamplerIndex = ::Engine::Base::Render::Vk::Samplers::Make(p_Source.Sampler);

    ::Engine::Applied::Render::Resources::Textures::SyncSamplers();
    WriteUvTransform(p_Source, p_Data.UvTransforms[p_Slot * 2], p_Data.UvTransforms[p_Slot * 2 + 1]);
}

}

Material::Material(MaterialCreateInfo p_CreateInfo)
    : Resource(MakeKey(p_CreateInfo))
    , m_CreateInfo(std::move(p_CreateInfo))
    , LoadState(std::make_shared<LoadData>())
{
}

Material::~Material()
{
    std::erase(m_PendingGpuMaterials, this);
}

std::string Material::GetTypeName() const
{
    return "Material";
}

void Material::BeginLoad()
{
    std::erase(m_PendingGpuMaterials, this);

    ReleaseIndex(Index);
    Index = UINT32_MAX;

    AdvanceVersionChecksum();

    LoadState = std::make_shared<LoadData>();
    m_GpuTicket.reset();
    Index = UINT32_MAX;

    if (!m_CreateInfo.TargetShader) {
        SetFailed("material requires Shader Ref");

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

    if (m_GpuTicket) {
        if (!m_GpuTicket->Done.load(std::memory_order_acquire)) {
            SetState(::Engine::Util::Applied::ResourceState::Uploading);

            return;
        }

        if (m_GpuTicket->Rejected.load(std::memory_order_acquire)) {
            SetRejected(m_GpuTicket->FailureReason);

            return;
        }

        if (m_GpuTicket->Failed.load(std::memory_order_acquire)) {
            SetFailed(m_GpuTicket->FailureReason);

            return;
        }

        SetState(::Engine::Util::Applied::ResourceState::Ready);

        const std::shared_ptr<MaterialSnapshot> Snapshot = std::make_shared<MaterialSnapshot>();

        Snapshot->Data = Data;
        Snapshot->Index = Index;
        Snapshot->VersionChecksum = GetVersionChecksum();

        m_Snapshot.store(
            std::shared_ptr<const MaterialSnapshot>(Snapshot),
            std::memory_order_release
        );

        return;
    }

    if (m_CreateInfo.TargetShader.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
        SetFailed("Shader dependency failed: " + m_CreateInfo.TargetShader->GetFailureReason());

        return;
    }

    if (m_CreateInfo.TargetShader.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        SetRejected("Shader dependency rejected: " + m_CreateInfo.TargetShader->GetFailureReason());

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
            SetRejected("material CPU job version is stale");

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

    SetTexture(Data, 0, m_CreateInfo.BaseColorTexture, Data.BaseColorTexture, Data.BaseColorSampler);
    SetTexture(Data, 1, m_CreateInfo.MetallicRoughnessTexture, Data.MetallicRoughnessTexture, Data.MetallicRoughnessSampler);
    SetTexture(Data, 2, m_CreateInfo.NormalTexture, Data.NormalTexture, Data.NormalSampler);
    SetTexture(Data, 3, m_CreateInfo.OcclusionTexture, Data.OcclusionTexture, Data.OcclusionSampler);
    SetTexture(Data, 4, m_CreateInfo.EmissiveTexture, Data.EmissiveTexture, Data.EmissiveSampler);
    SetTexture(Data, 5, m_CreateInfo.SpecularTexture, Data.SpecularTexture, Data.SpecularSampler);
    SetTexture(Data, 6, m_CreateInfo.SpecularColorTexture, Data.SpecularColorTexture, Data.SpecularColorSampler);
    SetTexture(Data, 7, m_CreateInfo.DiffuseTransmissionTexture, Data.DiffuseTransmissionTexture, Data.DiffuseTransmissionSampler);
    SetTexture(Data, 8, m_CreateInfo.DiffuseTransmissionColorTexture, Data.DiffuseTransmissionColorTexture, Data.DiffuseTransmissionColorSampler);
    SetTexture(Data, 9, m_CreateInfo.ClearcoatTexture, Data.ClearcoatTexture, Data.ClearcoatSampler);
    SetTexture(Data, 10, m_CreateInfo.ClearcoatRoughnessTexture, Data.ClearcoatRoughnessTexture, Data.ClearcoatRoughnessSampler);
    SetTexture(Data, 11, m_CreateInfo.ClearcoatNormalTexture, Data.ClearcoatNormalTexture, Data.ClearcoatNormalSampler);
    SetTexture(Data, 12, m_CreateInfo.SheenColorTexture, Data.SheenColorTexture, Data.SheenColorSampler);
    SetTexture(Data, 13, m_CreateInfo.SheenRoughnessTexture, Data.SheenRoughnessTexture, Data.SheenRoughnessSampler);

    if (m_CreateInfo.HeightTexture.Target) {
        const std::shared_ptr<const ::Engine::Applied::Render::Resources::Textures::TextureSnapshot> Snapshot =
            m_CreateInfo.HeightTexture.Target->GetSnapshot();
        if (!Snapshot) {
            return;
        }

        Data.HeightTexture = Snapshot->Index;
        Data.HeightSampler = ::Engine::Base::Render::Vk::Samplers::Make(m_CreateInfo.HeightTexture.Sampler);
        ::Engine::Applied::Render::Resources::Textures::SyncSamplers();
    }

    Index = TakeIndex();

    {
        std::lock_guard<std::mutex> Lock(m_RegistryMutex);
        m_Data[Index] = Data;
        ++m_Version;
    }

    m_PendingGpuMaterials.push_back(this);
    SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);
}

void Material::Unload()
{
    std::erase(m_PendingGpuMaterials, this);

    AdvanceVersionChecksum();

    if (GetState() == ::Engine::Util::Applied::ResourceState::Unloading) {
        return;
    }

    SetState(::Engine::Util::Applied::ResourceState::Unloading);

    ReleaseIndex(Index);
    Index = UINT32_MAX;
}

void Material::SetGpuTicket(
    std::shared_ptr<::Engine::Base::Render::ResourceLoader::ResourceJobTicket> p_Ticket
)
{
    m_GpuTicket = std::move(p_Ticket);
}

std::shared_ptr<const MaterialSnapshot> Material::GetSnapshot() const
{
    return m_Snapshot.load(std::memory_order_acquire);
}

void CaptureFrameSnapshot()
{
    m_FrameBufferSnapshot = m_BufferSnapshot.load(std::memory_order_acquire);
}

uint32_t Material::GetIndex() const { return Index; }

const MaterialData& Material::GetData() const { return Data; }

bool Material::IsDoubleSided() const { return m_CreateInfo.DoubleSided; }
bool Material::CastsShadows() const { return m_CreateInfo.CastShadows; }

AlphaMode Material::GetAlphaMode() const { return m_CreateInfo.Alpha; }

const ::Engine::Util::Applied::ResourceHandle<
    ::Engine::Applied::Render::Resources::Shaders::Shader>& Material::GetShader() const { return m_CreateInfo.TargetShader; }

VkDescriptorSet Material::GetExtraSet() const { return m_CreateInfo.ExtraSet; }

void Init()
{
    ::Engine::Base::Render::RenderingPool::Register("scene_materials_flush", Flush, 900);
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot(
        "scene_materials_snapshot",
        CaptureFrameSnapshot,
        900
    );

    for (uint32_t I = 0; I < ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT; ++I) {
        m_BufferCreateTickets[I] = ::Engine::Base::Render::ResourceLoader::PushFrameJobVk(
            [I](VkCommandBuffer) {
                m_Buffers[I] = ::Engine::Base::Render::ResourceLoader::CreateBuffer(
                    sizeof(MaterialData) * ::Engine::Configs::Static::Graphics::MAX_MATERIALS,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                );
            },
            ::Engine::Configs::Static::ResourceLoader::GPU_OPS_HIGH_ORDER,
            { 0, 0, 1, 0, 0 }
        );
    }
}

void Cleanup()
{
    m_PendingGpuMaterials.clear();

    for (auto& Buffer : m_Buffers) {
        ::Engine::Base::Render::ResourceLoader::DestroyBuffer(Buffer);
    }

    std::lock_guard<std::mutex> Lock(m_RegistryMutex);

    m_Data.clear();
    m_FreeIndices.clear();
    m_ErasedMaterials.clear();
    m_FrameBufferSnapshot.reset();
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

    const uint32_t Frame = ::Engine::Base::Render::Vk::Frame::GetCurrentFrameIndex();
    if (m_BufferTickets[Frame]
        && m_BufferTickets[Frame]->Done.load(std::memory_order_acquire)
        && !m_BufferTickets[Frame]->Failed.load(std::memory_order_acquire)
        && !m_BufferTickets[Frame]->Rejected.load(std::memory_order_acquire)) {

        const std::shared_ptr<MaterialBufferSnapshot> Snapshot =
            std::make_shared<MaterialBufferSnapshot>();

        Snapshot->Buffer = m_Buffers[Frame];
        Snapshot->Address = m_Buffers[Frame].Address;
        Snapshot->Version = m_BufferVersions[Frame];

        m_BufferSnapshot.store(
            std::shared_ptr<const MaterialBufferSnapshot>(Snapshot),
            std::memory_order_release
        );
    }

    if (!m_BufferCreateTickets[Frame]
        || !m_BufferCreateTickets[Frame]->Done.load(std::memory_order_acquire)) {

        return;
    }

    if (m_BufferCreateTickets[Frame]->Rejected.load(std::memory_order_acquire)) {
        for (Material* MaterialValue : m_PendingGpuMaterials) {
            MaterialValue->SetRejected(m_BufferCreateTickets[Frame]->FailureReason);
        }

        m_PendingGpuMaterials.clear();

        return;
    }

    if (m_BufferCreateTickets[Frame]->Failed.load(std::memory_order_acquire)) {
        for (Material* MaterialValue : m_PendingGpuMaterials) {
            MaterialValue->SetFailed(m_BufferCreateTickets[Frame]->FailureReason);
        }

        m_PendingGpuMaterials.clear();

        return;
    }

    if (m_BufferVersions[Frame] == m_Version || m_Data.empty()) {
        return;
    }

    const ::Engine::Base::Render::Buffer Buffer = m_Buffers[Frame];

    m_BufferTickets[Frame] = ::Engine::Base::Render::ResourceLoader::PushFrameJobVk(
        [Buffer](VkCommandBuffer) {
            std::vector<MaterialData> Data;

            {
                std::lock_guard<std::mutex> Lock(m_RegistryMutex);
                Data = m_Data;
            }

            ::Engine::Base::Render::ResourceLoader::QueueBufferUpload(
                Buffer,
                Data.data(),
                sizeof(MaterialData) * Data.size()
            );
        },
        ::Engine::Configs::Static::ResourceLoader::GPU_OPS_NORMAL_ORDER,
        {
            sizeof(MaterialData) * m_Data.size(),
            1,
            0,
            0,
            1
        }
    );

    for (Material* MaterialValue : m_PendingGpuMaterials) {
        MaterialValue->SetGpuTicket(m_BufferTickets[Frame]);
    }
    m_PendingGpuMaterials.clear();

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

std::shared_ptr<const MaterialBufferSnapshot> GetBufferSnapshot()
{
    return m_FrameBufferSnapshot;
}

}
