#version 400

layout( location = 0 ) out vec4 fragColor;

in vec3 fragmentColor;

void main()
{
    fragColor = vec4(fragmentColor.xyz, 1.0);
}
