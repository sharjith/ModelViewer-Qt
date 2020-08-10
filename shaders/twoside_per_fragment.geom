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
    if(displayMode == 0 || displayMode == 1)
    {
	for(int i=0; i<gl_in.length(); i++)
	{
	    g_normal = v_normal[i];
	    g_texCoord2d = v_texCoord2d[i];
	    g_position = v_position[i];
	    gl_Position = gl_in[i].gl_Position;

	    g_clipDistX = clipDistX[i];
	    g_clipDistY = clipDistY[i];
	    g_clipDistZ = clipDistZ[i];
	    g_clipDist = clipDist[i];

	    gl_ClipDistance[0] = g_clipDistX;
	    gl_ClipDistance[1] = g_clipDistY;
	    gl_ClipDistance[2] = g_clipDistZ;
	    gl_ClipDistance[3] = g_clipDist;

	    EmitVertex();
        }
        EndPrimitive();
    }
    else
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
        g_clipDistX = clipDistX[0];
        g_clipDistY = clipDistY[0];
        g_clipDistZ = clipDistZ[0];
        g_clipDist = clipDist[0];

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
        g_clipDistX = clipDistX[1];
        g_clipDistY = clipDistY[1];
        g_clipDistZ = clipDistZ[1];
        g_clipDist = clipDist[1];

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
        g_clipDistX = clipDistX[2];
        g_clipDistY = clipDistY[2];
        g_clipDistZ = clipDistZ[2];
        g_clipDist = clipDist[2];

        gl_ClipDistance[0] = g_clipDistX;
        gl_ClipDistance[1] = g_clipDistY;
        gl_ClipDistance[2] = g_clipDistZ;
        gl_ClipDistance[3] = g_clipDist;
	EmitVertex();

        EndPrimitive();
    }
}  
