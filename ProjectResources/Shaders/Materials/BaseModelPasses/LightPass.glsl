#ifndef X_BASE_MODEL_LIGHT
#define X_BASE_MODEL_LIGHT

#ifdef X_FRAGMENT_STAGE

#include "Util/Head/BindlessTextures.glsl"

const uint LightPoint       = 0u;
const uint LightSpot        = 1u;
const uint LightDirectional = 2u;
const uint LightRectangle   = 3u;
const uint LightDisk        = 4u;
const uint LightSphere      = 5u;
const uint LightTube        = 6u;
const uint LightHdri        = 7u;
const uint LightSky         = 8u;

const float Pi = 3.14159265;

const int SampleCount = 16;

struct LightData
{
    vec4 PositionRange;
    vec4 DirectionIntensity;
    vec4 Color;
    vec4 AxisXSize;
    vec4 AxisYSize;
    vec4 Shape;
    vec4 SkyColor;
    vec4 HorizonColor;
    vec4 GroundColor;
    uvec4 Options;
};

layout(std430, set = 1, binding = 0) readonly buffer FrameLights
{
    mat4 InverseViewProjection;
    uvec4 Counts;
    LightData Lights[];
} Scene;

vec2 LightSample(int p_Index)
{
    const float Index = float(p_Index) + 0.5;

    return vec2(Index / float(SampleCount), fract(Index * 0.61803399));
}

float RangeFalloff(float p_DistanceSquared, float p_Range)
{
    if (p_Range <= 0.0) { return 1.0; }

    const float RatioSquared = p_DistanceSquared / (p_Range * p_Range);

    return max(1.0 - RatioSquared * RatioSquared, 0.0);
}

float PunctualLight(LightData p_Light, vec3 p_Position, vec3 p_Normal)
{
    const vec3 Forward = p_Light.DirectionIntensity.xyz;

    if (p_Light.Options.x == LightDirectional) { return max(dot(p_Normal, -Forward), 0.0); }

    const vec3 Offset = p_Light.PositionRange.xyz - p_Position;
    const float DistanceSquared = max(dot(Offset, Offset), 0.000001);
    const vec3 Direction = Offset * inversesqrt(DistanceSquared);
    float Falloff = RangeFalloff(DistanceSquared, p_Light.PositionRange.w);

    if (p_Light.Options.x == LightSpot) {
        const float Inner = p_Light.Shape.x;
        const float Outer = p_Light.Shape.y;
        const float Angle = clamp((dot(Forward, -Direction) - Outer) / max(Inner - Outer, 0.000001), 0.0, 1.0);

        Falloff *= Angle * Angle;
    }

    return max(dot(p_Normal, Direction), 0.0) * Falloff / DistanceSquared;
}

float AreaSize(LightData p_Light)
{
    const float Radius = p_Light.Shape.z;

    switch (p_Light.Options.x) {
    case LightRectangle: return p_Light.AxisXSize.w * p_Light.AxisYSize.w;
    case LightDisk:      return Pi * Radius * Radius;
    case LightSphere:    return 4.0 * Pi * Radius * Radius;
    case LightTube:      return 2.0 * Pi * Radius * p_Light.Shape.w;
    default:             return 0.0;
    }
}

vec3 AreaSample(LightData p_Light, vec2 p_Sample, out vec3 p_Normal)
{
    const vec3 X = p_Light.AxisXSize.xyz;
    const vec3 Y = p_Light.AxisYSize.xyz;
    const vec3 Forward = p_Light.DirectionIntensity.xyz;
    const float Radius = p_Light.Shape.z;
    const float Angle = 2.0 * Pi * p_Sample.y;
    const vec2 Circle = vec2(cos(Angle), sin(Angle));

    p_Normal = Forward;

    switch (p_Light.Options.x) {
    case LightRectangle:
        return X * ((p_Sample.x - 0.5) * p_Light.AxisXSize.w)
            + Y * ((p_Sample.y - 0.5) * p_Light.AxisYSize.w);
    case LightDisk:
        return (X * Circle.x + Y * Circle.y) * (Radius * sqrt(p_Sample.x));
    case LightSphere: {
        const float Height = 1.0 - 2.0 * p_Sample.x;
        p_Normal = (X * Circle.x + Y * Circle.y) * sqrt(max(1.0 - Height * Height, 0.0)) + Forward * Height;

        return Radius * p_Normal;
    }
    case LightTube:
        p_Normal = Y * Circle.x + Forward * Circle.y;

        return X * ((p_Sample.x - 0.5) * p_Light.Shape.w) + Radius * p_Normal;
    default:
        return vec3(0.0);
    }
}

