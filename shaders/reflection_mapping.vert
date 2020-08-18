#version 450 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
in vec2 texCoord2d;

out vec3 v_normal;
out vec3 v_position;
out vec2 v_texCoord2d;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;


void main()
{
    v_normal     = normalize(normalMatrix * vertexNormal);                       // normal vector
    v_position   = vec3(modelMatrix * viewMatrix * vec4(vertexPosition, 1));               // vertex pos in eye coords
    v_texCoord2d = texCoord2d;
    gl_Position = projectionMatrix * modelMatrix * viewMatrix * vec4(vertexPosition, 1.0);
}
