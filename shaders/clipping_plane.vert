#version 330 core
layout (location = 0) in vec3 vertexPosition;
in vec2 texCoord2d;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

out vec2 texCoord;

void main()
{
    texCoord = texCoord2d;
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1.0);
}
