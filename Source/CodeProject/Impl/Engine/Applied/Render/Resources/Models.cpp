#include "X/Engine/Applied/Render/Resources/Models.hpp"

#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Applied/Render/Resources/Textures.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Engine::Applied::Render::Resources::Models
{

struct Model::LoadData
{
    std::mutex Mutex;

    std::vector<ModelPart> Parts;
    std::vector<PunctualLight> Lights;
    std::string FailureReason;

    bool Finished = false;
    bool Failed = false;
};

namespace
{

std::string MakeKey(const ModelCreateInfo& p_Info)
{
    const std::string ShaderKey = p_Info.Shader ? p_Info.Shader->GetKey() : std::string();
    const std::string TransparentKey = p_Info.TransparentShader ? p_Info.TransparentShader->GetKey() : std::string();
    std::string Key = p_Info.GltfPath + ":" + std::to_string(ShaderKey.size()) + ":" + ShaderKey;
    Key += std::to_string(TransparentKey.size()) + ":" + TransparentKey;

    if (p_Info.LightShader) { Key += p_Info.LightShader->GetKey(); }

    return Key;
}

PunctualLight MakeLight(const fastgltf::Light& p_Light, const glm::mat4& p_Transform,
    const ::Engine::Util::Applied::ResourceHandle<Shaders::Shader>& p_Shader)
{
    LightPointCreateInfo Light;
    Light.Shader = p_Shader;

    switch (p_Light.type) {
    case fastgltf::LightType::Point:       Light.Type = LightType::Point;       break;
    case fastgltf::LightType::Spot:        Light.Type = LightType::Spot;        break;
    case fastgltf::LightType::Directional: Light.Type = LightType::Directional; break;
    }

    Light.Color          = glm::vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]);
    Light.Intensity      = p_Light.intensity;
    Light.Range          = p_Light.range.value_or(0.0f);
    Light.InnerConeAngle = p_Light.innerConeAngle.value_or(Light.InnerConeAngle);
    Light.OuterConeAngle = p_Light.outerConeAngle.value_or(Light.OuterConeAngle);
    return {::Engine::Util::Applied::ResourceHandle<LightPoint>(std::move(Light)), p_Transform};
}

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

        const fastgltf::Accessor& Positions = p_Asset.accessors[PositionAttribute->accessorIndex];

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

std::size_t GetTexCoordSet(const fastgltf::TextureInfo& p_Info)
{
    return p_Info.transform ? p_Info.transform->texCoordIndex.value_or(p_Info.texCoordIndex) : p_Info.texCoordIndex;
}

template<typename TextureInfoType>
void SetMaterialTexture(
    const fastgltf::Asset& p_Asset,
    const std::optional<TextureInfoType>& p_Info,
    const std::filesystem::path& p_ModelPath,
    Materials::MaterialTexture& p_Target,
    VkFormat p_Format = VK_FORMAT_R8G8B8A8_UNORM
)
{
    if (!p_Info.has_value()) { return; }

    const std::size_t TextureIndex = p_Info->textureIndex;

    if (TextureIndex >= p_Asset.textures.size()) { return; }

    const fastgltf::Texture& Texture = p_Asset.textures[TextureIndex];

    if (!Texture.imageIndex.has_value() || Texture.imageIndex.value() >= p_Asset.images.size()) { return; }

    const fastgltf::Image& Image = p_Asset.images[Texture.imageIndex.value()];
    Textures::TextureCreateInfo Info;
    Info.Format = p_Format;

    if (const auto* Source = std::get_if<fastgltf::sources::URI>(&Image.data)) {
        Info.Path = std::filesystem::absolute(p_ModelPath.parent_path() / Source->uri.fspath())
            .lexically_normal().string();

    } else {
        Info.Path = std::filesystem::absolute(p_ModelPath).lexically_normal().string() + "#image/" + std::to_string(Texture.imageIndex.value());

        if (const auto* View = std::get_if<fastgltf::sources::BufferView>(&Image.data)) {
            const auto Bytes = fastgltf::DefaultBufferDataAdapter{}(p_Asset, View->bufferViewIndex);

            Info.EncodedData = std::make_shared<const std::vector<std::byte>>(Bytes.begin(), Bytes.end());
        } else if (const auto* Array = std::get_if<fastgltf::sources::Array>(&Image.data)) {
            Info.EncodedData = std::make_shared<const std::vector<std::byte>>(Array->bytes.begin(), Array->bytes.end());
        } else if (const auto* View = std::get_if<fastgltf::sources::ByteView>(&Image.data)) {
            Info.EncodedData = std::make_shared<const std::vector<std::byte>>(View->bytes.begin(), View->bytes.end());
        } else { return; }
    }

    p_Target.Target = ::Engine::Util::Applied::ResourceHandle<Textures::Texture>(std::move(Info));
    p_Target.TexCoord = static_cast<uint32_t>(GetTexCoordSet(*p_Info));

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
            case fastgltf::Wrap::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case fastgltf::Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case fastgltf::Wrap::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    p_Target.Sampler.WrapU = ConvertWrap(Sampler.wrapS);
    p_Target.Sampler.WrapV = ConvertWrap(Sampler.wrapT);
}

