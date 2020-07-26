#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
in vec2 texCoord2d;

uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;

out vec3 v_normal;
out vec3 v_position;
out vec2 v_texCoord2d;				

void main()
{
    v_normal     = normalize(normalMatrix * vertexNormal);                       // normal vector
    v_position   = vec3(modelViewMatrix * vec4(vertexPosition, 1));              // vertex pos in eye coords
    v_texCoord2d = texCoord2d;

    gl_Position = projectionMatrix * modelViewMatrix * vec4(vertexPosition, 1);

    gl_ClipDistance[0] = dot(clipPlaneX, modelViewMatrix* vec4(vertexPosition, 1));
    gl_ClipDistance[1] = dot(clipPlaneY, modelViewMatrix* vec4(vertexPosition, 1));
    gl_ClipDistance[2] = dot(clipPlaneZ, modelViewMatrix* vec4(vertexPosition, 1));
}
