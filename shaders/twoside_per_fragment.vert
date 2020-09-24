#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 texCoord2d;
layout(location = 3) in vec3 vertexTangent;
layout(location = 4) in vec3 vertexBitangent;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
uniform mat4 lightSpaceMatrix;
uniform vec3 cameraPos;
uniform vec3 lightPos;

// user defined clip plane
uniform vec4 clipPlane;

out float v_clipDistX;
out float v_clipDistY;
out float v_clipDistZ;
out float v_clipDist;

out vec3 v_normal;
out vec3 v_position;
out vec2 v_texCoord2d;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec3 v_tangentLightPos;
out vec3 v_tangentViewPos;
out vec3 v_tangentFragPos;

out vec3 v_reflectionPosition;
out vec3 v_reflectionNormal;

out VS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} vs_out_shadow;

void main()
{
    v_normal     = normalize(normalMatrix * vertexNormal);                       // normal vector
    //v_normal = mat3(transpose(inverse(modelMatrix))) * vertexNormal;
    v_position   = vec3(modelMatrix * vec4(vertexPosition, 1));              // vertex pos in eye coords
    v_texCoord2d = texCoord2d;
    v_tangent = normalize(normalMatrix * vertexTangent);
    v_bitangent = normalize(normalMatrix * vertexBitangent);

    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1);

    v_clipDistX = dot(clipPlaneX, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDistY = dot(clipPlaneY, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDistZ = dot(clipPlaneZ, modelViewMatrix* vec4(vertexPosition, 1));
    v_clipDist = dot(clipPlane, modelViewMatrix* vec4(vertexPosition, 1));

    // Shadow mapping
    vs_out_shadow.FragPos = vec3(modelMatrix * vec4(vertexPosition, 1.0));
    vs_out_shadow.Normal = normalize(mat3(transpose(inverse(modelMatrix))) * vertexNormal);
    vs_out_shadow.TexCoords = v_texCoord2d;
    vs_out_shadow.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out_shadow.FragPos, 1.0);
    vs_out_shadow.cameraPos = cameraPos;
    vs_out_shadow.lightPos = lightPos;

    // Cube environment mapping
    v_reflectionPosition = vec3(modelMatrix * vec4(vertexPosition, 1.0));
    v_reflectionNormal = normalize(mat3(transpose(inverse(modelMatrix))) * vertexNormal);

    // Depth mapping
    vec3 T = normalize(mat3(normalMatrix) * vertexTangent);
    vec3 B = normalize(mat3(normalMatrix) * vertexBitangent);
    vec3 N = normalize(mat3(normalMatrix) * vertexNormal);
    if (dot(cross(N, T), B) < 0.0f)
    {
        T = T * -1.0f;
    }
    mat3 TBN = transpose(mat3(T, B, N));

    v_tangentLightPos = TBN * lightPos - v_position;
    v_tangentViewPos  = TBN * cameraPos - v_position;
    v_tangentFragPos  = TBN * v_position;

    // Moved this to geometry shader
    /*
    gl_ClipDistance[0] = clipDistX;
    gl_ClipDistance[1] = clipDistY;
    gl_ClipDistance[2] = clipDistZ;
    gl_ClipDistance[3] = clipDist;
    */
}

