#version 450

#include "Materials/StdModel/Service/Surface.glsl"

#include "Materials/StdModel/Lighting.glsl"

layout(location = 0) out vec4 OutColor;

void main()
{
    const Surface Material = ReadSurface();

    vec3 Color = vec3(0.0);

    for (uint Index = 0u; Index < Scene.Counts.x; ++Index) {
        Color += ShadeLight(Scene.Lights[Index], Material.Albedo.rgb,
            WorldPosition * uintBitsToFloat(Scene.Counts.z), Material.Normal);
    }

    OutColor = vec4(Color, Material.Albedo.a);
}