float AreaLight(LightData p_Light, vec3 p_Position, vec3 p_Normal)
{
    const float SampleArea = AreaSize(p_Light) / float(SampleCount);

    if (SampleArea <= 0.0) { return 0.0; }

    float Irradiance = 0.0;

    for (int Index = 0; Index < SampleCount; ++Index) {
        vec3 EmitterNormal;
        const vec3 Sample = p_Light.PositionRange.xyz + AreaSample(p_Light, LightSample(Index), EmitterNormal);
        const vec3 Offset = Sample - p_Position;
        const float DistanceSquared = max(dot(Offset, Offset), 0.000001);
        const vec3 Direction = Offset * inversesqrt(DistanceSquared);

        Irradiance += max(dot(EmitterNormal, -Direction), 0.0) * max(dot(p_Normal, Direction), 0.0)
            * RangeFalloff(DistanceSquared, p_Light.PositionRange.w) / (DistanceSquared + SampleArea / Pi);
    }

    return SampleArea * Irradiance;
}

vec3 EnvironmentColor(LightData p_Light, vec3 p_Direction)
{
    const vec3 Local = vec3(dot(p_Direction, p_Light.AxisXSize.xyz), dot(p_Direction, p_Light.AxisYSize.xyz),
        -dot(p_Direction, p_Light.DirectionIntensity.xyz));

    if (p_Light.Options.x == LightSky) {
        return Local.y >= 0.0
            ? mix(p_Light.HorizonColor.rgb, p_Light.SkyColor.rgb, Local.y)
            : mix(p_Light.HorizonColor.rgb, p_Light.GroundColor.rgb, -Local.y);
    }

    const vec2 Uv = vec2(atan(Local.z, Local.x) / (2.0 * Pi) + 0.5, acos(clamp(Local.y, -1.0, 1.0)) / Pi);

    return SampleTextureLod(p_Light.Options.y, Scene.Counts.y, Uv, 0.0).rgb;
}

vec3 EnvironmentLight(LightData p_Light, vec3 p_Normal)
{
    const vec3 Reference = abs(p_Normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    const vec3 X = normalize(cross(Reference, p_Normal));
    const vec3 Y = cross(p_Normal, X);
    vec3 Irradiance = vec3(0.0);

    for (int Index = 0; Index < SampleCount; ++Index) {
        const vec2 Sample = LightSample(Index);
        const float Angle = 2.0 * Pi * Sample.y;
        const vec3 Direction = (X * cos(Angle) + Y * sin(Angle)) * sqrt(Sample.x) + p_Normal * sqrt(1.0 - Sample.x);

        Irradiance += EnvironmentColor(p_Light, Direction);
    }

    return Irradiance / float(SampleCount);
}

vec3 ShadeSurface(vec3 p_Albedo, vec3 p_Position, vec3 p_Normal)
{
    vec3 Color = vec3(0.0);

    for (uint Index = 0u; Index < Scene.Counts.x; ++Index) {
        const LightData Light = Scene.Lights[Index];
        const uint Type = Light.Options.x;
        const vec3 Tint = Light.Color.rgb * Light.DirectionIntensity.w;

        if (Type <= LightDirectional) {
            Color += Tint * (PunctualLight(Light, p_Position, p_Normal) / Pi);
        } else if (Type <= LightTube) {
            Color += Tint * (AreaLight(Light, p_Position, p_Normal) / Pi);
        } else {
            Color += Tint * EnvironmentLight(Light, p_Normal);
        }
    }

    return Color * p_Albedo;
}

#ifdef X_BASE_MODEL_LIGHT_PASS

layout(set = 2, binding = 0) uniform sampler2D SceneAlbedo;
layout(set = 2, binding = 1) uniform sampler2D SceneNormal;
layout(set = 2, binding = 2) uniform sampler2D SceneDepth;

vec3 ReconstructWorld(vec2 p_Clip, float p_Depth)
{
    const vec4 World = Scene.InverseViewProjection * vec4(p_Clip, p_Depth, 1.0);

    return World.xyz / World.w;
}

vec4 ResolveLighting()
{
    const ivec2 Pixel = ivec2(gl_FragCoord.xy);
    const float Depth = texelFetch(SceneDepth, Pixel, 0).r;
    const vec2 Clip = gl_FragCoord.xy * 2.0 / vec2(textureSize(SceneDepth, 0)) - 1.0;

    if (Depth == 0.0) {
        for (uint Index = 0u; Index < Scene.Counts.x; ++Index) {
            const LightData Light = Scene.Lights[Index];

            if (Light.Options.x >= LightHdri && Light.Options.z != 0u) {
                const vec3 Direction = normalize(ReconstructWorld(Clip, 0.0) - ReconstructWorld(Clip, 1.0));

                return vec4(EnvironmentColor(Light, Direction) * Light.Color.rgb * Light.DirectionIntensity.w, 1.0);
            }
        }

        discard;
    }

    const vec3 Albedo = texelFetch(SceneAlbedo, Pixel, 0).rgb;
    const vec3 Normal = normalize(texelFetch(SceneNormal, Pixel, 0).xyz);

    return vec4(ShadeSurface(Albedo, ReconstructWorld(Clip, Depth), Normal), 1.0);
}

#endif

#else

void RenderLightVertex()
{
    const vec2 Corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    gl_Position = vec4(Corner * 2.0 - 1.0, 0.0, 1.0);
}

#endif

#endif
