#ifndef X_BASE_MODEL_GEOMETRY
#define X_BASE_MODEL_GEOMETRY

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

#ifdef X_FRAGMENT_STAGE

#include "Util/Head/BindlessTextures.glsl"
#include "Util/Head/MaterialConstants.glsl"

layout(location = 0) in vec2 UV;
layout(location = 1) in vec3 Normal;

#ifdef X_BASE_MODEL_TRANSPARENT_PASS
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

#else

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aUvX;
layout(location = 2) in float aUvY;
layout(location = 3) in vec3 aNormal;

layout(location = 4) in mat4 aWorld;
layout(location = 8) in vec4 aNormal0;
layout(location = 9) in vec4 aNormal1;
layout(location = 10) in vec4 aNormal2;

layout(location = 0) out vec2 UV;
layout(location = 1) out vec3 Normal;

#ifdef X_BASE_MODEL_TRANSPARENT_PASS
layout(location = 2) out vec3 WorldPosition;
#endif

void RenderGeometryVertex()
{
    const vec4 Position = aWorld * vec4(aPosition, 1.0);

    UV = vec2(
        dot(Push.UvTransformRow0, vec4(aUvX, aUvY, 1.0, 1.0)),
        dot(Push.UvTransformRow1, vec4(aUvX, aUvY, 1.0, 1.0))
    );

    Normal = mat3(aNormal0.xyz, aNormal1.xyz, aNormal2.xyz) * aNormal;
    gl_Position = Push.Projection * Position;

#ifdef X_BASE_MODEL_TRANSPARENT_PASS
    WorldPosition = Position.xyz;
#endif
}

#endif

#endif
