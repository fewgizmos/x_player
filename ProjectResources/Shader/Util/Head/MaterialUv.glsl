#ifndef X_MATERIAL_UV
#define X_MATERIAL_UV

#include "MaterialData.glsl"

vec2 MaterialUv(MaterialTextureData p_Texture, vec2 p_Uv)
{
    const vec3 Source = vec3(p_Uv, 1.0);

    return vec2(dot(p_Texture.UvTransformRow0.xyz, Source), dot(p_Texture.UvTransformRow1.xyz, Source));
}

#endif
