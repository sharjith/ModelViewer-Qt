#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
in vec2 texCoord2d;

uniform mat4 modelMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
uniform mat4 lightSpaceMatrix;

// user defined clip plane
uniform vec4 clipPlane;

out float v_clipDistX;
out float v_clipDistY;
out float v_clipDistZ;
out float v_clipDist;

out vec3 v_normal;
out vec3 v_position;
out vec2 v_texCoord2d;	

out vec4 v_fragPosLightSpace;
out vec3 v_reflectionNormal;
out vec4 v_clipSpace;

void main()
{
    v_normal     = normalize(normalMatrix * vertexNormal);                       // normal vector
    v_position   = vec3(modelViewMatrix * vec4(vertexPosition, 1));              // vertex pos in eye coords
    v_texCoord2d = texCoord2d;

    v_clipSpace = projectionMatrix * modelViewMatrix * vec4(vertexPosition.x, 0.0, vertexPosition.y, 1.0);
    gl_Position = projectionMatrix * modelViewMatrix * vec4(vertexPosition, 1);

    v_clipDistX = dot(clipPlaneX, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDistY = dot(clipPlaneY, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDistZ = dot(clipPlaneZ, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDist = dot(clipPlane, modelViewMatrix* vec4(vertexPosition, 1));

    // for shadow mapping
    v_fragPosLightSpace = lightSpaceMatrix * vec4(vertexPosition, 1.0);

    // for reflection mapping
    v_reflectionNormal = mat3(transpose(inverse(modelMatrix))) * vertexNormal;

    // Moved this to geometry shader
    /*
    gl_ClipDistance[0] = clipDistX;
    gl_ClipDistance[1] = clipDistY;
    gl_ClipDistance[2] = clipDistZ;
    gl_ClipDistance[3] = clipDist;
    */
}

