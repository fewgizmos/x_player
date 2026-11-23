#include "X/Engine/Applied/Render/Resources/Models.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Engine::Applied::Render::Resources::Models
{

struct Model::LoadData
{
    std::mutex Mutex;

    std::vector<ModelPart> Parts;
    std::string FailureReason;

    bool Finished = false;
    bool Failed = false;
};

namespace
{


std::vector<uint32_t> BuildTriangleIndices(const fastgltf::Asset& p_Asset, const fastgltf::Primitive& p_Primitive)
{
    std::vector<uint32_t> Source;

    if (p_Primitive.indicesAccessor.has_value()) {
        const fastgltf::Accessor& Accessor = p_Asset.accessors[p_Primitive.indicesAccessor.value()];
        Source.reserve(Accessor.count);

        fastgltf::iterateAccessor<uint32_t>(p_Asset, Accessor, [&Source](uint32_t p_Index) {
            Source.push_back(p_Index);
        });
    } else {
        const auto PositionAttribute = p_Primitive.findAttribute("POSITION");

        if (PositionAttribute == p_Primitive.attributes.end()) {
            throw std::runtime_error("model primitive has no POSITION attribute");
        }

        const fastgltf::Accessor& Positions =
            p_Asset.accessors[PositionAttribute->accessorIndex];
        Source.resize(Positions.count);

        for (std::size_t Index = 0; Index < Source.size(); ++Index) {
            Source[Index] = static_cast<uint32_t>(Index);
        }
    }

    if (p_Primitive.type == fastgltf::PrimitiveType::Triangles) {
        if (Source.size() % 3 != 0) {
            throw std::runtime_error("triangle primitive index count is not divisible by three");
        }

        return Source;
    }

    std::vector<uint32_t> Result;
    Result.reserve(Source.size() * 3);

    if (p_Primitive.type == fastgltf::PrimitiveType::TriangleStrip) {
        for (std::size_t Index = 2; Index < Source.size(); ++Index) {
            if ((Index & 1u) == 0) {
                Result.insert(Result.end(), { Source[Index - 2], Source[Index - 1], Source[Index] });
            } else {
                Result.insert(Result.end(), { Source[Index - 1], Source[Index - 2], Source[Index] });
            }
        }

        return Result;
    }

    if (p_Primitive.type == fastgltf::PrimitiveType::TriangleFan) {
        for (std::size_t Index = 2; Index < Source.size(); ++Index) {
            Result.insert(Result.end(), { Source[0], Source[Index - 1], Source[Index] });
        }

        return Result;
    }

    throw std::runtime_error("unsupported model primitive topology");
}

template<typename TextureInfoType>
void SetMaterialTexture(
    const fastgltf::Asset& p_Asset,
    const std::optional<TextureInfoType>& p_Info,
    const std::filesystem::path& p_ModelPath,
    Materials::MaterialTexture& p_Target
)
{
    if (!p_Info.has_value()) {
        return;
    }

    const std::size_t TextureIndex = p_Info->textureIndex;

    if (TextureIndex >= p_Asset.textures.size()) {
        return;
    }

    const fastgltf::Texture& Texture = p_Asset.textures[TextureIndex];

    if (!Texture.imageIndex.has_value() || Texture.imageIndex.value() >= p_Asset.images.size()) {
        return;
    }

    const fastgltf::Image& Image = p_Asset.images[Texture.imageIndex.value()];
    const fastgltf::sources::URI* Source = std::get_if<fastgltf::sources::URI>(&Image.data);

    if (Source == nullptr) {
        return;
    }

    const std::filesystem::path TexturePath =
        std::filesystem::absolute(p_ModelPath.parent_path() / Source->uri.fspath()).lexically_normal();

    p_Target.Target = ::Engine::Util::Applied::ResourceHandle<
        ::Engine::Applied::Render::Resources::Textures::Texture>(
            ::Engine::Applied::Render::Resources::Textures::TextureCreateInfo{
                TexturePath.string(),
                VK_FORMAT_R8G8B8A8_SRGB
            }
        );

    if (p_Info->transform) {
        p_Target.UvOffset = glm::vec2(
            p_Info->transform->uvOffset[0],
            p_Info->transform->uvOffset[1]
        );

        p_Target.UvScale = glm::vec2(
            p_Info->transform->uvScale[0],
            p_Info->transform->uvScale[1]
        );

        p_Target.UvRotation = p_Info->transform->rotation;
    }

    if (!Texture.samplerIndex.has_value() || Texture.samplerIndex.value() >= p_Asset.samplers.size()) {
        return;
    }

    const fastgltf::Sampler& Sampler = p_Asset.samplers[Texture.samplerIndex.value()];

    if (Sampler.magFilter.has_value()) {
        p_Target.Sampler.MagFilter = Sampler.magFilter.value() == fastgltf::Filter::Nearest
            ? VK_FILTER_NEAREST
            : VK_FILTER_LINEAR;
    }

    if (Sampler.minFilter.has_value()) {
        p_Target.Sampler.MinFilter =
            Sampler.minFilter.value() == fastgltf::Filter::Nearest
            || Sampler.minFilter.value() == fastgltf::Filter::NearestMipMapNearest
            || Sampler.minFilter.value() == fastgltf::Filter::NearestMipMapLinear
            ? VK_FILTER_NEAREST
            : VK_FILTER_LINEAR;

        p_Target.Sampler.MipMode =
            Sampler.minFilter.value() == fastgltf::Filter::NearestMipMapNearest
            || Sampler.minFilter.value() == fastgltf::Filter::LinearMipMapNearest
            ? VK_SAMPLER_MIPMAP_MODE_NEAREST
            : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    const auto ConvertWrap = [](fastgltf::Wrap p_Wrap) {
        switch (p_Wrap) {
        case fastgltf::Wrap::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case fastgltf::Wrap::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case fastgltf::Wrap::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    p_Target.Sampler.WrapU = ConvertWrap(Sampler.wrapS);
    p_Target.Sampler.WrapV = ConvertWrap(Sampler.wrapT);
}

Meshes::MeshCreateInfo BuildMesh(const fastgltf::Asset& p_Asset, const fastgltf::Primitive& p_Primitive)
{
    const auto PositionAttribute = p_Primitive.findAttribute("POSITION");

    if (PositionAttribute == p_Primitive.attributes.end()) {
        throw std::runtime_error("model primitive has no POSITION attribute");
    }

    const fastgltf::Accessor& Positions = p_Asset.accessors[PositionAttribute->accessorIndex];

    Meshes::MeshCreateInfo Result;
    Result.Vertices.resize(Positions.count);

    for (Meshes::Vertex& Vertex : Result.Vertices) {
        Vertex = {};
        Vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        Vertex.Color = glm::vec4(1.0f);
        Vertex.Joints = glm::uvec4(0u);
        Vertex.Weights = glm::vec4(0.0f);
        Vertex.Weights.x = 1.0f;
    }

    fastgltf::iterateAccessorWithIndex<glm::vec3>(p_Asset, Positions,
        [&Result](glm::vec3 p_Value, std::size_t p_Index) {
            Result.Vertices[p_Index].Position = p_Value;
            Result.Vertices[p_Index].Color = glm::vec4(1.0f);
        }
    );

    const auto NormalAttribute = p_Primitive.findAttribute("NORMAL");

    if (NormalAttribute != p_Primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(p_Asset,
            p_Asset.accessors[NormalAttribute->accessorIndex],
            [&Result](glm::vec3 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].Normal = p_Value;
            }
        );
    }

    const auto UvAttribute = p_Primitive.findAttribute("TEXCOORD_0");

    if (UvAttribute != p_Primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(p_Asset,
            p_Asset.accessors[UvAttribute->accessorIndex],
            [&Result](glm::vec2 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].UvX = p_Value.x;
                Result.Vertices[p_Index].UvY = p_Value.y;
            }
        );
    }

    const auto TangentAttribute = p_Primitive.findAttribute("TANGENT");

    if (TangentAttribute != p_Primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(p_Asset,
            p_Asset.accessors[TangentAttribute->accessorIndex],
            [&Result](glm::vec4 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].Tangent = p_Value;
            }
        );
    }

    const auto ColorAttribute = p_Primitive.findAttribute("COLOR_0");

    if (ColorAttribute != p_Primitive.attributes.end()) {
        const fastgltf::Accessor& Colors = p_Asset.accessors[ColorAttribute->accessorIndex];

        if (Colors.type == fastgltf::AccessorType::Vec4) {
            fastgltf::iterateAccessorWithIndex<glm::vec4>(p_Asset, Colors,
                [&Result](glm::vec4 p_Value, std::size_t p_Index) {
                    Result.Vertices[p_Index].Color = p_Value;
                }
            );
        } else if (Colors.type == fastgltf::AccessorType::Vec3) {
            fastgltf::iterateAccessorWithIndex<glm::vec3>(p_Asset, Colors,
                [&Result](glm::vec3 p_Value, std::size_t p_Index) {
                    Result.Vertices[p_Index].Color = glm::vec4(p_Value, 1.0f);
                }
            );
        }
    }

    const auto JointsAttribute = p_Primitive.findAttribute("JOINTS_0");

    if (JointsAttribute != p_Primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::uvec4>(
            p_Asset,
            p_Asset.accessors[JointsAttribute->accessorIndex],
            [&Result](glm::uvec4 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].Joints = p_Value;
            }
        );
    }

    const auto WeightsAttribute = p_Primitive.findAttribute("WEIGHTS_0");

    if (WeightsAttribute != p_Primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            p_Asset,
            p_Asset.accessors[WeightsAttribute->accessorIndex],
            [&Result](glm::vec4 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].Weights = p_Value;
            }
        );
    }

    Result.Indices = BuildTriangleIndices(p_Asset, p_Primitive);

    return Result;
}

