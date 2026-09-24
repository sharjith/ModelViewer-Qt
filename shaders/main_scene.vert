#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec2 texCoord0;
layout(location = 4) in vec2 texCoord1;
layout(location = 5) in vec2 texCoord2;
layout(location = 6) in vec2 texCoord3;
layout(location = 7) in vec3 vertexTangent;
layout(location = 8) in vec3 vertexBitangent;
layout(location = 9) in vec4 jointIndices;
layout(location = 10) in vec4 jointWeights;
// Surface Analysis overlay (curvature/thickness/deviation heatmaps) - a
// dedicated attribute, deliberately NOT sharing vertexColor's location/
// buffer. See RenderableMesh::setAnalysisOverlayColors()'s doc comment for
// why: vertexColor is real authored mesh data (e.g. genuine Point Set
// Reconstruction vertex colors), and this overlay must never touch it.
layout(location = 11) in vec4 analysisColor;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelViewMatrix;
uniform mat3 normalMatrix;
uniform mat3 worldNormalMatrix;
uniform mat4 projectionMatrix;
uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
uniform mat4 lightSpaceMatrix;
uniform vec3 cameraPos;
uniform vec3 lightPos;
uniform bool hasSkinning;
uniform int jointCount;
uniform mat4 jointMatrices[128];

// user defined clip plane
uniform vec4 clipPlane;

// Box clipping (4th clipping mode): six view-space half-space planes, in the
// order xMin, xMax, yMin, yMax, zMin, zMax. Mutually exclusive with the
// per-axis planes above - when enabled they take over clip distance slots 0..5.
uniform vec4 clipPlaneBox[6];
uniform bool clipPlaneBoxEnabled;

out vec3 v_position;
out vec3 v_normal;
out vec4 v_color;
out vec4 v_rawVertexColor;
out vec4 v_analysisColor;
out vec2 v_texCoord0;
out vec2 v_texCoord1;
out vec2 v_texCoord2;
out vec2 v_texCoord3;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec3 v_worldTangent;
out vec3 v_worldBitangent;
out vec3 v_tangentLightPos;
out vec3 v_tangentViewPos;
out vec3 v_tangentFragPos;

out vec3 v_reflectionPosition;
out vec3 v_reflectionNormal;
flat out vec3 v_flatNormal;
flat out vec3 v_reflectionFlatNormal;
flat out vec3 v_positionFlat;
noperspective out vec3 v_positionLinear;

out VS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} vs_out_shadow;

mat4 computeSkinMatrix()
{
    if (!hasSkinning || jointCount <= 0)
        return mat4(1.0);

    mat4 skin = mat4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        float weight = jointWeights[i];
        if (weight <= 0.0)
            continue;

        int jointIndex = int(jointIndices[i]);
        if (jointIndex < 0 || jointIndex >= jointCount || jointIndex >= 128)
            continue;

        skin += jointMatrices[jointIndex] * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0)
        return mat4(1.0);

    return skin;
}

