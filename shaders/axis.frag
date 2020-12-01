#version 450 core

layout( location = 0 ) out vec4 fragColor;

in vec3 fragmentColor;

uniform bool renderCone = false;
uniform vec3 coneColor;

void main()
{
    if(renderCone)
        fragColor = vec4(coneColor, 1.0);
    else
        fragColor = vec4(fragmentColor.xyz, 1.0);
}