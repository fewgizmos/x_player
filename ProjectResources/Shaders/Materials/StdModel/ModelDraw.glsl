#ifndef X_MODEL_DRAW
#define X_MODEL_DRAW

#extension GL_EXT_buffer_reference : require

#include "Util/Head/MaterialData.glsl"

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MaterialBuffer
{
    MaterialData Items[];
};

struct MeshVertex
{
    vec3 Position;
    float UvX;
    vec3 Normal;
    float UvY;
    vec4 Tangent;
    vec4 Color;
    uvec4 Joints;
    vec4 Weights;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer
{
    MeshVertex Items[];
};

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer TexCoordBuffer
{
    vec2 Items[];
};

layout(push_constant) uniform PushConstants
{
    mat4 Projection;
    MaterialBuffer Materials;
    VertexBuffer Vertices;
    TexCoordBuffer ExtraTexCoords;
    uint MaterialIndex;
    uint ExtraTexCoordCount;
} Push;

#endif
