#version 450

layout(location = 0) out vec4 OutColor;

#ifdef X_BASE_MODEL_LIGHT_PASS

#include "Materials/BaseModelPasses/LightPass.glsl"

void main()
{
    OutColor = ResolveLighting();
}

#elif defined(X_BASE_MODEL_TRANSPARENT_PASS)

#include "Materials/BaseModelPasses/GeometryPass.glsl"
#include "Materials/BaseModelPasses/LightPass.glsl"

void main()
{
    const Surface Material = ReadSurface();
    OutColor = vec4(ShadeSurface(Material.Albedo.rgb, WorldPosition, Material.Normal), Material.Albedo.a);
}

#else

#include "Materials/BaseModelPasses/GeometryPass.glsl"

layout(location = 1) out vec4 OutNormal;

void main()
{
    const Surface Material = ReadSurface();
    OutColor = Material.Albedo;
    OutNormal = vec4(Material.Normal, 0.0);
}

#endif
