#version 450 core

out vec4 fragColor;

in vec3 texCoords;

uniform samplerCube skybox;
uniform bool hdrToneMapping = false;
uniform bool gammaCorrection = false;
uniform float screenGamma = 2.2;

void main()
{
    fragColor = texture(skybox, texCoords);

    // HDR tonemapping
    if(hdrToneMapping)
        fragColor = fragColor / (fragColor + vec4(1.0));
    // gamma correct
    if(gammaCorrection)
        fragColor = pow(fragColor, vec4(1.0/screenGamma));
}