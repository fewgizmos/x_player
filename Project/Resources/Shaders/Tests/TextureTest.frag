#version 450 core
#extension GL_EXT_nonuniform_qualifier : require

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    uint TextureIndex;
    uint SamplerIndex;
} Push;

layout(set = 0, binding = 0) uniform sampler SceneSamplers[64];
layout(set = 0, binding = 1) uniform texture2D SceneTextures[4096];

layout(location = 0) in vec2 UV;
layout(location = 0) out vec4 OutColor;

void main()
{
    OutColor = texture(
        sampler2D(
            SceneTextures[nonuniformEXT(Push.TextureIndex)],
            SceneSamplers[nonuniformEXT(Push.SamplerIndex)]
        ),
        UV
    );
}
