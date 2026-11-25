#version 450

#include "Materials/StdModel/Surface.glsl"

layout(location = 0) out vec4 OutColor;
layout(location = 1) out vec4 OutNormal;

void main()
{
    const Surface Material = ReadSurface();

    OutColor = Material.Albedo;
    OutNormal = vec4(Material.Normal, 0.0);
}
