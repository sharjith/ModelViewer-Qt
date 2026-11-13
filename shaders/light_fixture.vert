#version 450 core

// Light fixture gizmos (bulb, spot, sun) and their beam cone lines.
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;

out VS_OUT {
    vec3 viewNormal;
} vs_out;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

void main()
{
    // Uniform scale and rotation only, so the model matrix doubles as the normal matrix.
    vs_out.viewNormal = mat3(viewMatrix) * (mat3(modelMatrix) * vertexNormal);
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1.0);
}
