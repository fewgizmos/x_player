#version 450 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
} Push;

layout(location = 0) out vec3 Color;

void main()
{
    Color = aColor;
    gl_Position = Push.Projection * vec4(aPosition, 0.0, 1.0);
}
