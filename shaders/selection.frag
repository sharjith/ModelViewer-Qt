#version 450 core

uniform vec4 pickingColor;

out vec4 fragColor;

void main()
{
    fragColor = pickingColor;
} 
