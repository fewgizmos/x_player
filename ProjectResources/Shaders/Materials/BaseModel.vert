#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aUvX;
layout(location = 2) in float aUvY;
layout(location = 3) in vec3 aNormal;

layout(location = 4) in mat4 aWorld;
layout(location = 8) in vec4 aNormal0;
layout(location = 9) in vec4 aNormal1;
layout(location = 10) in vec4 aNormal2;

layout(push_constant) uniform PushConstants
{
    mat4 Projection;

    vec4 BaseColor;

    uint TextureIndex;
    uint SamplerIndex;

    float AlphaCutoff;
    uint AlphaMode;

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

    Normal = mat3(aNormal0.xyz, aNormal1.xyz, aNormal2.xyz) * aNormal;

    gl_Position = Push.Projection * aWorld * vec4(aPosition, 1.0);
}
