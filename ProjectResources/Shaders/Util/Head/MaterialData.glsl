#ifndef X_MATERIAL_DATA
#define X_MATERIAL_DATA

#include "MaterialConstants.glsl"

struct MaterialData
{
    vec4 BaseColor;
    vec3 Emissive;
    float Metallic;
    float Roughness;
    float NormalScale;
    float OcclusionStrength;
    float AlphaCutoff;
    uint BaseColorTexture;
    uint BaseColorSampler;
    uint MetallicRoughnessTexture;
    uint MetallicRoughnessSampler;
    uint NormalTexture;
    uint NormalSampler;
    uint OcclusionTexture;
    uint OcclusionSampler;
    uint EmissiveTexture;
    uint EmissiveSampler;
    uint AlphaMode;
    uint SpecularTexture;
    uint SpecularSampler;
    uint SpecularColorTexture;
    uint SpecularColorSampler;
    uint DiffuseTransmissionTexture;
    uint DiffuseTransmissionSampler;
    uint DiffuseTransmissionColorTexture;
    uint DiffuseTransmissionColorSampler;
    uint ClearcoatTexture;
    uint ClearcoatSampler;
    uint ClearcoatRoughnessTexture;
    uint ClearcoatRoughnessSampler;
    uint ClearcoatNormalTexture;
    uint ClearcoatNormalSampler;
    uint SheenColorTexture;
    uint SheenColorSampler;
    uint SheenRoughnessTexture;
    uint SheenRoughnessSampler;
    uint HeightTexture;
    uint HeightSampler;
    uint Padding0;
    vec4 UvTransforms[MaterialTexturesCount * 2u];
    vec3 SpecularColor;
    float SpecularFactor;
    float Ior;
    float EmissiveStrength;
    float ClearcoatFactor;
    float ClearcoatRoughness;
    vec3 DiffuseTransmissionColor;
    float DiffuseTransmissionFactor;
    vec3 SheenColor;
    float SheenRoughness;
    float ClearcoatNormalScale;
    float HeightScale;
    float HeightWorld;
    float Padding1;
};

#endif
