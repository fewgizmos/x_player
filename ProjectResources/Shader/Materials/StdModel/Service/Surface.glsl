#ifndef X_STD_MODEL_SURFACE
#define X_STD_MODEL_SURFACE

#include "Materials/StdModel/Service/ModelDraw.glsl"

#include "Util/Head/BindlessTextures.glsl"

layout(location = 0) in vec4 UvPairs[MaterialUvPairCount];
layout(location = MaterialUvPairCount) in vec3 Normal;
layout(location = MaterialUvPairCount + 1u) in vec4 Tangent;
layout(location = MaterialUvPairCount + 2u) in vec3 WorldPosition;

struct Surface
{
    vec4 Albedo;
    vec3 Normal;
};

vec2 GetUv(uint p_Slot)
{
    const vec4 Pair = UvPairs[p_Slot / 2u];
    return (p_Slot & 1u) == 0u ? Pair.xy : Pair.zw;
}

vec4 ReadTexture(MaterialData p_Material, uint p_Slot)
{
    const MaterialTextureData Texture = p_Material.Textures[p_Slot];
    return Texture.Texture == NoTexture ? vec4(1.0) : SampleTexture(Texture.Texture, Texture.Sampler, GetUv(p_Slot));
}

vec3 NormalizeOr(vec3 p_Value, vec3 p_Fallback)
{
    const float LengthSquared = dot(p_Value, p_Value);
    return LengthSquared > 0.0 ? p_Value * inversesqrt(LengthSquared) : p_Fallback;
}

vec3 MapNormal(MaterialTextureData p_Texture, vec2 p_Uv, vec3 p_Normal, vec3 p_Direction)
{
    const vec3 Axis     = abs(p_Normal.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
    const vec3 Fallback = normalize(Axis - p_Normal * dot(p_Normal, Axis));

    vec3 T = NormalizeOr(Tangent.xyz - p_Normal * dot(p_Normal, Tangent.xyz), Fallback);

    float Orientation = Tangent.w < 0.0 ? -1.0 : 1.0;

    if (p_Texture.UvTransformRow0.xy != vec2(1.0, 0.0) || p_Texture.UvTransformRow1.xy != vec2(0.0, 1.0)) {
        const vec2 UvX = dFdx(p_Uv);
        const vec2 UvY = dFdy(p_Uv);

        const vec3 PositionX = dFdx(WorldPosition);
        const vec3 PositionY = dFdy(WorldPosition);

        const float Det = UvX.x * UvY.y - UvX.y * UvY.x;

        if (abs(Det) > 1e-20) {
            const vec3 U = (PositionX * UvY.y - PositionY * UvX.y) * sign(Det);
            const vec3 V = (PositionY * UvX.x - PositionX * UvY.x) * sign(Det);

            T = NormalizeOr(U - p_Normal * dot(p_Normal, U), T);

            Orientation = dot(cross(p_Normal, T), V) < 0.0 ? -1.0 : 1.0;
        }
    }

    const vec3 B = cross(p_Normal, T) * Orientation;

    return NormalizeOr(mat3(T, B, p_Normal) * p_Direction, p_Normal);
}

Surface ReadSurface()
{
    const MaterialData Material = Push.Materials.Items[Push.MaterialIndex];

    vec4 Albedo        = Material.BaseColor * ReadTexture(Material, TextureSlotBaseColor);
    vec3 SurfaceNormal = NormalizeOr(Normal, vec3(0.0, 1.0, 0.0));

    const bool PackedNormal = Material.Textures[TextureSlotNormal].Texture == NoTexture;
    const uint NormalSlot   = PackedNormal ? TextureSlotPackedNormalMetallicRoughness : TextureSlotNormal;

    const MaterialTextureData NormalTexture = Material.Textures[NormalSlot];

    if (NormalTexture.Texture != NoTexture) {
        const vec4 Sample = ReadTexture(Material, NormalSlot);
        vec3 Direction = Sample.rgb * 2.0 - 1.0;

        if (PackedNormal) {
            Direction.z = sqrt(max(1.0 - dot(Direction.xy, Direction.xy), 0.0));
        }

        Direction.xy *= Material.NormalScale;
        SurfaceNormal = MapNormal(NormalTexture, GetUv(NormalSlot), SurfaceNormal, Direction);
    }

    if (Material.AlphaMode == AlphaModeMask && Albedo.a < Material.AlphaCutoff) { discard; }
    if (Material.AlphaMode != AlphaModeBlend) { Albedo.a = 1.0; }

    return Surface(Albedo, gl_FrontFacing ? SurfaceNormal : -SurfaceNormal);
}

#endif
