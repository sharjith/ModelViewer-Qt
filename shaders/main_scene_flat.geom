#version 450 core

// Passthrough geometry shader for flat shading.
// Computes the true geometric face normal via cross(edge0, edge1) from
// world-space vertex positions provided by main_scene_flat.vert through the
// VS_FLAT_GEOM interface block.  The result is placed into v_flatNormal
// (view-space) and v_reflectionFlatNormal (world-space) for all three emitted
// vertices so that the fragment shader can use them directly.
//
// WHY a dedicated interface block for ALL varyings?
// NVIDIA's GLSL compiler (OpenGL 4.5 / Shader 4.50 NVIDIA) rejects any GS that
// declares BOTH "in T name[]" (input array) AND "out T name" (output scalar)
// with the same identifier — error C1038.  By funnelling all VS→GS data through
// VS_FLAT_GEOM (different name from any out v_*) there is zero naming conflict.
//
// Used only by _fgFlatShader (DisplayMode::FLATSHADED, GL_TRIANGLES meshes).

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

// ---- Full VS→GS interface block (matches main_scene_flat.vert) ---------------
// All member names are DIFFERENT from the out v_* outputs below → no C1038.
in VS_FLAT_GEOM {
    vec3 position;             // world-space vertex position
    vec3 normal;               // view-space smooth normal
    vec4 color;
    vec4 rawVertexColor;
    vec2 texCoord0;
    vec2 texCoord1;
    vec2 texCoord2;
    vec2 texCoord3;
    vec3 tangent;
    vec3 bitangent;
    vec3 worldTangent;
    vec3 worldBitangent;
    vec3 tangentLightPos;
    vec3 tangentViewPos;
    vec3 tangentFragPos;
    vec3 reflectionPosition;
    vec3 reflectionNormal;     // world-space smooth normal
    vec3 flatNormal;           // placeholder (GS overrides)
    vec3 reflectionFlatNormal; // placeholder (GS overrides)
    vec3 positionFlat;
    vec3 positionLinear;
    vec4 analysisColor;
} gs_fg_in[];

// ---- Shadow interface block input -------------------------------------------
in VS_OUT_SHADOW {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 cameraPos;
    vec3 lightPos;
} gs_in_shadow[];

// ---- Fragment-shader outputs (same names as FS "in" declarations) ------------
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
} fs_in_shadow;

uniform mat4 viewMatrix;

vec3 gseSafeNorm(vec3 v, vec3 fallback)
{
    float len = length(v);
    return len > 1e-8 ? v / len : fallback;
}

void main()
{
    // ---- Compute true geometric face normal from world-space positions --------
    vec3 edge0 = gs_fg_in[1].position - gs_fg_in[0].position;
    vec3 edge1 = gs_fg_in[2].position - gs_fg_in[0].position;
    vec3 faceWorld = cross(edge0, edge1);

    // Average smooth world normals as orientation reference (ensures outward sign).
    vec3 smoothWorldRef = gseSafeNorm(
        gs_fg_in[0].reflectionNormal
        + gs_fg_in[1].reflectionNormal
        + gs_fg_in[2].reflectionNormal,
        vec3(0.0, 0.0, 1.0));
    faceWorld = gseSafeNorm(faceWorld, smoothWorldRef);
    if (dot(faceWorld, smoothWorldRef) < 0.0)
        faceWorld = -faceWorld;

    // View-space face normal.
    vec3 smoothViewRef = gseSafeNorm(
        gs_fg_in[0].normal + gs_fg_in[1].normal + gs_fg_in[2].normal,
        vec3(0.0, 0.0, 1.0));
    vec3 faceView = gseSafeNorm(mat3(viewMatrix) * faceWorld, smoothViewRef);

    // ---- Emit three vertices -------------------------------------------------
    for (int i = 0; i < 3; ++i)
    {
        v_position           = gs_fg_in[i].position;
        v_normal             = gs_fg_in[i].normal;
        v_color              = gs_fg_in[i].color;
        v_rawVertexColor     = gs_fg_in[i].rawVertexColor;
        v_analysisColor      = gs_fg_in[i].analysisColor;
        v_texCoord0          = gs_fg_in[i].texCoord0;
        v_texCoord1          = gs_fg_in[i].texCoord1;
        v_texCoord2          = gs_fg_in[i].texCoord2;
        v_texCoord3          = gs_fg_in[i].texCoord3;
        v_tangent            = gs_fg_in[i].tangent;
        v_bitangent          = gs_fg_in[i].bitangent;
        v_worldTangent       = gs_fg_in[i].worldTangent;
        v_worldBitangent     = gs_fg_in[i].worldBitangent;
        v_tangentLightPos    = gs_fg_in[i].tangentLightPos;
        v_tangentViewPos     = gs_fg_in[i].tangentViewPos;
        v_tangentFragPos     = gs_fg_in[i].tangentFragPos;
        v_reflectionPosition = gs_fg_in[i].reflectionPosition;
        v_reflectionNormal   = gs_fg_in[i].reflectionNormal;
        v_positionFlat       = gs_fg_in[i].positionFlat;
        v_positionLinear     = gs_fg_in[i].positionLinear;

        // Override flat normals with the true geometric face normal.
        v_flatNormal           = faceView;
        v_reflectionFlatNormal = faceWorld;

        fs_in_shadow.FragPos           = gs_in_shadow[i].FragPos;
        fs_in_shadow.Normal            = gs_in_shadow[i].Normal;
        fs_in_shadow.TexCoords         = gs_in_shadow[i].TexCoords;
        fs_in_shadow.FragPosLightSpace = gs_in_shadow[i].FragPosLightSpace;
        fs_in_shadow.cameraPos         = gs_in_shadow[i].cameraPos;
        fs_in_shadow.lightPos          = gs_in_shadow[i].lightPos;

        gl_Position        = gl_in[i].gl_Position;
        gl_ClipDistance[0] = gl_in[i].gl_ClipDistance[0];
        gl_ClipDistance[1] = gl_in[i].gl_ClipDistance[1];
        gl_ClipDistance[2] = gl_in[i].gl_ClipDistance[2];
        gl_ClipDistance[3] = gl_in[i].gl_ClipDistance[3];
        EmitVertex();
    }
    EndPrimitive();
}
