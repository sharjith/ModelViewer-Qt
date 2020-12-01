#version 450 core

layout(location = 0) in vec3 vertexPosition;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
// user defined clip plane
uniform vec4 clipPlane;

out float v_clipDistX;
out float v_clipDistY;
out float v_clipDistZ;
out float v_clipDist;

void main()
{
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1);

    v_clipDistX = dot(clipPlaneX, viewMatrix * modelMatrix * vec4(vertexPosition, 1));
    v_clipDistY = dot(clipPlaneY, viewMatrix * modelMatrix* vec4(vertexPosition, 1));
    v_clipDistZ = dot(clipPlaneZ, viewMatrix * modelMatrix* vec4(vertexPosition, 1));
    v_clipDist =  dot(clipPlane, viewMatrix * modelMatrix* vec4(vertexPosition, 1));

    gl_ClipDistance[0] = v_clipDistX;
    gl_ClipDistance[1] = v_clipDistY;
    gl_ClipDistance[2] = v_clipDistZ;
    gl_ClipDistance[3] = v_clipDist;
}