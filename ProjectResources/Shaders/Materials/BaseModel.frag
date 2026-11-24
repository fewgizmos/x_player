#version 450

#include "Util/Head/BindlessTextures.glsl"
#include "Util/Head/MaterialConstants.glsl"

layout(push_constant) uniform PushConstants {
    mat4 Projection;
    vec4 BaseColor;
    uint TextureIndex;
    uint SamplerIndex;
    float AlphaCutoff;
    uint AlphaMode;
    vec4 UvTransformRow0;
    vec4 UvTransformRow1;
} Push;

layout(location = 0) in vec2 UV;

layout(location = 1) in vec3 Normal;

layout(location = 0) out vec4 OutColor;

void main() {
    vec4 Albedo = Push.TextureIndex == NoTexture
        ? Push.BaseColor
        : SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV) * Push.BaseColor;

    if (Push.AlphaMode == AlphaModeMask && Albedo.a < Push.AlphaCutoff) { discard; }
    if (Push.AlphaMode != AlphaModeBlend) { Albedo.a = 1.0; }

    vec3 SurfaceNormal = gl_FrontFacing ? Normal : -Normal;
    float Diffuse = max(normalize(SurfaceNormal).y, 0.0);

    OutColor = vec4(Albedo.rgb * (0.45 + 0.90 * Diffuse), Albedo.a);
}
