#version 330 core
out vec4 fragColor;

uniform vec3 planeColor;

void main()
{
    fragColor = vec4(planeColor, 1.0f); // set alle 4 vector values to 1.0
}
