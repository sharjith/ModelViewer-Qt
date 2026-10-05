#version 450 core

layout( location = 0 ) out vec4 fragColor;

in vec3 fragmentColor;

uniform bool renderCone = false;
uniform vec3 coneColor;
uniform float opacity = 1.0;

void main()
{
    if(renderCone)
        fragColor = vec4(coneColor, opacity);
    else
        fragColor = vec4(fragmentColor.xyz, opacity);
}
