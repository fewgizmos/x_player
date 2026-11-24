#version 450 core

#include "Util/Head/BindlessTextures.glsl"

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    uint TextureIndex;
    uint SamplerIndex;
} Push;

layout(location = 0) in vec2 UV;
layout(location = 0) out vec4 OutColor;

void main()
{
    OutColor = SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV);
}
