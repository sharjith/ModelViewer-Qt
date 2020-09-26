#version 330 core

in vec2 texCoord;

uniform vec3 planeColor;
uniform sampler2D hatchMap;

out vec4 fragColor;

void main()
{
    fragColor = vec4(planeColor, 1.0f);
    //fragColor = texture(hatchMap, texCoord);
}
