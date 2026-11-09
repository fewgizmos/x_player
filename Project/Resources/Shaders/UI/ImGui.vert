#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform uPushConstant
{
    vec2 uScale;
    vec2 uTranslate;
} Push;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout(location = 0) out struct
{
    vec4 Color;
    vec2 UV;
} Out;

vec3 SrgbToLinear(vec3 p_Color)
{
    return mix(
        p_Color / 12.92,
        pow((p_Color + 0.055) / 1.055, vec3(2.4)),
        step(vec3(0.04045), p_Color)
    );
}

void main()
{
    Out.Color = vec4(SrgbToLinear(aColor.rgb), aColor.a);
    Out.UV = aUV;

    gl_Position = vec4(aPos * Push.uScale + Push.uTranslate, 0.0, 1.0);
}
