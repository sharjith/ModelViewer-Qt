#version 450 core

// Flat-shading variant of main_scene.vert.
// All per-vertex varyings are packed into the VS_FLAT_GEOM interface block
// so that main_scene_flat.geom can declare
//   in VS_FLAT_GEOM { ... } gs_fg_in[];
// without any same-name conflict with its own
//   out vec3 v_position; / out vec3 v_normal; / ...
// outputs (which are required to match the fragment shader's in declarations).
//
// The VS_OUT_SHADOW block and gl_Position / gl_ClipDistance are unchanged
// from the standard vertex shader.

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
// Surface Analysis overlay - see main_scene.vert's own doc comment for this
// attribute. Forwarded through VS_FLAT_GEOM/the geometry shader purely so
// this program still LINKS against main_scene.frag's "in vec4
// v_analysisColor" (both programs share that one fragment shader) - actual
// meshes with an active overlay never render through this flat-shading
// program at all (ViewportWidget.cpp excludes them), so the value reaching
// the fragment shader here is never actually used.
layout(location = 11) in vec4 analysisColor;

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
uniform bool hasSkinning;
uniform int jointCount;
uniform mat4 jointMatrices[128];

uniform vec4 clipPlane;

// All per-vertex varyings go into this single interface block.
// The geometry shader receives them as gs_fg_in[i].* (unambiguous)
// and writes them back to the fragment shader as individual v_* outputs.
out VS_FLAT_GEOM {
    vec3 position;              // world-space vertex position   → v_position
    vec3 normal;                // view-space smooth normal       → v_normal
    vec4 color;                 //                                → v_color
    vec4 rawVertexColor;        //                                → v_rawVertexColor
    vec2 texCoord0;             //                                → v_texCoord0
    vec2 texCoord1;
    vec2 texCoord2;
    vec2 texCoord3;
    vec3 tangent;               //                                → v_tangent
    vec3 bitangent;             //                                → v_bitangent
    vec3 worldTangent;          //                                → v_worldTangent
    vec3 worldBitangent;        //                                → v_worldBitangent
    vec3 tangentLightPos;       //                                → v_tangentLightPos
    vec3 tangentViewPos;        //                                → v_tangentViewPos
    vec3 tangentFragPos;        //                                → v_tangentFragPos
    vec3 reflectionPosition;    //                                → v_reflectionPosition
    vec3 reflectionNormal;      // world-space smooth normal       → v_reflectionNormal
    // The GS will override flatNormal / reflectionFlatNormal with the geometric face normal.
    vec3 flatNormal;            // view-space  (→ v_flatNormal)
    vec3 reflectionFlatNormal;  // world-space (→ v_reflectionFlatNormal)
    vec3 positionFlat;          // constant per-face position     → v_positionFlat
    vec3 positionLinear;        // for noperspective interp.      → v_positionLinear
    vec4 analysisColor;         //                                → v_analysisColor
} vs_fg;

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
    vec4 skinnedPosition  = skinMatrix * vec4(vertexPosition, 1.0);
    vec3 skinnedNormal    = mat3(skinMatrix) * vertexNormal;
    vec3 skinnedTangent   = mat3(skinMatrix) * vertexTangent;
    vec3 skinnedBitangent = mat3(skinMatrix) * vertexBitangent;

    vec4 worldPos = modelMatrix * skinnedPosition;
    vs_fg.position = vec3(worldPos);

    // View-space normal
    vec3 transformedNormal = normalMatrix * skinnedNormal;
    float transformedNormalLen = length(transformedNormal);
    vs_fg.normal = (transformedNormalLen < 1e-8)
        ? vec3(0.0)
        : transformedNormal / transformedNormalLen;

    vs_fg.color         = vertexColor;
    vs_fg.rawVertexColor = vertexColor;
    vs_fg.analysisColor = analysisColor;
    vs_fg.texCoord0     = texCoord0;
    vs_fg.texCoord1     = texCoord1;
    vs_fg.texCoord2     = texCoord2;
    vs_fg.texCoord3     = texCoord3;

    // View-space tangent / bitangent
    vec3 transformedTangent = normalMatrix * skinnedTangent;
    float transformedTangentLen = length(transformedTangent);
    vs_fg.tangent = (transformedTangentLen < 1e-8)
        ? vec3(0.0)
        : transformedTangent / transformedTangentLen;

    vec3 transformedBitangent = normalMatrix * skinnedBitangent;
    float transformedBitangentLen = length(transformedBitangent);
    vs_fg.bitangent = (transformedBitangentLen < 1e-8)
        ? vec3(0.0)
        : transformedBitangent / transformedBitangentLen;

    // World-space tangent / bitangent
    mat3 worldNormalMatrix = mat3(transpose(inverse(modelMatrix)));
    vec3 worldTangent = worldNormalMatrix * skinnedTangent;
    float worldTangentLen = length(worldTangent);
    vs_fg.worldTangent = (worldTangentLen < 1e-8)
        ? vec3(0.0)
        : worldTangent / worldTangentLen;

    vec3 worldBitangent = worldNormalMatrix * skinnedBitangent;
    float worldBitangentLen = length(worldBitangent);
    vs_fg.worldBitangent = (worldBitangentLen < 1e-8)
        ? vec3(0.0)
        : worldBitangent / worldBitangentLen;

    gl_Position = projectionMatrix * viewMatrix * worldPos;

    // Shadow mapping
    vs_out_shadow.FragPos  = vec3(worldPos);
    vec3 shadowNormal = worldNormalMatrix * skinnedNormal;
    float shadowNormalLen = length(shadowNormal);
    vs_out_shadow.Normal   = (shadowNormalLen < 1e-8) ? vec3(0.0) : shadowNormal / shadowNormalLen;
    vs_out_shadow.TexCoords           = texCoord0;
    vs_out_shadow.FragPosLightSpace   = lightSpaceMatrix * vec4(vs_out_shadow.FragPos, 1.0);
    vs_out_shadow.cameraPos           = cameraPos;
    vs_out_shadow.lightPos            = lightPos;

    // Reflection / world-space normal
    vs_fg.reflectionPosition = vec3(worldPos);
    vec3 reflNormal = worldNormalMatrix * skinnedNormal;
    float reflNormalLen = length(reflNormal);
    vs_fg.reflectionNormal = (reflNormalLen < 1e-8) ? vec3(0.0) : reflNormal / reflNormalLen;

    // Flat-shading varyings — the GS will replace flatNormal / reflectionFlatNormal
    // with the geometric face normal.  positionFlat / positionLinear just carry the
    // world position for constant-per-face lighting.
    vs_fg.flatNormal           = vs_fg.normal;
    vs_fg.reflectionFlatNormal = vs_fg.reflectionNormal;
    vs_fg.positionFlat         = vec3(worldPos);
    vs_fg.positionLinear       = vec3(worldPos);

    // TBN matrix for tangent-space lighting (depth / parallax mapping)
    vec3 T = mat3(modelViewMatrix) * skinnedTangent;
    float Tlen = length(T);
    if (Tlen < 1e-8) T = vec3(0.0); else T = T / Tlen;

    vec3 N = mat3(modelViewMatrix) * skinnedNormal;
    float Nlen = length(N);
    if (Nlen < 1e-8) N = vec3(0.0); else N = N / Nlen;

    vec3 B = normalize(cross(N, T));
    if (length(N) > 0.01 && length(T) > 0.01)
    {
        if (dot(cross(N, T), B) < 0.0)
            T = -T;
    }
    mat3 TBN = transpose(mat3(T, B, N));

    vs_fg.tangentLightPos = TBN * lightPos;
    vs_fg.tangentViewPos  = TBN * cameraPos;
    vs_fg.tangentFragPos  = TBN * vec3(worldPos);

    // Clip distances for hardware clipping
    vec4 viewPos = modelViewMatrix * skinnedPosition;
    gl_ClipDistance[0] = dot(clipPlaneX, viewPos);
    gl_ClipDistance[1] = dot(clipPlaneY, viewPos);
    gl_ClipDistance[2] = dot(clipPlaneZ, viewPos);
    gl_ClipDistance[3] = dot(clipPlane,  viewPos);
}