Meshes::MeshSkeletonCreateInfo BuildSkeleton(
    const fastgltf::Asset& p_Asset,
    const fastgltf::Skin& p_Skin
)
{
    Meshes::MeshSkeletonCreateInfo Result;
    Result.ParentIndices.resize(p_Skin.joints.size(), -1);
    Result.InverseBindMatrices.resize(p_Skin.joints.size(), glm::mat4(1.0f));

    for (std::size_t JointIndex = 0; JointIndex < p_Skin.joints.size(); ++JointIndex) {
        const std::size_t JointNode = p_Skin.joints[JointIndex];

        if (JointNode >= p_Asset.nodes.size()) {
            throw std::runtime_error("skin joint node index is out of range");
        }

        for (std::size_t ParentIndex = 0; ParentIndex < p_Skin.joints.size(); ++ParentIndex) {
            const std::size_t ParentNode = p_Skin.joints[ParentIndex];

            if (ParentNode >= p_Asset.nodes.size()) {
                throw std::runtime_error("skin parent node index is out of range");
            }

            if (std::find(
                    p_Asset.nodes[ParentNode].children.begin(),
                    p_Asset.nodes[ParentNode].children.end(),
                    JointNode
                ) != p_Asset.nodes[ParentNode].children.end()) {

                Result.ParentIndices[JointIndex] = static_cast<int32_t>(ParentIndex);

                break;
            }
        }
    }

    if (p_Skin.inverseBindMatrices.has_value()) {
        fastgltf::iterateAccessorWithIndex<glm::mat4>(
            p_Asset,
            p_Asset.accessors[p_Skin.inverseBindMatrices.value()],
            [&Result](glm::mat4 p_Value, std::size_t p_Index) {
                if (p_Index < Result.InverseBindMatrices.size()) {
                    Result.InverseBindMatrices[p_Index] = p_Value;
                }
            }
        );
    }

    return Result;
}

}

