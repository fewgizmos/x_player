#version 450 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUV;

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    uint TextureIndex;
    uint SamplerIndex;
} Push;

layout(location = 0) out vec2 UV;

void main()
{
    UV = aUV;
    gl_Position = Push.Projection * vec4(aPosition, 0.0, 1.0);
}
