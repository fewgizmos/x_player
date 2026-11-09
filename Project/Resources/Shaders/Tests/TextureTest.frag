#version 450 core

layout(set = 0, binding = 0) uniform sampler2D Texture;

layout(location = 0) in vec2 UV;
layout(location = 0) out vec4 OutColor;

void main()
{
    OutColor = texture(Texture, UV);
}
