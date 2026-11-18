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

layout(location = 0) out vec4 OutColor;

void main() {
    OutColor = Push.TextureIndex == 0xFFFFFFFFu 
        ? Push.BaseColor 
        : SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV) * Push.BaseColor;
}
