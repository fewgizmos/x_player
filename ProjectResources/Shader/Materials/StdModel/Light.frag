#version 450

#include "Materials/StdModel/Lighting.glsl"

layout(location = 0) flat in uint LightIndex;
layout(location = 0) out vec4 OutColor;

layout(set = 2, binding = 0) uniform sampler2D SceneAlbedo;
layout(set = 2, binding = 1) uniform sampler2D SceneNormal;
layout(set = 2, binding = 2) uniform sampler2D SceneDepth;

vec3 ReconstructWorld(vec2 p_Clip, float p_Depth)
{
    const vec4 World = Scene.InverseViewProjection * vec4(p_Clip, p_Depth, 1.0);

    return World.xyz / World.w;
}

void main()
{
    const ivec2 Pixel = ivec2(gl_FragCoord.xy);
    const float Depth = texelFetch(SceneDepth, Pixel, 0).r;
    const vec2 Clip = gl_FragCoord.xy * 2.0 / vec2(textureSize(SceneDepth, 0)) - 1.0;

    const LightData Light = Scene.Lights[LightIndex];

    if (Depth == 0.0) {
        if (Light.Options.x < LightHdri || Light.Options.z == 0u) { discard; }

        const vec3 Direction = normalize(ReconstructWorld(Clip, 0.5) - ReconstructWorld(Clip, 1.0));

        OutColor = vec4(EnvironmentColor(Light, Direction) * Light.Color.rgb * Light.DirectionIntensity.w, 1.0);

        return;
    }

    const vec3 Albedo = texelFetch(SceneAlbedo, Pixel, 0).rgb;
    const vec3 Normal = normalize(texelFetch(SceneNormal, Pixel, 0).xyz);

    OutColor = vec4(ShadeLight(Light, Albedo, ReconstructWorld(Clip, Depth), Normal), 1.0);
}
