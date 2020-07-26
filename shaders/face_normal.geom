#version 450 core

layout (triangles) in;
layout (line_strip, max_vertices = 6) out;

in VS_OUT {
    vec3 normal;
} gs_in[];


const float MAGNITUDE = 0.2;
uniform float modelSize;

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


void main()
{
    vec3 P0 = gl_in[0].gl_Position.xyz;
    vec3 P1 = gl_in[1].gl_Position.xyz;
    vec3 P2 = gl_in[2].gl_Position.xyz;

    vec3 V0 = P0 - P1;
    vec3 V1 = P2 - P1;

    vec3 N = cross(V1, V0);
    N = normalize(N);

    // Center of the triangle
    vec3 P = (P0+P1+P2) / 3.0;

    gl_Position = vec4(P, 1.0);
    g_clipDistX = clipDistX[0];
    g_clipDistY = clipDistY[0];
    g_clipDistZ = clipDistZ[0];
    g_clipDist = clipDist[0];

    gl_ClipDistance[0] = g_clipDistX;
    gl_ClipDistance[1] = g_clipDistY;
    gl_ClipDistance[2] = g_clipDistZ;
    gl_ClipDistance[3] = g_clipDist;
    EmitVertex();

    gl_Position = vec4(P + gs_in[0].normal * modelSize * MAGNITUDE, 1.0);
    g_clipDistX = clipDistX[1];
    g_clipDistY = clipDistY[1];
    g_clipDistZ = clipDistZ[1];
    g_clipDist = clipDist[1];

    gl_ClipDistance[0] = g_clipDistX;
    gl_ClipDistance[1] = g_clipDistY;
    gl_ClipDistance[2] = g_clipDistZ;
    gl_ClipDistance[3] = g_clipDist;
    EmitVertex();
    EndPrimitive();
}
