#version 450

#include "Util/Head/BindlessTextures.glsl"

layout(push_constant) uniform PushConstants {
    mat4 Projection;
    vec4 BaseColor;
    uint TextureIndex;
    uint SamplerIndex;
    vec4 UvTransformRow0;
    vec4 UvTransformRow1;
} Push;

layout(location = 0) in vec2 UV;

layout(location = 1) in vec3 Normal;

layout(location = 0) out vec4 OutColor;

void main() {
    vec4 Albedo = Push.TextureIndex == 0xFFFFFFFFu 
        ? Push.BaseColor 
        : SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV) * Push.BaseColor;
    // Light direction in the rock mesh coordinate system.
    float Diffuse = max(dot(normalize(Normal), normalize(vec3(-0.4, 0.35, -0.85))), 0.0);
    OutColor = vec4(Albedo.rgb * (0.28 + 0.72 * Diffuse), Albedo.a);
}
