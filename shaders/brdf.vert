#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec2 texCoord2d;

out vec2 texCoords;

void main()
{
    texCoords = texCoord2d;
    gl_Position = vec4(vertexPosition, 1.0);
}
