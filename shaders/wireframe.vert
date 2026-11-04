#version 450 core

// Lightweight wireframe vertex shader.
// Attribute names and locations match main_scene.vert so existing VAOs bind correctly.

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec2 texCoord0;
layout(location = 9) in vec4 jointIndices;
layout(location = 10) in vec4 jointWeights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform bool hasVertexColors;
uniform bool hasSkinning;
uniform int  jointCount;
uniform mat4 jointMatrices[128];

// Same uniforms as wireframe.frag (one program, one value). Used only to pull a highlighted mesh's lines in front of
// coincident ones: see the depth bias at the end of main().
uniform bool isWireframePass;
uniform bool hovered;
uniform bool selected;

// About two to three steps of a 24-bit depth buffer in NDC. Relative to the buffer's own resolution, so it stays below any real
// gap between surfaces, yet is enough for the selected or hovered mesh to win against copies of itself at the same position.
// Kept small on purpose: a step spans more real distance the farther away the surface is.
const float kHighlightDepthBias = 3.0e-7;

out vec2 v_texCoord;
out vec3 v_worldNormal;
out vec4 v_vertexColor;

mat4 computeSkinMatrix()
{
    if (!hasSkinning || jointCount <= 0)
        return mat4(1.0);

    mat4 skin = mat4(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float weight = jointWeights[i];
        if (weight <= 0.0) continue;
        int idx = int(jointIndices[i]);
        if (idx < 0 || idx >= jointCount || idx >= 128) continue;
        skin += jointMatrices[idx] * weight;
        totalWeight += weight;
    }
    return totalWeight <= 0.0 ? mat4(1.0) : skin;
}

void main()
{
    mat4 skinMatrix = computeSkinMatrix();
    vec4 skinnedPos  = skinMatrix * vec4(vertexPosition, 1.0);
    vec3 skinnedNorm = mat3(skinMatrix) * vertexNormal;

    v_texCoord    = texCoord0;
    // mat3(modelMatrix) skips inverse-transpose; acceptable for wireframe depth cues.
    v_worldNormal = normalize(mat3(modelMatrix) * skinnedNorm);
    v_vertexColor = hasVertexColors ? vertexColor : vec4(1.0);
    gl_Position   = projectionMatrix * viewMatrix * modelMatrix * skinnedPos;

    // Hover and selection colour the lines only when isWireframePass is false (Wireframe and Hollow Mesh modes). Meshes that sit
    // at exactly the same place would otherwise decide by draw order which one's lines are visible, and the highlight could be
    // hidden by an unhighlighted twin. Clip-space shift, so it works for GL_LINES too, where a polygon offset does not apply.
    if (!isWireframePass && (hovered || selected))
        gl_Position.z -= kHighlightDepthBias * gl_Position.w;
}
