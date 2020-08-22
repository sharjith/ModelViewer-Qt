#version 450 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
in vec2 texCoord2d;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
// user defined clip plane
uniform vec4 clipPlane;

out vec3 v_normal;
out vec3 v_position;
out vec2 v_texCoord2d;
out vec3 v_reflectionNormal;

out float v_clipDistX;
out float v_clipDistY;
out float v_clipDistZ;
out float v_clipDist;


void main()
{
    v_normal     = normalize(normalMatrix * vertexNormal);                       // normal vector
    v_position   = vec3(modelMatrix * viewMatrix * vec4(vertexPosition, 1));               // vertex pos in eye coords
    v_texCoord2d = texCoord2d;
    gl_Position = projectionMatrix * modelMatrix * viewMatrix * vec4(vertexPosition, 1.0);

    v_clipDistX = dot(clipPlaneX, modelMatrix* viewMatrix * vec4(vertexPosition, 1));
    v_clipDistY = dot(clipPlaneY, modelMatrix* viewMatrix * vec4(vertexPosition, 1));
    v_clipDistZ = dot(clipPlaneZ, modelMatrix* viewMatrix * vec4(vertexPosition, 1));
    v_clipDist = dot(clipPlane, modelMatrix* viewMatrix * vec4(vertexPosition, 1));

    gl_ClipDistance[0] = v_clipDistX;
    gl_ClipDistance[1] = v_clipDistY;
    gl_ClipDistance[2] = v_clipDistZ;
    gl_ClipDistance[3] = v_clipDist;

    // for reflection mapping
    v_reflectionNormal = mat3(transpose(inverse(modelMatrix))) * vertexNormal;
}
