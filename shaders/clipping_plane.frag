#version 450 core

in vec2 texCoord;

uniform bool selected;
uniform vec3 planeColor;
uniform sampler2D hatchMap;

out vec4 fragColor;

void main()
{
    fragColor = vec4(planeColor, 1.0f);
    fragColor = mix(fragColor, texture(hatchMap, texCoord), 0.25);

    if(selected)
    {
        fragColor = mix(fragColor, vec4(1.0f, .65f, 0.0f, 1.0f), 0.5f);
    }
}