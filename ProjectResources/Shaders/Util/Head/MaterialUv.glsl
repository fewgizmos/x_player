#ifndef X_MATERIAL_UV
#define X_MATERIAL_UV

#include "MaterialData.glsl"

vec2 MaterialUv(MaterialData p_Material, uint p_Slot, vec2 p_Uv)
{
    vec3 Source = vec3(p_Uv, 1.0);

    return vec2(
        dot(p_Material.UvTransforms[p_Slot * 2u].xyz, Source),
        dot(p_Material.UvTransforms[p_Slot * 2u + 1u].xyz, Source)
    );
}

#endif
