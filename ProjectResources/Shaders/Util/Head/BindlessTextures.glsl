#ifndef X_BINDLESS_TEXTURES
#define X_BINDLESS_TEXTURES

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler SceneSamplers[X_MAX_SAMPLERS];
layout(set = 0, binding = 1) uniform texture2D SceneTextures[X_MAX_TEXTURES];

vec4 SampleTextureLod(uint p_Texture, uint p_Sampler, vec2 p_Uv, float p_Lod)
{
    return textureLod(sampler2D(
        SceneTextures[nonuniformEXT(p_Texture)],
        SceneSamplers[nonuniformEXT(p_Sampler)]
    ), p_Uv, p_Lod);
}

#ifdef X_FRAGMENT_STAGE
vec4 SampleTexture(uint p_Texture, uint p_Sampler, vec2 p_Uv)
{
    return texture(sampler2D(
        SceneTextures[nonuniformEXT(p_Texture)],
        SceneSamplers[nonuniformEXT(p_Sampler)]
    ), p_Uv);
}
#endif

#endif
