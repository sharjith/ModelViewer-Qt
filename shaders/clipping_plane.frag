#version 450 core

in vec3 vWorldPos;

uniform bool selected;
uniform vec3 planeColor;

// texture sampler (kept for compatibility)
uniform sampler2D hatchMap;

// World-space mapping (set from C++)
uniform vec3 hatchOrigin;
uniform vec3 uDir;
uniform vec3 vDir;
uniform float worldUnitsPerTile; // spacing: world units per repeat

// Procedural hatch controls (set from C++)
uniform float hatchThickness;    // fraction of tile (0..0.5). e.g. 0.012
uniform float hatchIntensity;    // 0..1
uniform int   hatchPattern;      // 0 = 45° only; 1 = 135°; 2 = vertical; 3 = horizontal; 3 = vert+hor; 4 = 45+135
uniform vec3  hatchLineColor;    // color of hatch lines (e.g. black)

// Toggle between procedural and texture-based hatch
uniform bool useTexture;         // when true, sample hatchMap instead of procedural
uniform vec2 textureFlip;        // (1,1) normal; (-1,1) flip U; (1,-1) flip V

// Multi-plane corner-cut trim (set from C++, see drawSectionCapping()'s doc
// comment above these uniforms' assignment for the full derivation). This
// quad caps ONE axis; otherApply{X,Y,Z} says whether each OTHER axis is
// currently active and should further restrict where this cap is exposed.
// A fragment only belongs to this cap if it is on the REMOVED side of every
// other active axis - if another active plane still keeps it, that column
// of material was never cut away, so there is no cap surface there.
uniform bool  otherApplyX;
uniform bool  otherApplyY;
uniform bool  otherApplyZ;
uniform float otherThreshX;
uniform float otherThreshY;
uniform float otherThreshZ;
uniform bool  otherFlippedX;
uniform bool  otherFlippedY;
uniform bool  otherFlippedZ;

out vec4 fragColor;

// ---------- helpers ----------
float aaStripe(float coordWU, float spacingWU, float halfThicknessWU)
{
    float t = coordWU / spacingWU;
    float frac = fract(t + 0.5) - 0.5;
    float fw = fwidth(frac);
    return 1.0 - smoothstep(halfThicknessWU/spacingWU - fw*0.5,
                            halfThicknessWU/spacingWU + fw*0.5,
                            abs(frac));
}

vec2 rotate2(vec2 p, float ang)
{
    float c = cos(ang);
    float s = sin(ang);
    return vec2(c*p.x - s*p.y, s*p.x + c*p.y);
}

// ---------- main ----------
void main()
{
    // Multi-plane corner-cut trim - see the uniform declarations above.
    // "Kept by that other axis" (should discard) is the exact complement of
    // VisibilityComputationHelper's isBoundingBoxFullyClipped_* convention.
    if (otherApplyX && (otherFlippedX ? (vWorldPos.x >= otherThreshX) : (vWorldPos.x <= otherThreshX)))
        discard;
    if (otherApplyY && (otherFlippedY ? (vWorldPos.y >= otherThreshY) : (vWorldPos.y <= otherThreshY)))
        discard;
    if (otherApplyZ && (otherFlippedZ ? (vWorldPos.z >= otherThreshZ) : (vWorldPos.z <= otherThreshZ)))
        discard;

    // world-space coords on plane
    vec3 rel = vWorldPos - hatchOrigin;
    vec2 uv_w = vec2(dot(rel, uDir), dot(rel, vDir)); // world units

    float spacing = max(1e-6, worldUnitsPerTile);
    float halfThickWU = 0.5 * clamp(hatchThickness, 0.0005, 0.5) * spacing;

    // If using texture-based hatch, sample texture and composite with plane color
    if (useTexture)
    {
        // compute repeating UVs in texture space (one repeat per 'spacing' world units)
        vec2 uv = uv_w / spacing;

        // Apply optional flip (flip is either +1 or -1 per component)
        uv *= textureFlip;

        // Because we want the pattern to be anchored / stable, offset uv so the plane origin maps
        // predictably; using hatchOrigin projects naturally; if desired, you may add a phase offset.
        // Sample texture (texture should be setup with GL_REPEAT)
        vec3 hatch = texture(hatchMap, uv).rgb;

        // Mix texture with plane color controlled by hatchIntensity
        vec4 base = vec4(planeColor, 1.0);
        fragColor = mix(base, vec4(hatch, 1.0), 0.25);

        if (selected) {
            fragColor = mix(fragColor, vec4(1.0, 0.65, 0.0, 1.0), 0.5);
        }
        return;
    }

    // Procedural branch
    float accum = 0.0;

    if (hatchPattern == 0) {
        vec2 r = rotate2(uv_w, 0.78539816339); // 45°
        accum = aaStripe(r.x, spacing, halfThickWU);
    }
    else if (hatchPattern == 1) {
        vec2 r = rotate2(uv_w, -0.78539816339); // 135°
        accum = aaStripe(r.x, spacing, halfThickWU);
    }
    else if (hatchPattern == 2) { // horizontal
        accum = aaStripe(uv_w.y, spacing, halfThickWU);
    }
    else if (hatchPattern == 3) { // vertical
        accum = aaStripe(uv_w.x, spacing, halfThickWU);        
    }
    else if (hatchPattern == 4) { // grid
        float s1 = aaStripe(uv_w.x, spacing, halfThickWU);
        float s2 = aaStripe(uv_w.y, spacing, halfThickWU);
        accum = clamp((s1 + s2) * 0.5, 0.0, 1.0);
    }
    else { // diagonal criss cross
        vec2 r = rotate2(uv_w, 0.78539816339); // 45°
        float s1 = aaStripe(r.x, spacing, halfThickWU);
        r = rotate2(uv_w, -0.78539816339);      // 135°
        float s2 = aaStripe(r.x, spacing, halfThickWU);
        accum = clamp((s1 + s2) * 0.5, 0.0, 1.0);
    }

    vec3 hatchCol = hatchLineColor;
    vec3 base = planeColor;
    vec3 col = mix(base, hatchCol, clamp(accum * hatchIntensity, 0.0, 1.0));

    if (selected) col = mix(col, vec3(1.0, 0.65, 0.0), 0.5);

    fragColor = vec4(col, 1.0);
}
