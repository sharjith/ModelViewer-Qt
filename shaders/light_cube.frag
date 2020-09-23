#version 330 core
out vec4 fragColor;

uniform vec3 lightColor;

void main()
{
    fragColor = vec4(lightColor, 1.0f); // set alle 4 vector values to 1.0
}
