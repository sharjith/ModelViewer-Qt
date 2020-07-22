#version 400

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexColor;

out vec3 fragmentColor;

uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;

void main()
{    
    gl_Position = projectionMatrix * modelViewMatrix * vec4(vertexPosition, 1);
    fragmentColor = vertexColor;
}

