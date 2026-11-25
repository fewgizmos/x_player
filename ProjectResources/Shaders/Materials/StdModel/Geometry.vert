#version 450

#include "Materials/StdModel/ModelDraw.glsl"

#include "Util/Head/MaterialUv.glsl"

layout(location = 4) in mat4 aWorld;

layout(location = 8) in vec4  aNormal0;
layout(location = 9) in vec4  aNormal1;
layout(location = 10) in vec4 aNormal2;

layout(location = 0) out vec4 UvPairs[MaterialUvPairCount];

layout(location = MaterialUvPairCount) out vec3      Normal;
layout(location = MaterialUvPairCount + 1u) out vec4 Tangent;
layout(location = MaterialUvPairCount + 2u) out vec3 WorldPosition;

vec2 GetUv(uint p_Slot, vec2 p_Uv)
{
    const MaterialTextureData Texture = Push.Materials.Items[Push.MaterialIndex].Textures[p_Slot];

    if (Texture.Texture == NoTexture) { return vec2(0.0); }

    vec2 UV = p_Uv;

    if (Texture.TexCoord > 0u && Texture.TexCoord <= Push.ExtraTexCoordCount) {
        const uint Index = uint(gl_VertexIndex) * Push.ExtraTexCoordCount + Texture.TexCoord - 1u;

        UV = Push.ExtraTexCoords.Items[Index];
    }

    return MaterialUv(Texture, UV);
}

void main()
{
    const MeshVertex Vertex = Push.Vertices.Items[uint(gl_VertexIndex)];

    const vec4 Position = aWorld * vec4(Vertex.Position, 1.0);
    const vec2 UV       = vec2(Vertex.UvX, Vertex.UvY);

    for (uint Pair = 0u; Pair < MaterialUvPairCount; ++Pair) {
        const uint Slot = Pair * 2u;

        UvPairs[Pair] = vec4(GetUv(Slot, UV), Slot + 1u < MaterialTexturesCount ? GetUv(Slot + 1u, UV) : vec2(0.0));
    }

    Normal  = mat3(aNormal0.xyz, aNormal1.xyz, aNormal2.xyz) * Vertex.Normal;
    Tangent = vec4(mat3(aWorld) * Vertex.Tangent.xyz, aNormal0.w * Vertex.Tangent.w);
    WorldPosition = Position.xyz;

    gl_Position = Push.Projection * Position;
}
