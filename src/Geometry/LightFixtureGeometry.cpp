#include "LightFixtureGeometry.h"

#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr int kAroundSegments = 28;
}

void LightFixtureGeometry::segment(Profile& profile, float r0, float z0, float r1, float z1, float nr, float nz)
{
    profile.push_back({ r0, z0, nr, nz });
    profile.push_back({ r1, z1, nr, nz });
}

void LightFixtureGeometry::segmentAuto(Profile& profile, float r0, float z0, float r1, float z1, bool upward)
{
    const float dr = r1 - r0;
    const float dz = z1 - z0;
    const float length = std::sqrt(dr * dr + dz * dz);
    if (length < 1e-6f)
        return;
    // Walking upward along the outside of a cylinder (dz > 0) the outward normal is (+1, 0); walking downward it is the same.
    const float nr = (upward ? dz : -dz) / length;
    const float nz = (upward ? -dr : dr) / length;
    segment(profile, r0, z0, r1, z1, nr, nz);
}

void LightFixtureGeometry::sphereArc(Profile& profile, float centreZ, float radius, float angle0, float angle1, int steps)
{
    for (int i = 0; i <= steps; ++i)
    {
        const float a = angle0 + (angle1 - angle0) * static_cast<float>(i) / static_cast<float>(steps);
        profile.push_back({ radius * std::cos(a), centreZ + radius * std::sin(a), std::cos(a), std::sin(a) });
    }
}

void LightFixtureGeometry::addLathe(const Profile& profile, int segments)
{
    if (profile.size() < 2)
        return;
    const unsigned int base = static_cast<unsigned int>(_points.size() / 3);
    const int ring = segments + 1;   // the seam vertex is duplicated so the normals stay continuous
    for (const ProfilePoint& p : profile)
    {
        for (int j = 0; j <= segments; ++j)
        {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            _points.insert(_points.end(), { p.r * c, p.r * s, p.z });
            _normals.insert(_normals.end(), { p.nr * c, p.nr * s, p.nz });
        }
    }
    for (std::size_t i = 0; i + 1 < profile.size(); ++i)
    {
        for (int j = 0; j < segments; ++j)
        {
            const unsigned int a = base + static_cast<unsigned int>(i * ring + j);
            const unsigned int b = a + 1;
            const unsigned int c = a + static_cast<unsigned int>(ring);
            const unsigned int d = c + 1;
            _indices.insert(_indices.end(), { a, c, b, b, c, d });
        }
    }
}

