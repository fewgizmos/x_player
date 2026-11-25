#version 450

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    vec4 BaseColor;
    uint TextureIndex;
    uint SamplerIndex;
    float AlphaCutoff;
    uint AlphaMode;
    vec4 UvTransformRow0;
    vec4 UvTransformRow1;
} Push;

#include "Util/Head/BindlessTextures.glsl"
#include "Util/Head/MaterialConstants.glsl"

layout(location = 0) in vec2 UV;
layout(location = 1) in vec3 Normal;

#ifdef X_SHADE_SERVICE_TRANSPARENT
layout(location = 2) in vec3 WorldPosition;
#endif

struct Surface
{
    vec4 Albedo;
    vec3 Normal;
};

Surface ReadSurface()
{
    vec4 Albedo = Push.TextureIndex == NoTexture
        ? Push.BaseColor
        : SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV) * Push.BaseColor;

    if (Push.AlphaMode == AlphaModeMask && Albedo.a < Push.AlphaCutoff) { discard; }
    if (Push.AlphaMode != AlphaModeBlend) { Albedo.a = 1.0; }

    const vec3 SurfaceNormal = gl_FrontFacing ? Normal : -Normal;
    const float LengthSquared = dot(SurfaceNormal, SurfaceNormal);

    return Surface(Albedo, LengthSquared > 0.0 ? SurfaceNormal * inversesqrt(LengthSquared) : vec3(0.0, 1.0, 0.0));
}

layout(location = 0) out vec4 OutColor;

#ifdef X_SHADE_SERVICE_TRANSPARENT
#include "Materials/StdModel/Lighting.glsl"
#else
layout(location = 1) out vec4 OutNormal;
#endif

void main()
{
    const Surface Material = ReadSurface();
#ifdef X_SHADE_SERVICE_TRANSPARENT
    vec3 Color = vec3(0.0);
    for (uint Index = 0u; Index < Scene.Counts.x; ++Index) {
        Color += ShadeLight(Scene.Lights[Index], Material.Albedo.rgb,
            WorldPosition * uintBitsToFloat(Scene.Counts.z), Material.Normal);
    }
    OutColor = vec4(Color, Material.Albedo.a);
#else
    OutColor = Material.Albedo;
    OutNormal = vec4(Material.Normal, 0.0);
#endif
}
