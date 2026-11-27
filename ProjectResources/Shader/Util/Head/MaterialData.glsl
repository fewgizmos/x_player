#ifndef X_MATERIAL_DATA
#define X_MATERIAL_DATA

#include "MaterialConstants.glsl"

struct MaterialTextureData
{
    vec4 UvTransformRow0;
    vec4 UvTransformRow1;

    uint Texture;
    uint Sampler;
    uint TexCoord;
    uint Padding;
};

struct MaterialData
{
    vec4 BaseColor;
    vec3 Emissive;
    float Metallic;

    float Roughness;
    float NormalScale;
    float OcclusionStrength;
    float AlphaCutoff;

    vec3 SpecularColor;
    float SpecularFactor;

    vec3 DiffuseTransmissionColor;
    float DiffuseTransmissionFactor;

    vec3 SheenColor;
    float SheenRoughness;

    vec3 AttenuationColor;
    float AttenuationDistance;

    float Ior;

    float EmissiveStrength;

    float ClearcoatFactor;
    float ClearcoatRoughness;
    float ClearcoatNormalScale;

    float HeightScale;
    float HeightWorld;

    float TransmissionFactor;

    float ThicknessFactor;
    float IridescenceFactor;
    float IridescenceIor;
    float IridescenceThicknessMin;
    float IridescenceThicknessMax;

    float AnisotropyStrength;
    float AnisotropyRotation;

    uint AlphaMode;

    uvec4 Options;
    MaterialTextureData Textures[MaterialTexturesCount];
};

#endif