void LightFixtureGeometry::addBox(float cx, float cy, float cz, float hx, float hy, float hz, float rotZ)
{
    const float c = std::cos(rotZ);
    const float s = std::sin(rotZ);
    const auto rotate = [c, s](float x, float y, float& ox, float& oy) { ox = x * c - y * s; oy = x * s + y * c; };

    struct Face { float nx, ny, nz; float ux, uy, uz; float vx, vy, vz; };
    const Face faces[6] = {
        { 1, 0, 0,   0, 1, 0,   0, 0, 1 },
        { -1, 0, 0,  0, 0, 1,   0, 1, 0 },
        { 0, 1, 0,   0, 0, 1,   1, 0, 0 },
        { 0, -1, 0,  1, 0, 0,   0, 0, 1 },
        { 0, 0, 1,   1, 0, 0,   0, 1, 0 },
        { 0, 0, -1,  0, 1, 0,   1, 0, 0 },
    };
    const float half[3] = { hx, hy, hz };
    for (const Face& f : faces)
    {
        const unsigned int base = static_cast<unsigned int>(_points.size() / 3);
        const float n[3] = { f.nx, f.ny, f.nz };
        const float u[3] = { f.ux, f.uy, f.uz };
        const float v[3] = { f.vx, f.vy, f.vz };
        for (int corner = 0; corner < 4; ++corner)
        {
            const float su = (corner == 0 || corner == 3) ? -1.0f : 1.0f;
            const float sv = (corner < 2) ? -1.0f : 1.0f;
            float p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = n[k] * half[k] + su * u[k] * half[k] + sv * v[k] * half[k];
            float px, py, nx, ny;
            rotate(p[0], p[1], px, py);
            rotate(n[0], n[1], nx, ny);
            _points.insert(_points.end(), { cx + px, cy + py, cz + p[2] });
            _normals.insert(_normals.end(), { nx, ny, n[2] });
        }
        _indices.insert(_indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

void LightFixtureGeometry::addLine(float x0, float y0, float z0, float x1, float y1, float z1)
{
    const unsigned int base = static_cast<unsigned int>(_points.size() / 3);
    _points.insert(_points.end(), { x0, y0, z0, x1, y1, z1 });
    _normals.insert(_normals.end(), { 0, 0, 1, 0, 0, 1 });
    _indices.insert(_indices.end(), { base, base + 1 });
}

void LightFixtureGeometry::addCircle(float z, float radius, int segments)
{
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        addLine(radius * std::cos(a0), radius * std::sin(a0), z, radius * std::cos(a1), radius * std::sin(a1), z);
    }
}

LightFixtureGeometry::LightFixtureGeometry(Part part)
    : _part(part)
{
    Profile profile;
    switch (part)
    {
    case Part::BulbGlass:
    {
        // Neck, then the sphere from below its equator up to the pole. The filament (the light) sits at the sphere's centre.
        segmentAuto(profile, 0.30f, -0.95f, 0.447f, -0.40f, true);
        sphereArc(profile, 0.0f, 0.6f, -0.7297f, kPi * 0.5f, 14);
        addLathe(profile, kAroundSegments);
        break;
    }
    case Part::BulbBase:
    {
        segmentAuto(profile, 0.30f, -0.95f, 0.30f, -1.05f, false);
        float z = -1.05f;
        for (int k = 0; k < 3; ++k)
        {
            segment(profile, 0.30f, z, 0.35f, z, 0.0f, 1.0f);                    // top of the ridge
            segment(profile, 0.35f, z, 0.35f, z - 0.07f, 1.0f, 0.0f);            // ridge side
            segment(profile, 0.35f, z - 0.07f, 0.30f, z - 0.07f, 0.0f, -1.0f);   // underside
            segment(profile, 0.30f, z - 0.07f, 0.30f, z - 0.17f, 1.0f, 0.0f);    // groove
            z -= 0.17f;
        }
        segmentAuto(profile, 0.30f, z, 0.14f, z - 0.16f, false);                 // rounded tip
        segment(profile, 0.14f, z - 0.16f, 0.0f, z - 0.16f, 0.0f, -1.0f);        // contact cap
        addLathe(profile, kAroundSegments);
        break;
    }
    case Part::BulbHalo:
    {
        sphereArc(profile, 0.0f, 1.25f, -kPi * 0.5f, kPi * 0.5f, 16);
        addLathe(profile, kAroundSegments);
        break;
    }
    case Part::SpotBody:
    {
        segment(profile, 0.0f, -1.4f, 0.5f, -1.4f, 0.0f, -1.0f);                 // rear cap
        segment(profile, 0.5f, -1.4f, 0.5f, 0.05f, 1.0f, 0.0f);                  // can
        segmentAuto(profile, 0.5f, 0.05f, 0.82f, 0.95f, true);                   // flared hood
        segment(profile, 0.82f, 0.95f, 0.76f, 0.95f, 0.0f, 1.0f);                // hood lip
        segmentAuto(profile, 0.76f, 0.95f, 0.5f, 0.25f, false);                  // inside of the hood
        // inward-facing: the walk goes down and towards the axis, the normal points at the axis and forward
        profile.back().nr = profile[profile.size() - 2].nr = -0.94f;
        profile.back().nz = profile[profile.size() - 2].nz = 0.35f;
        segment(profile, 0.5f, 0.25f, 0.0f, 0.25f, 0.0f, 1.0f);                  // floor behind the lens
        addLathe(profile, kAroundSegments);
        // U-shaped yoke below the can
        addBox(0.66f, -0.55f, -0.45f, 0.05f, 0.55f, 0.35f, 0.0f);
        addBox(-0.66f, -0.55f, -0.45f, 0.05f, 0.55f, 0.35f, 0.0f);
        addBox(0.0f, -1.1f, -0.45f, 0.71f, 0.06f, 0.35f, 0.0f);
        break;
    }
    case Part::SpotLens:
    {
        segment(profile, 0.0f, 0.3f, 0.5f, 0.3f, 0.0f, 1.0f);
        addLathe(profile, kAroundSegments);
        break;
    }
    case Part::SunHead:
    {
        // disc
        segment(profile, 0.0f, 0.08f, 0.55f, 0.08f, 0.0f, 1.0f);
        segment(profile, 0.55f, 0.08f, 0.55f, -0.08f, 1.0f, 0.0f);
        segment(profile, 0.55f, -0.08f, 0.0f, -0.08f, 0.0f, -1.0f);
        addLathe(profile, 36);
        // rays, long and short alternating
        for (int k = 0; k < 12; ++k)
        {
            const float a = 2.0f * kPi * static_cast<float>(k) / 12.0f;
            const float halfLength = (k % 2 == 0) ? 0.24f : 0.13f;
            const float centre = 0.72f + halfLength;
            addBox(centre * std::cos(a), centre * std::sin(a), 0.0f, halfLength, 0.045f, 0.05f, a);
        }
        // arrow along the direction of the light
        Profile arrow;
        segment(arrow, 0.0f, 0.1f, 0.07f, 0.1f, 0.0f, -1.0f);
        segment(arrow, 0.07f, 0.1f, 0.07f, 1.6f, 1.0f, 0.0f);
        segment(arrow, 0.07f, 1.6f, 0.2f, 1.6f, 0.0f, -1.0f);
        segmentAuto(arrow, 0.2f, 1.6f, 0.0f, 2.2f, true);
        addLathe(arrow, 16);
        break;
    }
    case Part::BeamCone:
    {
        for (int k = 0; k < 8; ++k)
        {
            const float a = 2.0f * kPi * static_cast<float>(k) / 8.0f;
            addLine(0.0f, 0.0f, 0.0f, std::cos(a), std::sin(a), 1.0f);
        }
        addCircle(1.0f, 1.0f, 48);
        break;
    }
    case Part::BeamRim:
    {
        addCircle(1.0f, 1.0f, 48);
        break;
    }
    case Part::LampShade:
    {
        // Cone shade over the bulb: a small cap at the top (-Z), flaring to a rim past the glass. Outward normal of the cone wall is
        // (0.879, -0.478) in (r, z); the inside is the same wall with the normal reversed.
        segment(profile, 0.0f, -1.95f, 0.25f, -1.95f, 0.0f, -1.0f);              // cap
        segmentAuto(profile, 0.25f, -1.95f, 1.5f, 0.35f, true);                  // outside of the cone
        segment(profile, 1.5f, 0.35f, 1.44f, 0.35f, 0.0f, 1.0f);                 // rim
        segment(profile, 1.44f, 0.35f, 0.2f, -1.9f, -0.879f, 0.478f);            // inside of the cone
        addLathe(profile, kAroundSegments);
        break;
    }
    case Part::BeamFill:
    {
        // The normal is radial and tilted back, but only the position's z matters: the shader fades the glow along the axis.
        segment(profile, 0.0f, 0.0f, 1.0f, 1.0f, 0.7071f, -0.7071f);
        addLathe(profile, 48);
        break;
    }
    }
    _primitiveMode = isLines() ? 1u : 4u;
}
