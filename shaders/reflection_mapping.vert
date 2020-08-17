#version 450 core
layout (location = 0) in vec3 aPos;

uniform mat4 reflectionSpaceMatrix;
uniform mat4 model;

void main()
{
    gl_Position = reflectionSpaceMatrix * model * vec4(aPos, 1.0);
}
