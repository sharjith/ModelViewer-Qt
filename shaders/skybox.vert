#version 450 core

layout(location = 0) in vec3 vertexPosition;
in vec2 texCoord2d;

out vec3 texCoords;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;

void main()
{
    texCoords = vertexPosition;
    vec4 pos = projectionMatrix * viewMatrix * vec4(vertexPosition, 1.0);
    gl_Position = pos.xyww;
}  
