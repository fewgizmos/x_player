#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aUvX;
layout(location = 2) in float aUvY;
layout(location = 3) in vec3 aNormal;

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    vec4 BaseColor;
    uint TextureIndex;
    uint SamplerIndex;
    vec4 UvTransformRow0;
    vec4 UvTransformRow1;
} Push;

layout(location = 0) out vec2 UV;
layout(location = 1) out vec3 Normal;

void main()
{
    UV = vec2(
        dot(Push.UvTransformRow0, vec4(aUvX, aUvY, 1.0, 1.0)),
        dot(Push.UvTransformRow1, vec4(aUvX, aUvY, 1.0, 1.0))
    );

    Normal = normalize(aNormal);

    gl_Position = Push.Projection * vec4(aPosition, 1.0);
}