void main()
{
    mat4 skinMatrix = computeSkinMatrix();
    vec4 skinnedPosition = skinMatrix * vec4(vertexPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * vertexNormal;
    vec3 skinnedTangent = mat3(skinMatrix) * vertexTangent;
    vec3 skinnedBitangent = mat3(skinMatrix) * vertexBitangent;

    vec4 worldPos = modelMatrix * skinnedPosition;
    v_position   = vec3(worldPos);              // vertex pos in eye coords
    vec3 transformedNormal = normalMatrix * skinnedNormal;
    float transformedNormalLen = length(transformedNormal);
    if (transformedNormalLen < 1e-8)
    {
        v_normal = vec3(0.0, 0.0, 0.0);
    }
    else
    {
        v_normal = transformedNormal / transformedNormalLen;
    }
    v_color            = vertexColor;
    v_rawVertexColor   = vertexColor;
    v_analysisColor    = analysisColor;
    v_texCoord0 = texCoord0;
    v_texCoord1 = texCoord1;
    v_texCoord2 = texCoord2;
    v_texCoord3 = texCoord3;
    vec3 transformedTangent = normalMatrix * skinnedTangent;
    float transformedTangentLen = length(transformedTangent);
    if (transformedTangentLen < 1e-8)
        v_tangent = vec3(0.0);
    else
        v_tangent = transformedTangent / transformedTangentLen;

    vec3 transformedBitangent = normalMatrix * skinnedBitangent;
    float transformedBitangentLen = length(transformedBitangent);
    if (transformedBitangentLen < 1e-8)
        v_bitangent = vec3(0.0);
    else
        v_bitangent = transformedBitangent / transformedBitangentLen;

    vec3 worldTangent = worldNormalMatrix * skinnedTangent;
    float worldTangentLen = length(worldTangent);
    if (worldTangentLen < 1e-8)
        v_worldTangent = vec3(0.0);
    else
        v_worldTangent = worldTangent / worldTangentLen;

    vec3 worldBitangent = worldNormalMatrix * skinnedBitangent;
    float worldBitangentLen = length(worldBitangent);
    if (worldBitangentLen < 1e-8)
        v_worldBitangent = vec3(0.0);
    else
        v_worldBitangent = worldBitangent / worldBitangentLen;

    gl_Position = projectionMatrix * viewMatrix * worldPos;

    // Shadow mapping
    vs_out_shadow.FragPos = vec3(worldPos);
    vec3 shadowNormal = worldNormalMatrix * skinnedNormal;
    float shadowNormalLen = length(shadowNormal);
    if (shadowNormalLen < 1e-8)
        vs_out_shadow.Normal = vec3(0.0);
    else
        vs_out_shadow.Normal = shadowNormal / shadowNormalLen;
    vs_out_shadow.TexCoords = v_texCoord0;
    vs_out_shadow.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out_shadow.FragPos, 1.0);
    vs_out_shadow.cameraPos = cameraPos;
    vs_out_shadow.lightPos = lightPos;

    // Cube environment mapping
    v_reflectionPosition = vec3(worldPos);
    vec3 reflNormal = worldNormalMatrix * skinnedNormal;
    float reflNormalLen = length(reflNormal);
    if (reflNormalLen < 1e-8)
        v_reflectionNormal = vec3(0.0);
    else
        v_reflectionNormal = reflNormal / reflNormalLen;

    // Flat shading: provoking vertex's value used for whole triangle (flat qualifier)
    v_flatNormal = v_normal;
    v_reflectionFlatNormal = v_reflectionNormal;
    v_positionFlat = vec3(worldPos);  // constant per triangle — for flat lighting
    // Linear (non-perspective) interpolation of world position keeps dFdx/dFdy
    // constant across the triangle regardless of zoom — required for stable flat normals.
    v_positionLinear = vec3(worldPos);

    // Depth mapping
    vec3 T = mat3(modelViewMatrix) * skinnedTangent;
    float Tlen = length(T);
    if (Tlen < 1e-8)
        T = vec3(0.0);
    else
        T = T / Tlen;

    vec3 N = mat3(modelViewMatrix) * skinnedNormal;
    float Nlen = length(N);
    if (Nlen < 1e-8)
        N = vec3(0.0);
    else
        N = N / Nlen;

    vec3 B = normalize(cross(N, T));  // Safe now, N and T are valid
    if (length(N) > 0.01 && length(T) > 0.01)  // Only check if both valid
    {
        if (dot(cross(N, T), B) < 0.0)
            T = -T;
    }
    mat3 TBN = transpose(mat3(T, B, N));

    v_tangentLightPos = TBN * lightPos;
    v_tangentViewPos  = TBN * cameraPos;
    v_tangentFragPos  = TBN * v_position;

    // Assign clip distances for hardware clipping   
    vec4 viewPos = modelViewMatrix * skinnedPosition;
    // Constant indices throughout (no loop) so the implicit gl_ClipDistance
    // size is unambiguously 6. Slots 4..5 are only enabled in box mode.
    if (clipPlaneBoxEnabled)
    {
        gl_ClipDistance[0] = dot(clipPlaneBox[0], viewPos);
        gl_ClipDistance[1] = dot(clipPlaneBox[1], viewPos);
        gl_ClipDistance[2] = dot(clipPlaneBox[2], viewPos);
        gl_ClipDistance[3] = dot(clipPlaneBox[3], viewPos);
        gl_ClipDistance[4] = dot(clipPlaneBox[4], viewPos);
        gl_ClipDistance[5] = dot(clipPlaneBox[5], viewPos);
    }
    else
    {
        gl_ClipDistance[0] = dot(clipPlaneX, viewPos);
        gl_ClipDistance[1] = dot(clipPlaneY, viewPos);
        gl_ClipDistance[2] = dot(clipPlaneZ, viewPos);
        gl_ClipDistance[3] = dot(clipPlane, viewPos);
        gl_ClipDistance[4] = 1.0;
        gl_ClipDistance[5] = 1.0;
    }

}
