#version 450

layout(location = 0) flat out uint LightIndex;

void main()
{
    const vec2 Corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    LightIndex = uint(gl_InstanceIndex);

    gl_Position = vec4(Corner * 2.0 - 1.0, 0.0, 1.0);
}
