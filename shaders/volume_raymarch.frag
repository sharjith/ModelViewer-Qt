#version 450 core

out vec4 FragColor;

uniform sampler3D volumeValues;
uniform sampler3D volumeValidity;
uniform sampler1D transferFunction;
uniform sampler2D sceneDepth;
uniform mat4 inverseViewProjection;
uniform mat4 inverseModel;
uniform vec3 boxMinimum;
uniform vec3 boxMaximum;
uniform vec3 voxelSize;
uniform vec2 framebufferSize;
uniform vec2 viewportOrigin;
uniform vec2 viewportSize;
uniform vec3 axisClipEnabled;
uniform vec3 axisClipThreshold;
uniform vec3 axisClipSign;
uniform bool boxClipEnabled;
uniform bool boxClipKeepInside;
uniform vec3 boxClipMinimum;
uniform vec3 boxClipMaximum;

bool intersectBox(vec3 origin, vec3 direction, out float enter, out float leave)
{
    vec3 safeDirection = mix(vec3(1.0e-20), direction, greaterThan(abs(direction), vec3(1.0e-20)));
    vec3 a = (boxMinimum - origin) / safeDirection;
    vec3 b = (boxMaximum - origin) / safeDirection;
    vec3 lo = min(a, b), hi = max(a, b);
    enter = max(max(lo.x, lo.y), lo.z);
    leave = min(min(hi.x, hi.y), hi.z);
    return leave > max(enter, 0.0);
}

vec3 unproject(vec2 ndc, float depth)
{
    vec4 p = inverseViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / framebufferSize;
    // Camera NDC is relative to the current GL viewport. That viewport is half-sized in multi-view and deliberately
    // shifted in compare mode, while depth lookup remains relative to the full framebuffer.
    vec2 ndc = ((gl_FragCoord.xy - viewportOrigin) / viewportSize) * 2.0 - 1.0;
    vec3 worldNear = unproject(ndc, 0.0);
    vec3 worldFar = unproject(ndc, 1.0);
    vec3 worldDirection = normalize(worldFar - worldNear);
    vec3 localOrigin = (inverseModel * vec4(worldNear, 1.0)).xyz;
    vec3 localDirection = (inverseModel * vec4(worldDirection, 0.0)).xyz;

    float enter, leave;
    if (!intersectBox(localOrigin, localDirection, enter, leave))
        discard;
    enter = max(enter, 0.0);

    float opaqueDepth = texture(sceneDepth, uv).r;
    if (opaqueDepth < 0.999999)
    {
        vec3 opaqueWorld = unproject(ndc, opaqueDepth);
        float opaqueDistance = dot(opaqueWorld - worldNear, worldDirection);
        leave = min(leave, opaqueDistance);
    }
    if (leave <= enter)
        discard;

    vec3 extent = boxMaximum - boxMinimum;
    float localStep = max(min(voxelSize.x, min(voxelSize.y, voxelSize.z)) * 0.65, 1.0e-8);
    float stepLength = localStep / max(length(localDirection), 1.0e-12);
    stepLength = max(stepLength, (leave - enter) / 900.0);
    vec4 accumulated = vec4(0.0);
    float t = enter + 0.5 * stepLength;
    for (int sampleIndex = 0; sampleIndex < 1024 && t < leave && accumulated.a < 0.985; ++sampleIndex, t += stepLength)
    {
        vec3 localPoint = localOrigin + t * localDirection;
        vec3 worldPoint = worldNear + t * worldDirection;
        vec3 axisDistance = axisClipSign * (worldPoint - axisClipThreshold);
        if ((axisClipEnabled.x > 0.5 && axisDistance.x < 0.0)
            || (axisClipEnabled.y > 0.5 && axisDistance.y < 0.0)
            || (axisClipEnabled.z > 0.5 && axisDistance.z < 0.0))
            continue;
        if (boxClipEnabled)
        {
            bool insideBox = all(greaterThanEqual(worldPoint, boxClipMinimum))
                          && all(lessThanEqual(worldPoint, boxClipMaximum));
            if ((boxClipKeepInside && !insideBox) || (!boxClipKeepInside && insideBox))
                continue;
        }
        vec3 texCoord = (localPoint - boxMinimum) / extent;
        float validity = texture(volumeValidity, texCoord).r;
        if (validity < 0.25)
            continue;
        float value = clamp(texture(volumeValues, texCoord).r, 0.0, 1.0);
        vec4 sampleColor = texture(transferFunction, value);
        // The curve describes opacity per voxel; correct it when sampling more or less densely than one voxel.
        sampleColor.a = 1.0 - pow(max(1.0 - sampleColor.a, 0.0), stepLength * length(localDirection) / localStep);
        sampleColor.rgb *= sampleColor.a;
        accumulated.rgb += (1.0 - accumulated.a) * sampleColor.rgb;
        accumulated.a += (1.0 - accumulated.a) * sampleColor.a;
    }
    if (accumulated.a <= 0.001)
        discard;
    FragColor = vec4(accumulated.rgb / accumulated.a, accumulated.a);
}