void ReadMaterialTextures(const fastgltf::Asset& p_Asset, const fastgltf::Material& p_Material,
    const std::filesystem::path& p_Path, Materials::MaterialCreateInfo& p_Info)
{
    const auto Set = [&](const auto& p_Texture, Materials::MaterialTexture& p_Target,
        VkFormat p_Format = VK_FORMAT_R8G8B8A8_UNORM) {
        SetMaterialTexture(p_Asset, p_Texture, p_Path, p_Target, p_Format);
    };

    constexpr VkFormat Srgb = VK_FORMAT_R8G8B8A8_SRGB;
    Set(p_Material.pbrData.baseColorTexture, p_Info.BaseColorTexture, Srgb);
    Set(p_Material.pbrData.metallicRoughnessTexture, p_Info.MetallicRoughnessTexture);
    Set(p_Material.normalTexture, p_Info.NormalTexture);
    Set(p_Material.occlusionTexture, p_Info.OcclusionTexture);
    Set(p_Material.emissiveTexture, p_Info.EmissiveTexture, Srgb);
    Set(p_Material.packedNormalMetallicRoughnessTexture, p_Info.PackedNormalMetallicRoughnessTexture);

    if (p_Material.normalTexture)    { p_Info.NormalScale       = p_Material.normalTexture->scale; }
    if (p_Material.occlusionTexture) { p_Info.OcclusionStrength = p_Material.occlusionTexture->strength; }

    p_Info.EmissiveStrength = p_Material.emissiveStrength;
    p_Info.Ior = p_Material.ior;
    p_Info.Unlit = p_Material.unlit;

    if (const auto& Specular = p_Material.specular) {
        p_Info.SpecularFactor = Specular->specularFactor;
        p_Info.SpecularColor = glm::vec3(Specular->specularColorFactor[0], Specular->specularColorFactor[1], Specular->specularColorFactor[2]);

        Set(Specular->specularTexture, p_Info.SpecularTexture);
        Set(Specular->specularColorTexture, p_Info.SpecularColorTexture, Srgb);
    }

    if (const auto& Transmission = p_Material.transmission) {
        p_Info.TransmissionFactor = Transmission->transmissionFactor;

        Set(Transmission->transmissionTexture, p_Info.TransmissionTexture);
    }

    if (const auto& Volume = p_Material.volume) {
        p_Info.ThicknessFactor     = Volume->thicknessFactor;
        p_Info.AttenuationDistance = Volume->attenuationDistance;
        p_Info.AttenuationColor    = glm::vec3(Volume->attenuationColor[0], Volume->attenuationColor[1], Volume->attenuationColor[2]);

        Set(Volume->thicknessTexture, p_Info.ThicknessTexture);
    }

    if (const auto& Transmission = p_Material.diffuseTransmission) {
        p_Info.DiffuseTransmissionFactor = Transmission->diffuseTransmissionFactor;

        const auto& Color = Transmission->diffuseTransmissionColorFactor;
        p_Info.DiffuseTransmissionColor = glm::vec3(Color[0], Color[1], Color[2]);

        Set(Transmission->diffuseTransmissionTexture, p_Info.DiffuseTransmissionTexture);
        Set(Transmission->diffuseTransmissionColorTexture, p_Info.DiffuseTransmissionColorTexture, Srgb);
    }

    if (const auto& Clearcoat = p_Material.clearcoat) {
        p_Info.ClearcoatFactor    = Clearcoat->clearcoatFactor;
        p_Info.ClearcoatRoughness = Clearcoat->clearcoatRoughnessFactor;

        if (Clearcoat->clearcoatNormalTexture) { p_Info.ClearcoatNormalScale = Clearcoat->clearcoatNormalTexture->scale; }

        Set(Clearcoat->clearcoatTexture, p_Info.ClearcoatTexture);
        Set(Clearcoat->clearcoatRoughnessTexture, p_Info.ClearcoatRoughnessTexture);
        Set(Clearcoat->clearcoatNormalTexture, p_Info.ClearcoatNormalTexture);
    }
    if (const auto& Sheen = p_Material.sheen) {
        p_Info.SheenColor = glm::vec3(Sheen->sheenColorFactor[0], Sheen->sheenColorFactor[1], Sheen->sheenColorFactor[2]);
        p_Info.SheenRoughness = Sheen->sheenRoughnessFactor;

        Set(Sheen->sheenColorTexture, p_Info.SheenColorTexture, Srgb);
        Set(Sheen->sheenRoughnessTexture, p_Info.SheenRoughnessTexture);
    }
    if (const auto& Iridescence = p_Material.iridescence) {
        p_Info.IridescenceFactor = Iridescence->iridescenceFactor;
        p_Info.IridescenceIor    = Iridescence->iridescenceIor;

        p_Info.IridescenceThicknessMin = Iridescence->iridescenceThicknessMinimum;
        p_Info.IridescenceThicknessMax = Iridescence->iridescenceThicknessMaximum;

        Set(Iridescence->iridescenceTexture, p_Info.IridescenceTexture);
        Set(Iridescence->iridescenceThicknessTexture, p_Info.IridescenceThicknessTexture);
    }
    if (const auto& Anisotropy = p_Material.anisotropy) {
        p_Info.AnisotropyStrength = Anisotropy->anisotropyStrength;
        p_Info.AnisotropyRotation = Anisotropy->anisotropyRotation;

        Set(Anisotropy->anisotropyTexture, p_Info.AnisotropyTexture);
    }
}

