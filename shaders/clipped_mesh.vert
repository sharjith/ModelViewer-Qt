#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 9) in vec4 jointIndices;
layout(location = 10) in vec4 jointWeights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

uniform vec4 clipPlaneX;
uniform vec4 clipPlaneY;
uniform vec4 clipPlaneZ;
// user defined clip plane
uniform vec4 clipPlane;
// Box clipping (4th clipping mode) - see main_scene.vert.
uniform vec4 clipPlaneBox[6];
uniform bool clipPlaneBoxEnabled;
uniform bool hasSkinning;
uniform int jointCount;
uniform mat4 jointMatrices[128];

out float v_clipDistX;
out float v_clipDistY;
out float v_clipDistZ;
out float v_clipDist;

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

    return totalWeight > 0.0 ? skin : mat4(1.0);
}

void main()
{
    vec4 posedPosition = computeSkinMatrix() * vec4(vertexPosition, 1.0);
    vec4 viewPos = viewMatrix * modelMatrix * posedPosition;
    gl_Position = projectionMatrix * viewPos;

    v_clipDistX = dot(clipPlaneX, viewPos);
    v_clipDistY = dot(clipPlaneY, viewPos);
    v_clipDistZ = dot(clipPlaneZ, viewPos);
    v_clipDist =  dot(clipPlane, viewPos);

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
        gl_ClipDistance[0] = v_clipDistX;
        gl_ClipDistance[1] = v_clipDistY;
        gl_ClipDistance[2] = v_clipDistZ;
        gl_ClipDistance[3] = v_clipDist;
        gl_ClipDistance[4] = 1.0;
        gl_ClipDistance[5] = 1.0;
    }
}
