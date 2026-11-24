#version 450

#ifdef X_BASE_MODEL_LIGHT_PASS

#include "Materials/BaseModelPasses/LightPass.glsl"

void main()
{
    RenderLightVertex();
}

#else

#include "Materials/BaseModelPasses/GeometryPass.glsl"

void main()
{
    RenderGeometryVertex();
}

#endif