Model::Model(CreateInfo p_CreateInfo)
    : Resource(p_CreateInfo.GltfPath + ":" + (p_CreateInfo.Shader ? p_CreateInfo.Shader->GetKey() : std::string()))
    , m_CreateInfo(std::move(p_CreateInfo))
    , m_LoadData(std::make_shared<LoadData>())
{
}

Model::~Model() = default;

std::string Model::GetTypeName() const
{
    return "Model";
}

void Model::BeginLoad()
{
    AdvanceVersionChecksum();

    m_LoadData = std::make_shared<LoadData>();
    m_Parts.clear();
    m_Snapshot.reset();

    const std::shared_ptr<LoadData> Data = m_LoadData;
    const CreateInfo Info = m_CreateInfo;

    ::Engine::Base::JobSystem::It::PushBackground(
        ::Engine::Base::Runtime::EngineThreadID::RenderResourceLoaderTh,
        "model_load_" + GetKey(),
        [Data, Info] {
            try {
                const std::filesystem::path Path(Info.GltfPath);

                fastgltf::Parser Parser(::Engine::Configs::Static::Graphics::PARSER_EXTENSIONS);
                auto File = fastgltf::GltfDataBuffer::FromPath(Path);

                if (File.error() != fastgltf::Error::None) {
                    throw std::runtime_error(
                        "failed to read gltf model: "
                        + std::string(fastgltf::getErrorMessage(File.error()))
                    );
                }

                auto Loaded = Parser.loadGltf(
                    File.get(), Path.parent_path(), fastgltf::Options::LoadExternalBuffers
                );

                if (Loaded.error() != fastgltf::Error::None) {
                    throw std::runtime_error(
                        "failed to parse gltf model: "
                        + std::string(fastgltf::getErrorMessage(Loaded.error()))
                    );
                }

                const fastgltf::Asset& Asset = Loaded.get();
                std::vector<::Engine::Util::Applied::ResourceHandle<Materials::Material>> MaterialHandles;

                MaterialHandles.reserve(Asset.materials.size());

                for (const fastgltf::Material& SourceMaterial : Asset.materials) {
                    Materials::MaterialCreateInfo MaterialInfo;
                    MaterialInfo.TargetShader = Info.Shader;

                    MaterialInfo.BaseColor = glm::vec4(
                        SourceMaterial.pbrData.baseColorFactor[0],
                        SourceMaterial.pbrData.baseColorFactor[1],
                        SourceMaterial.pbrData.baseColorFactor[2],
                        SourceMaterial.pbrData.baseColorFactor[3]
                    );

                    MaterialInfo.Metallic = SourceMaterial.pbrData.metallicFactor;
                    MaterialInfo.Roughness = SourceMaterial.pbrData.roughnessFactor;

                    MaterialInfo.Emissive = glm::vec3(
                        SourceMaterial.emissiveFactor[0],
                        SourceMaterial.emissiveFactor[1],
                        SourceMaterial.emissiveFactor[2]
                    );

                    SetMaterialTexture(
                        Asset,
                        SourceMaterial.pbrData.baseColorTexture,
                        Path,
                        MaterialInfo.BaseColorTexture
                    );

                    SetMaterialTexture(
                        Asset,
                        SourceMaterial.pbrData.metallicRoughnessTexture,
                        Path,
                        MaterialInfo.MetallicRoughnessTexture
                    );

                    SetMaterialTexture(
                        Asset,
                        SourceMaterial.normalTexture,
                        Path,
                        MaterialInfo.NormalTexture
                    );

                    SetMaterialTexture(
                        Asset,
                        SourceMaterial.occlusionTexture,
                        Path,
                        MaterialInfo.OcclusionTexture
                    );

                    SetMaterialTexture(
                        Asset,
                        SourceMaterial.emissiveTexture,
                        Path,
                        MaterialInfo.EmissiveTexture
                    );

                    MaterialInfo.Alpha = SourceMaterial.alphaMode == fastgltf::AlphaMode::Opaque
                        ? Materials::AlphaMode::Opaque
                        : Materials::AlphaMode::Mask;

                    MaterialInfo.AlphaCutoff = SourceMaterial.alphaCutoff;
                    MaterialInfo.DoubleSided = SourceMaterial.doubleSided;

                    MaterialHandles.push_back(
                        ::Engine::Util::Applied::ResourceHandle<Materials::Material>(std::move(MaterialInfo))
                    );
                }

                if (MaterialHandles.empty()) {
                    Materials::MaterialCreateInfo DefaultMaterial;

                    DefaultMaterial.TargetShader = Info.Shader;
                    MaterialHandles.push_back(
                        ::Engine::Util::Applied::ResourceHandle<Materials::Material>(
                            std::move(DefaultMaterial)
                        )
                    );
                }

                std::vector<glm::mat4> NodeWorldTransforms(
                    Asset.nodes.size(), glm::mat4(1.0f)
                );

                std::vector<glm::mat4> MeshWorldTransforms(
                    Asset.meshes.size(), glm::mat4(1.0f)
                );

                std::vector<std::vector<glm::mat4>> MeshJointMatrices(Asset.meshes.size());

                if (!Asset.scenes.empty()) {
                    std::function<void(std::size_t, const glm::mat4&)> BuildNodeTransforms;

                    BuildNodeTransforms = [&BuildNodeTransforms, &Asset, &NodeWorldTransforms](
                        std::size_t p_NodeIndex, const glm::mat4& p_ParentTransform
                    ) {
                        if (p_NodeIndex >= Asset.nodes.size()) {
                            throw std::runtime_error("scene node index is out of range");
                        }

                        const fastgltf::Node& Node = Asset.nodes[p_NodeIndex];
                        const glm::mat4 LocalTransform = glm::make_mat4(
                            fastgltf::getTransformMatrix(Node).data()
                        );

                        const glm::mat4 WorldTransform = p_ParentTransform * LocalTransform;
                        NodeWorldTransforms[p_NodeIndex] = WorldTransform;

                        for (const std::size_t Child : Node.children) {
                            BuildNodeTransforms(Child, WorldTransform);
                        }
                    };

                    const std::size_t SceneIndex = Asset.defaultScene.value_or(0);

                    if (SceneIndex >= Asset.scenes.size()) {
                        throw std::runtime_error("default scene index is out of range");
                    }

                    for (const std::size_t Root : Asset.scenes[SceneIndex].nodeIndices) {
                        BuildNodeTransforms(Root, glm::mat4(1.0f));
                    }

                    for (std::size_t NodeIndex = 0; NodeIndex < Asset.nodes.size(); ++NodeIndex) {
                        const fastgltf::Node& Node = Asset.nodes[NodeIndex];

                        if (!Node.meshIndex.has_value() || !Node.skinIndex.has_value()) {
                            continue;
                        }

                        const std::size_t MeshIndex = Node.meshIndex.value();
                        const std::size_t SkinIndex = Node.skinIndex.value();

                        if (MeshIndex >= Asset.meshes.size() || SkinIndex >= Asset.skins.size()) {
                            throw std::runtime_error("node mesh or skin index is out of range");
                        }

                        if (!MeshJointMatrices[MeshIndex].empty()) {
                            continue;
                        }

                        const Meshes::MeshSkeletonCreateInfo SkeletonInfo = BuildSkeleton(
                            Asset,
                            Asset.skins[SkinIndex]
                        );

                        MeshWorldTransforms[MeshIndex] = NodeWorldTransforms[NodeIndex];
                        const glm::mat4 MeshInverse = glm::inverse(MeshWorldTransforms[MeshIndex]);
                        MeshJointMatrices[MeshIndex].resize(SkeletonInfo.InverseBindMatrices.size());

                        for (std::size_t JointIndex = 0;
                            JointIndex < Asset.skins[SkinIndex].joints.size();

                            ++JointIndex) {
                                const std::size_t JointNode = Asset.skins[SkinIndex].joints[JointIndex];
                                MeshJointMatrices[MeshIndex][JointIndex] = MeshInverse
                                    * NodeWorldTransforms[JointNode]
                                    * SkeletonInfo.InverseBindMatrices[JointIndex];
                        }
                    }
                }

                std::vector<std::vector<std::size_t>> PartIndices(Asset.meshes.size());

                for (std::size_t MeshIndex = 0; MeshIndex < Asset.meshes.size(); ++MeshIndex) {
                    const fastgltf::Mesh& SourceMesh = Asset.meshes[MeshIndex];

                    for (const fastgltf::Primitive& Primitive : SourceMesh.primitives) {
                        Meshes::MeshCreateInfo MeshInfo = BuildMesh(Asset, Primitive);
                        std::vector<Meshes::MeshMorphTargetCreateInfo> MorphInfos;
                        MorphInfos.reserve(Primitive.targets.size());

                        for (std::size_t TargetIndex = 0; TargetIndex < Primitive.targets.size(); ++TargetIndex) {
                            Meshes::MeshMorphTargetCreateInfo TargetInfo;
                            TargetInfo.Name = "MorphTarget_" + std::to_string(TargetIndex);

                            if (TargetIndex < SourceMesh.weights.size()) {
                                TargetInfo.Weight = static_cast<float>(SourceMesh.weights[TargetIndex]);
                            }

                            const auto& Target = Primitive.targets[TargetIndex];

                            for (const fastgltf::Attribute& Attribute : Target) {
                                if (Attribute.name == "POSITION") {
                                    TargetInfo.PositionDeltas.resize(
                                        Asset.accessors[Attribute.accessorIndex].count
                                    );

                                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                        Asset,
                                        Asset.accessors[Attribute.accessorIndex],
                                        [&TargetInfo](glm::vec3 p_Value, std::size_t p_Index) {
                                            TargetInfo.PositionDeltas[p_Index] = p_Value;
                                        }
                                    );
                                }

                                if (Attribute.name == "NORMAL") {
                                    TargetInfo.NormalDeltas.resize(
                                        Asset.accessors[Attribute.accessorIndex].count
                                    );

                                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                                        Asset,
                                        Asset.accessors[Attribute.accessorIndex],
                                        [&TargetInfo](glm::vec3 p_Value, std::size_t p_Index) {
                                            TargetInfo.NormalDeltas[p_Index] = p_Value;
                                        }
                                    );
                                }
                            }

                            if (!TargetInfo.PositionDeltas.empty()
                                && TargetInfo.PositionDeltas.size() != MeshInfo.Vertices.size()) {
                                throw std::runtime_error(
                                    "morph target position count does not match mesh vertex count"
                                );
                            }

                            if (!TargetInfo.NormalDeltas.empty()
                                && TargetInfo.NormalDeltas.size() != MeshInfo.Vertices.size()) {
                                throw std::runtime_error(
                                    "morph target normal count does not match mesh vertex count"
                                );
                            }

                            MorphInfos.push_back(std::move(TargetInfo));
                        }

                        for (const Meshes::MeshMorphTargetCreateInfo& MorphInfo : MorphInfos) {
                            if (MorphInfo.Weight == 0.0f) {
                                continue;
                            }

                            for (std::size_t VertexIndex = 0; VertexIndex < MeshInfo.Vertices.size(); ++VertexIndex) {
                                if (VertexIndex < MorphInfo.PositionDeltas.size()) {
                                    MeshInfo.Vertices[VertexIndex].Position +=
                                        MorphInfo.PositionDeltas[VertexIndex] * MorphInfo.Weight;
                                }

                                if (VertexIndex < MorphInfo.NormalDeltas.size()) {
                                    MeshInfo.Vertices[VertexIndex].Normal +=
                                        MorphInfo.NormalDeltas[VertexIndex] * MorphInfo.Weight;
                                }
                            }
                        }

                        if (!MeshJointMatrices[MeshIndex].empty()) {
                            for (Meshes::Vertex& Vertex : MeshInfo.Vertices) {
                                glm::vec4 Position(0.0f);
                                glm::vec3 Normal(0.0f);
                                float WeightSum = 0.0f;

                                for (std::size_t Influence = 0; Influence < 4; ++Influence) {
                                    const uint32_t JointIndex = Vertex.Joints[Influence];
                                    const float Weight = Vertex.Weights[Influence];

                                    if (Weight <= 0.0f || JointIndex >= MeshJointMatrices[MeshIndex].size()) {
                                        continue;
                                    }

                                    const glm::mat4& JointMatrix = MeshJointMatrices[MeshIndex][JointIndex];

                                    Position += JointMatrix * glm::vec4(Vertex.Position, 1.0f) * Weight;
                                    Normal += glm::mat3(glm::transpose(glm::inverse(JointMatrix)))
                                        * Vertex.Normal * Weight;
                                    WeightSum += Weight;
                                }

                                if (WeightSum > 0.0f) {
                                    Vertex.Position = glm::vec3(Position / WeightSum);

                                    if (glm::dot(Normal, Normal) > 0.0f) {
                                        Vertex.Normal = glm::normalize(Normal);
                                    }
                                }
                            }
                        }

                        for (Meshes::Vertex& Vertex : MeshInfo.Vertices) {
                            if (glm::dot(Vertex.Normal, Vertex.Normal) > 0.0f) {
                                Vertex.Normal = glm::normalize(Vertex.Normal);
                            }
                        }

                        ModelPart Part;

                        Part.Mesh = ::Engine::Util::Applied::ResourceHandle<Meshes::Mesh>(std::move(MeshInfo));

                        if (Primitive.materialIndex.has_value()
                            && Primitive.materialIndex.value() < MaterialHandles.size()) {
                            Part.Material = MaterialHandles[Primitive.materialIndex.value()];
                        } else {
                            Part.Material = MaterialHandles[0];
                        }

                        for (Meshes::MeshMorphTargetCreateInfo& TargetInfo : MorphInfos) {
                            Part.MorphTargets.push_back(
                                ::Engine::Util::Applied::ResourceHandle<Meshes::MeshMorphTarget>(
                                    std::move(TargetInfo)
                                )
                            );
                            Part.MorphWeights.push_back(Part.MorphTargets.back()->GetData().Weight);
                        }

                        PartIndices[MeshIndex].push_back(Data->Parts.size());

                        Data->Parts.push_back(std::move(Part));
                    }
                }

                if (!Asset.scenes.empty()) {
                    const std::size_t SceneIndex = Asset.defaultScene.value_or(0);

                    if (SceneIndex >= Asset.scenes.size()) {
                        throw std::runtime_error("default scene index is out of range");
                    }

                    const fastgltf::Scene& Scene = Asset.scenes[SceneIndex];
                    std::function<void(std::size_t, const glm::mat4&)> VisitNode;

                    VisitNode = [&VisitNode, &Asset, &PartIndices, Data](
                        std::size_t p_NodeIndex, const glm::mat4& p_ParentTransform
                    ) {
                        const fastgltf::Node& Node = Asset.nodes[p_NodeIndex];
                        const glm::mat4 LocalTransform = glm::make_mat4(
                            fastgltf::getTransformMatrix(Node).data()
                        );

                        const glm::mat4 WorldTransform = p_ParentTransform * LocalTransform;

                        if (Node.meshIndex.has_value()) {
                            if (Node.meshIndex.value() >= PartIndices.size()) {
                                throw std::runtime_error("node mesh index is out of range");
                            }

                            for (const std::size_t PartIndex : PartIndices[Node.meshIndex.value()]) {
                                Data->Parts[PartIndex].PrivateTransform = WorldTransform;

                                if (Node.skinIndex.has_value()
                                    && Node.skinIndex.value() < Asset.skins.size()) {

                                    Meshes::MeshSkeletonCreateInfo SkeletonInfo = BuildSkeleton(
                                        Asset,
                                        Asset.skins[Node.skinIndex.value()]
                                    );

                                    Data->Parts[PartIndex].BindPoseMatrices = SkeletonInfo.InverseBindMatrices;
                                    Data->Parts[PartIndex].Skeleton =
                                        ::Engine::Util::Applied::ResourceHandle<Meshes::MeshSkeleton>(
                                            std::move(SkeletonInfo)
                                        );
                                }
                            }
                        }

                        for (const std::size_t Child : Node.children) {
                            VisitNode(Child, WorldTransform);
                        }
                    };

                    for (const std::size_t Root : Scene.nodeIndices) {
                        VisitNode(Root, glm::mat4(1.0f));
                    }
                }
            }

            catch (const std::exception& Error) {
                Data->FailureReason = Error.what();
                Data->Failed = true;
            }

            catch (...) {
                Data->FailureReason = "unknown model loading failure";
                Data->Failed = true;
            }

            std::lock_guard<std::mutex> Lock(Data->Mutex);
            Data->Finished = true;
        }
    );
}

