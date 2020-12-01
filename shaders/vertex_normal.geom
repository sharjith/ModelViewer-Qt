#version 450 core

layout (triangles) in;
layout (line_strip, max_vertices = 6) out;

in VS_OUT {
    vec3 normal;
} gs_in[];

const float MAGNITUDE = 0.05;

out vec3 g_normal;
out vec3 g_position;
out vec2 g_texCoord2d;

in float clipDistX[];
in float clipDistY[];
in float clipDistZ[];
in float clipDist[];

out float g_clipDistX;
out float g_clipDistY;
out float g_clipDistZ;
out float g_clipDist;

void GenerateLine(int index)
{
    gl_Position = gl_in[index].gl_Position;
    g_clipDistX = clipDistX[index];
    g_clipDistY = clipDistY[index];
    g_clipDistZ = clipDistZ[index];
    g_clipDist = clipDist[index];

    gl_ClipDistance[0] = g_clipDistX;
    gl_ClipDistance[1] = g_clipDistY;
    gl_ClipDistance[2] = g_clipDistZ;
    gl_ClipDistance[3] = g_clipDist;
    EmitVertex();

    gl_Position = gl_in[index].gl_Position + vec4(gs_in[index].normal, 0.0) * MAGNITUDE;
    g_clipDistX = clipDistX[index];
    g_clipDistY = clipDistY[index];
    g_clipDistZ = clipDistZ[index];
    g_clipDist = clipDist[index];

    gl_ClipDistance[0] = g_clipDistX;
    gl_ClipDistance[1] = g_clipDistY;
    gl_ClipDistance[2] = g_clipDistZ;
    gl_ClipDistance[3] = g_clipDist;
    EmitVertex();

    EndPrimitive();
}

void main()
{
    GenerateLine(0); // first vertex normal
    GenerateLine(1); // second vertex normal
    GenerateLine(2); // third vertex normal
}