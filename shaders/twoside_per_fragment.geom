#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices=6) out;

in vec3 v_position[];
in vec3 v_normal[];
in vec2 v_texCoord2d[];

out vec3 g_normal;
out vec3 g_position;
out vec2 g_texCoord2d;

noperspective out vec3 g_edgeDistance;
uniform mat4 viewportMatrix; // Viewport matrix

uniform int displayMode;

in VS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} gs_in_shadow[];

out GS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} gs_out_shadow;

in vec4 v_fragPosReflSpace[];
out vec4 g_fragPosReflSpace;

in vec3  v_reflectionNormal[];
out vec3 g_reflectionNormal;

in float v_clipDistX[];
in float v_clipDistY[];
in float v_clipDistZ[];
in float v_clipDist[];

out float g_clipDistX;
out float g_clipDistY;
out float g_clipDistZ;
out float g_clipDist;

uniform float alpha;

void main()
{
    if(displayMode == 2) // WireShaded
    {
        // Transform each vertex into viewport space
        vec3 p0 = vec3(viewportMatrix * (gl_in[0].gl_Position /
        gl_in[0].gl_Position.w));
        vec3 p1 = vec3(viewportMatrix * (gl_in[1].gl_Position /
        gl_in[1].gl_Position.w));
        vec3 p2 = vec3(viewportMatrix * (gl_in[2].gl_Position /
        gl_in[2].gl_Position.w));
        // Find the altitudes (ha, hb and hc)
        float a = length(p1 - p2);
        float b = length(p2 - p0);
        float c = length(p1 - p0);
        float alpha = acos( (b*b + c*c - a*a) / (2.0*b*c) );
        float beta = acos( (a*a + c*c - b*b) / (2.0*a*c) );
        float ha = abs( c * sin( beta ) );
        float hb = abs( c * sin( alpha ) );
        float hc = abs( b * sin( alpha ) );

        // Send the triangle along with the edge distances
        g_edgeDistance = vec3( ha, 0, 0 );
        g_normal = v_normal[0];
        g_texCoord2d = v_texCoord2d[0];
        g_position = v_position[0];
        gl_Position = gl_in[0].gl_Position;
        g_clipDistX = v_clipDistX[0];
        g_clipDistY = v_clipDistY[0];
        g_clipDistZ = v_clipDistZ[0];
        g_clipDist =  v_clipDist[0];

        gl_ClipDistance[0] = g_clipDistX;
        gl_ClipDistance[1] = g_clipDistY;
        gl_ClipDistance[2] = g_clipDistZ;
        gl_ClipDistance[3] = g_clipDist;
        EmitVertex();

        g_edgeDistance = vec3( 0, hb, 0 );
        g_normal = v_normal[1];
        g_texCoord2d = v_texCoord2d[1];
        g_position = v_position[1];
        gl_Position = gl_in[1].gl_Position;
        g_clipDistX = v_clipDistX[1];
        g_clipDistY = v_clipDistY[1];
        g_clipDistZ = v_clipDistZ[1];
        g_clipDist =  v_clipDist[1];

        gl_ClipDistance[0] = g_clipDistX;
        gl_ClipDistance[1] = g_clipDistY;
        gl_ClipDistance[2] = g_clipDistZ;
        gl_ClipDistance[3] = g_clipDist;
        EmitVertex();

        g_edgeDistance = vec3( 0, 0, hc );
        g_normal = v_normal[2];
        g_texCoord2d = v_texCoord2d[2];
        g_position = v_position[2];
        gl_Position = gl_in[2].gl_Position;
        g_clipDistX = v_clipDistX[2];
        g_clipDistY = v_clipDistY[2];
        g_clipDistZ = v_clipDistZ[2];
        g_clipDist =  v_clipDist[2];

        gl_ClipDistance[0] = g_clipDistX;
        gl_ClipDistance[1] = g_clipDistY;
        gl_ClipDistance[2] = g_clipDistZ;
        gl_ClipDistance[3] = g_clipDist;
        EmitVertex();

        EndPrimitive();
    }
    else
    {
        for(int i=0; i<gl_in.length(); i++)
        {
            g_normal = v_normal[i];
            g_texCoord2d = v_texCoord2d[i];
            g_position = v_position[i];
            gl_Position = gl_in[i].gl_Position;

            g_clipDistX = v_clipDistX[i];
            g_clipDistY = v_clipDistY[i];
            g_clipDistZ = v_clipDistZ[i];
            g_clipDist =  v_clipDist[i];

            gl_ClipDistance[0] = g_clipDistX;
            gl_ClipDistance[1] = g_clipDistY;
            gl_ClipDistance[2] = g_clipDistZ;
            gl_ClipDistance[3] = g_clipDist;


            g_fragPosReflSpace = v_fragPosReflSpace[i];

            if(displayMode == 3 && alpha < 1.0f)
                g_reflectionNormal = v_reflectionNormal[i]; // commented out because this is causing reflection map to not work correctly
            else
                g_reflectionNormal = vec3(0.0);

            // Shadow mapping
            gs_out_shadow.FragPos = gs_in_shadow[i].FragPos;
            gs_out_shadow.Normal = gs_in_shadow[i].Normal; // commented out because this is causing shadow map to not work correctly
            gs_out_shadow.TexCoords = gs_in_shadow[i].TexCoords;
            gs_out_shadow.FragPosLightSpace = gs_in_shadow[i].FragPosLightSpace;
            gs_out_shadow.cameraPos = gs_in_shadow[i].cameraPos;
            gs_out_shadow.lightPos = gs_in_shadow[i].lightPos;

            EmitVertex();
        }
        EndPrimitive();
    }
}  