void Model::Update()
{
    if (GetState() != ::Engine::Util::Applied::ResourceState::LoadingCpu
        && GetState() != ::Engine::Util::Applied::ResourceState::QueuedGpu) {

        return;
    }

    std::lock_guard<std::mutex> Lock(m_LoadData->Mutex);

    if (!m_LoadData->Finished) {
        return;
    }

    if (m_LoadData->Failed) {
        SetFailed(m_LoadData->FailureReason);

        return;
    }

    if (!m_CreateInfo.Shader) {
        SetFailed("model requires a shader resource");

        return;
    }

    if (m_CreateInfo.Shader.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
        SetFailed("model shader dependency failed: " + m_CreateInfo.Shader->GetFailureReason());

        return;
    }

    if (m_CreateInfo.Shader.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        SetRejected("model shader dependency rejected: " + m_CreateInfo.Shader->GetFailureReason());

        return;
    }

    for (const ModelPart& Part : m_LoadData->Parts) {
        if (!Part.Mesh || !Part.Material) {
            SetFailed("model part has no mesh or material");

            return;
        }

        if (Part.Material.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
            SetFailed("model material dependency failed: " + Part.Material->GetFailureReason());

            return;
        }

        if (Part.Material.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
            SetRejected("model material dependency rejected: " + Part.Material->GetFailureReason());

            return;
        }

        if (!Part.Mesh.IsReady() || !Part.Material.IsReady()) {
            SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);

            return;
        }

        if (Part.Skeleton) {
            if (Part.Skeleton.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
                SetFailed("model skeleton dependency failed");

                return;
            }

            if (Part.Skeleton.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
                SetRejected("model skeleton dependency rejected");

                return;
            }

            if (!Part.Skeleton.IsReady()) {
                SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);

                return;
            }

            if (Part.BindPoseMatrices.size() != Part.Skeleton->GetData().InverseBindMatrices.size()) {
                SetFailed("model skeleton bind pose data is incomplete");

                return;
            }
        }

        for (const ::Engine::Util::Applied::ResourceHandle<Meshes::MeshMorphTarget>& MorphTarget
            : Part.MorphTargets) {

            if (MorphTarget.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
                SetFailed("model morph target dependency failed");

                return;
            }

            if (MorphTarget.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
                SetRejected("model morph target dependency rejected");

                return;
            }

            if (!MorphTarget.IsReady()) {
                SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);

                return;
            }

            if (!MorphTarget->GetSnapshot()) {
                SetState(::Engine::Util::Applied::ResourceState::QueuedGpu);

                return;
            }
        }
    }

    m_Parts = std::move(m_LoadData->Parts);

    std::shared_ptr<ModelSnapshot> Snapshot = std::make_shared<ModelSnapshot>();
    Snapshot->Parts = m_Parts;
    Snapshot->VersionChecksum = GetVersionChecksum();

    m_Snapshot = std::move(Snapshot);

    SetState(::Engine::Util::Applied::ResourceState::Ready);
}

void Model::Unload()
{
    m_Snapshot.reset();
    m_Parts.clear();
    m_LoadData.reset();

    SetState(::Engine::Util::Applied::ResourceState::Unloading);
}

const std::vector<ModelPart>& Model::GetParts() const
{
    return m_Parts;
}

std::shared_ptr<const ModelSnapshot> Model::GetSnapshot() const
{
    return m_Snapshot;
}

const ::Engine::Util::Applied::ResourceHandle<
    ::Engine::Applied::Render::Resources::Shaders::Shader>& Model::GetShader() const
{
    return m_CreateInfo.Shader;
}

}
