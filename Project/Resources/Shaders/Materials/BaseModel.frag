#version 450

#include "Util/Head/BindlessTextures.glsl"

layout(push_constant) uniform PushConstants
{
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

void main()
{
    const vec3 LightDirection = normalize(vec3(-0.4, 0.8, 0.6));
    const float Ambient = 0.25;
    const float Diffuse = 0.75 * max(dot(normalize(Normal), LightDirection), 0.0);
    const vec3 Lighting = vec3(Ambient + Diffuse);

    OutColor = (Push.TextureIndex == 0xFFFFFFFFu
        ? Push.BaseColor
        : SampleTexture(Push.TextureIndex, Push.SamplerIndex, UV) * Push.BaseColor)
        * vec4(Lighting, 1.0);
}
