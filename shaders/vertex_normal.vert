#version 450 core

layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec3 vertexNormal;

out VS_OUT {
    vec3 normal;
} vs_out;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
// user defined clip plane
uniform vec4 clipPlane;

out float clipDistX;
out float clipDistY;
out float clipDistZ;
out float clipDist;

void main()
{
    mat3 normalMatrix = mat3(transpose(inverse(modelViewMatrix)));
    vs_out.normal = normalize(vec3(projectionMatrix * vec4(normalMatrix * vertexNormal, 0.0)));
    gl_Position = projectionMatrix * modelViewMatrix * vec4(vertexPosition, 1.0);

    clipDistX = dot(clipPlaneX, modelViewMatrix* vec4(vertexPosition, 1));
    clipDistY = dot(clipPlaneY, modelViewMatrix* vec4(vertexPosition, 1));
    clipDistZ = dot(clipPlaneZ, modelViewMatrix* vec4(vertexPosition, 1));
    clipDist = dot(clipPlane, modelViewMatrix* vec4(vertexPosition, 1));
}