Meshes::MeshCreateInfo BuildMesh(const fastgltf::Asset& p_Asset, const fastgltf::Primitive& p_Primitive)
{
    const auto PositionAttribute = p_Primitive.findAttribute("POSITION");

    if (PositionAttribute == p_Primitive.attributes.end()) {
        throw std::runtime_error("model primitive has no position attribute");
    }

    const fastgltf::Accessor& Positions = p_Asset.accessors[PositionAttribute->accessorIndex];

    Meshes::MeshCreateInfo Result;
    Result.Vertices.resize(Positions.count);

    for (Meshes::Vertex& Vertex : Result.Vertices) {
        Vertex = {};
        Vertex.Normal  = glm::vec3(0.0f, 1.0f, 0.0f);
        Vertex.Color   = glm::vec4(1.0f);
        Vertex.Joints  = glm::uvec4(0u);
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
        if (p_Asset.accessors[UvAttribute->accessorIndex].count != Positions.count) {
            throw std::runtime_error("texture coordinate count does not match mesh vertex count");
        }

        fastgltf::iterateAccessorWithIndex<glm::vec2>(p_Asset,
            p_Asset.accessors[UvAttribute->accessorIndex],
            [&Result](glm::vec2 p_Value, std::size_t p_Index) {
                Result.Vertices[p_Index].UvX = p_Value.x;
                Result.Vertices[p_Index].UvY = p_Value.y;
            }
        );
    }

    std::vector<std::size_t> ExtraUvAccessors;

    for (uint32_t Set = 1; ; ++Set) {
        const auto Attribute = p_Primitive.findAttribute("TEXCOORD_" + std::to_string(Set));

        if (Attribute == p_Primitive.attributes.end()) { break; }

        ExtraUvAccessors.push_back(Attribute->accessorIndex);
    }

    Result.ExtraTexCoordCount = static_cast<uint32_t>(ExtraUvAccessors.size());
    Result.ExtraTexCoords.resize(Positions.count * Result.ExtraTexCoordCount);

    for (uint32_t Set = 0; Set < Result.ExtraTexCoordCount; ++Set) {
        const auto& Accessor = p_Asset.accessors[ExtraUvAccessors[Set]];

        if (Accessor.count != Positions.count) { throw std::runtime_error("texture coordinate count does not match mesh vertex count"); }

        fastgltf::iterateAccessorWithIndex<glm::vec2>(p_Asset, Accessor,
            [&Result, Set](glm::vec2 p_Value, std::size_t p_Index) {
                Result.ExtraTexCoords[p_Index * Result.ExtraTexCoordCount + Set] = p_Value;
            });
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

std::vector<glm::vec2> ReadTangentTexCoords(
    const fastgltf::Asset& p_Asset, const fastgltf::Primitive& p_Primitive, std::size_t p_VertexCount
)
{
    std::size_t Set = 0;
    const std::size_t MaterialIndex = p_Primitive.materialIndex.value_or(p_Asset.materials.size());

    if (MaterialIndex < p_Asset.materials.size()) {
        const auto& Material = p_Asset.materials[MaterialIndex];

        if (Material.normalTexture) {
            Set = GetTexCoordSet(*Material.normalTexture);
        } else if (Material.packedNormalMetallicRoughnessTexture) {
            Set = GetTexCoordSet(*Material.packedNormalMetallicRoughnessTexture);
        }
    }

    const auto Attribute = p_Primitive.findAttribute("TEXCOORD_" + std::to_string(Set));

    if (Attribute == p_Primitive.attributes.end()) { return {}; }

    const auto& Accessor = p_Asset.accessors[Attribute->accessorIndex];

    if (Accessor.count != p_VertexCount) {
        throw std::runtime_error("tangent texture coordinate count does not match mesh vertex count");
    }

    std::vector<glm::vec2> TexCoords(p_VertexCount);

    fastgltf::iterateAccessorWithIndex<glm::vec2>(p_Asset, Accessor,
        [&TexCoords](glm::vec2 p_Value, std::size_t p_Index) { TexCoords[p_Index] = p_Value; });

    return TexCoords;
}

struct TangentBasis
{
    glm::vec3 Tangent   = glm::vec3(0.0f);
    glm::vec3 Bitangent = glm::vec3(0.0f);

    int Orientation = 0;

    uint32_t MirroredVertex = UINT32_MAX;
};

glm::vec3 NormalizeOr(glm::vec3 p_Vector, glm::vec3 p_Fallback)
{
    const float LengthSquared = glm::dot(p_Vector, p_Vector);

    return std::isfinite(LengthSquared) && LengthSquared > 0.0f
        ? p_Vector * glm::inversesqrt(LengthSquared) : p_Fallback;
}

void GenerateTangents(
    Meshes::MeshCreateInfo& p_Mesh, const std::vector<glm::vec2>& p_TexCoords,
    std::vector<Meshes::MeshMorphTargetCreateInfo>& p_MorphTargets
)
{
    std::vector<TangentBasis> Bases(p_Mesh.Vertices.size());

    if (!p_TexCoords.empty()) {
        for (std::size_t First = 0; First < p_Mesh.Indices.size(); First += 3) {
            const uint32_t A = p_Mesh.Indices[First];
            const uint32_t B = p_Mesh.Indices[First + 1];
            const uint32_t C = p_Mesh.Indices[First + 2];

            if (A >= p_TexCoords.size() || B >= p_TexCoords.size() || C >= p_TexCoords.size()) {
                throw std::runtime_error("mesh index is out of range");
            }

            const glm::dvec2 Uv1 = glm::dvec2(p_TexCoords[B]) - glm::dvec2(p_TexCoords[A]);
            const glm::dvec2 Uv2 = glm::dvec2(p_TexCoords[C]) - glm::dvec2(p_TexCoords[A]);
            const double Determinant = Uv1.x * Uv2.y - Uv1.y * Uv2.x;

            if (!std::isfinite(Determinant) || Determinant == 0.0) { continue; }

            const glm::dvec3 Position(p_Mesh.Vertices[A].Position);
            const glm::dvec3 Edge1 = glm::dvec3(p_Mesh.Vertices[B].Position) - Position;
            const glm::dvec3 Edge2 = glm::dvec3(p_Mesh.Vertices[C].Position) - Position;
            const glm::dvec3 Normal = glm::cross(Edge1, Edge2);

            if (!(glm::dot(Normal, Normal) > 0.0)) { continue; }

            const int Orientation = Determinant < 0.0 ? -1 : 1;
            const float Sign = static_cast<float>(Orientation);
            const glm::vec3 Tangent = Sign * glm::vec3(glm::normalize(Edge1 * Uv2.y - Edge2 * Uv1.y));
            const glm::vec3 Bitangent = Sign * glm::vec3(glm::normalize(Edge2 * Uv1.x - Edge1 * Uv2.x));

            for (std::size_t Corner = First; Corner < First + 3; ++Corner) {
                uint32_t& Index = p_Mesh.Indices[Corner];

                if (Bases[Index].Orientation == -Orientation) {
                    if (Bases[Index].MirroredVertex == UINT32_MAX) {
                        if (p_Mesh.Vertices.size() >= UINT32_MAX) {
                            throw std::runtime_error("mesh vertex count exceeds the index range");
                        }

                        Bases[Index].MirroredVertex = static_cast<uint32_t>(p_Mesh.Vertices.size());

                        p_Mesh.Vertices.push_back(p_Mesh.Vertices[Index]);
                        Bases.emplace_back();

                        for (uint32_t Set = 0; Set < p_Mesh.ExtraTexCoordCount; ++Set) {
                            const auto Uv = p_Mesh.ExtraTexCoords[static_cast<std::size_t>(Index) * p_Mesh.ExtraTexCoordCount + Set];

                            p_Mesh.ExtraTexCoords.push_back(Uv);
                        }

                        for (auto& Target : p_MorphTargets) {
                            if (!Target.PositionDeltas.empty()) { Target.PositionDeltas.push_back(Target.PositionDeltas[Index]); }
                            if (!Target.NormalDeltas.empty()) { Target.NormalDeltas.push_back(Target.NormalDeltas[Index]); }
                        }
                    }

                    Index = Bases[Index].MirroredVertex;
                }

                TangentBasis& Basis = Bases[Index];
                Basis.Orientation = Orientation;
                Basis.Tangent += Tangent;
                Basis.Bitangent += Bitangent;
            }
        }
    }

    for (std::size_t Index = 0; Index < p_Mesh.Vertices.size(); ++Index) {
        Meshes::Vertex& Vertex = p_Mesh.Vertices[Index];
        const TangentBasis& Basis = Bases[Index];
        const glm::vec3 Normal = NormalizeOr(Vertex.Normal, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 Axis = std::abs(Normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        const glm::vec3 Fallback = glm::normalize(Axis - Normal * glm::dot(Normal, Axis));
        const glm::vec3 Tangent = NormalizeOr(Basis.Tangent - Normal * glm::dot(Normal, Basis.Tangent), Fallback);
        const float Sign = glm::dot(glm::cross(Normal, Tangent), Basis.Bitangent) < 0.0f ? -1.0f : 1.0f;

        Vertex.Tangent = glm::vec4(Tangent, Sign);
    }
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
    : Resource(MakeKey(p_CreateInfo))
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
    m_Snapshot.store(nullptr, std::memory_order_release);

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

                    ReadMaterialTextures(Asset, SourceMaterial, Path, MaterialInfo);

                    if (SourceMaterial.alphaMode == fastgltf::AlphaMode::Mask) {
                        MaterialInfo.Alpha = Materials::AlphaMode::Mask;
                    } else if (SourceMaterial.alphaMode == fastgltf::AlphaMode::Blend) {
                        MaterialInfo.Alpha = Materials::AlphaMode::Blend;
                        MaterialInfo.TargetShader = Info.TransparentShader;
                    }

                    MaterialInfo.AlphaCutoff = SourceMaterial.alphaCutoff;
                    MaterialInfo.DoubleSided = SourceMaterial.doubleSided;

                    MaterialHandles.push_back(
                        ::Engine::Util::Applied::ResourceHandle<Materials::Material>(std::move(MaterialInfo))
                    );
                }

                ::Engine::Util::Applied::ResourceHandle<Materials::Material> DefaultMaterial;

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
                                glm::vec3 Tangent(0.0f);
                                glm::vec3 Bitangent(0.0f);
                                const glm::vec3 SourceTangent(Vertex.Tangent);
                                const glm::vec3 SourceBitangent = glm::cross(Vertex.Normal, SourceTangent) * Vertex.Tangent.w;
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
                                    Tangent += glm::mat3(JointMatrix) * SourceTangent * Weight;
                                    Bitangent += glm::mat3(JointMatrix) * SourceBitangent * Weight;
                                    WeightSum += Weight;
                                }

                                if (WeightSum > 0.0f) {
                                    Vertex.Position = glm::vec3(Position / WeightSum);

                                    if (glm::dot(Normal, Normal) > 0.0f) {
                                        Vertex.Normal = glm::normalize(Normal);
                                    }

                                    if (glm::dot(Tangent, Tangent) > 0.0f) {
                                        const float Handedness = glm::dot(glm::cross(Vertex.Normal, Tangent), Bitangent) < 0.0f ? -1.0f : 1.0f;
                                        Vertex.Tangent = glm::vec4(glm::normalize(Tangent), Handedness);
                                    }
                                }
                            }
                        }

                        for (Meshes::Vertex& Vertex : MeshInfo.Vertices) {
                            if (glm::dot(Vertex.Normal, Vertex.Normal) > 0.0f) {
                                Vertex.Normal = glm::normalize(Vertex.Normal);
                            }
                        }

                        if (Primitive.findAttribute("TANGENT") == Primitive.attributes.end()) {
                            const auto TexCoords = ReadTangentTexCoords(Asset, Primitive, MeshInfo.Vertices.size());
                            GenerateTangents(MeshInfo, TexCoords, MorphInfos);
                        }

                        ModelPart Part;

                        Part.Mesh = ::Engine::Util::Applied::ResourceHandle<Meshes::Mesh>(std::move(MeshInfo));

                        if (Primitive.materialIndex.has_value()
                            && Primitive.materialIndex.value() < MaterialHandles.size()) {
                            Part.Material = MaterialHandles[Primitive.materialIndex.value()];
                        } else {
                            if (!DefaultMaterial) {
                                Materials::MaterialCreateInfo DefaultInfo;
                                DefaultInfo.TargetShader = Info.Shader;
                                DefaultMaterial =
                                    ::Engine::Util::Applied::ResourceHandle<Materials::Material>(std::move(DefaultInfo));
                            }
                            Part.Material = DefaultMaterial;
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

                    VisitNode = [&VisitNode, &Asset, &PartIndices, &Info, Data](
                        std::size_t p_NodeIndex, const glm::mat4& p_ParentTransform
                    ) {
                        const fastgltf::Node& Node = Asset.nodes[p_NodeIndex];
                        const glm::mat4 LocalTransform = glm::make_mat4(
                            fastgltf::getTransformMatrix(Node).data()
                        );

                        const glm::mat4 WorldTransform = p_ParentTransform * LocalTransform;

                        if (Node.lightIndex.has_value()) {
                            if (Node.lightIndex.value() >= Asset.lights.size()) {
                                throw std::runtime_error("node light index is out of range");
                            }

                            Data->Lights.push_back(MakeLight(
                                Asset.lights[Node.lightIndex.value()], WorldTransform, Info.LightShader
                            ));
                        }

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

    if (!m_LoadData->Parts.empty() && !m_CreateInfo.Shader) {
        SetFailed("model requires a shader resource");

        return;
    }

    if (m_CreateInfo.Shader && m_CreateInfo.Shader.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
        SetFailed("model shader dependency failed: " + m_CreateInfo.Shader->GetFailureReason());

        return;
    }

    if (m_CreateInfo.Shader && m_CreateInfo.Shader.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
        SetRejected("model shader dependency rejected: " + m_CreateInfo.Shader->GetFailureReason());

        return;
    }

    for (const ModelPart& Part : m_LoadData->Parts) {
        if (!Part.Mesh || !Part.Material) {
            SetFailed("model part has no mesh or material");

            return;
        }

        if (Part.Mesh.GetState() == ::Engine::Util::Applied::ResourceState::Failed) {
            SetFailed("model mesh dependency failed: " + Part.Mesh->GetFailureReason());

            return;
        }

        if (Part.Mesh.GetState() == ::Engine::Util::Applied::ResourceState::Rejected) {
            SetRejected("model mesh dependency rejected: " + Part.Mesh->GetFailureReason());

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
    Snapshot->Lights = std::move(m_LoadData->Lights);

    if (!m_Parts.empty()) {
        glm::vec3 BoundsMin(std::numeric_limits<float>::max());
        glm::vec3 BoundsMax(std::numeric_limits<float>::lowest());

        for (const ModelPart& Part : m_Parts) {
            const std::shared_ptr<const Meshes::MeshSnapshot> Mesh = Part.Mesh->GetSnapshot();
            const glm::vec3 Center = (Mesh->BoundsMin + Mesh->BoundsMax) * 0.5f;
            const glm::vec3 Extent = (Mesh->BoundsMax - Mesh->BoundsMin) * 0.5f;
            const glm::mat4& World = Part.PrivateTransform;

            const glm::vec3 PartCenter(World * glm::vec4(Center, 1.0f));
            const glm::vec3 PartExtent = glm::abs(glm::vec3(World[0])) * Extent.x
                + glm::abs(glm::vec3(World[1])) * Extent.y
                + glm::abs(glm::vec3(World[2])) * Extent.z;

            BoundsMin = glm::min(BoundsMin, PartCenter - PartExtent);
            BoundsMax = glm::max(BoundsMax, PartCenter + PartExtent);
        }

        Snapshot->BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
        Snapshot->BoundsExtent = (BoundsMax - BoundsMin) * 0.5f;
    }

    Snapshot->VersionChecksum = GetVersionChecksum();

    m_Snapshot.store(std::move(Snapshot), std::memory_order_release);

    SetState(::Engine::Util::Applied::ResourceState::Ready);
}

void Model::Unload()
{
    m_Snapshot.store(nullptr, std::memory_order_release);
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
    return m_Snapshot.load(std::memory_order_acquire);
}

const ::Engine::Util::Applied::ResourceHandle<::Engine::Applied::Render::Resources::Shaders::Shader>& Model::GetShader() const
{
    return m_CreateInfo.Shader;
}

}
