#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aUvX;
layout(location = 2) in float aUvY;

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    uint TextureIndex;
    uint SamplerIndex;
} Push;

layout(location = 0) out vec2 UV;

void main()
{
    UV = vec2(aUvX, aUvY);
    gl_Position = Push.Projection * vec4(aPosition, 1.0);
}
