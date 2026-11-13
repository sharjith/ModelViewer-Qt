#pragma once

#include "MeshGeometry.h"

#include <vector>

// CPU geometry of the light gizmo fixtures, built from a few lathed profiles and boxes. Every part is modelled in the fixture's
// own frame: the light's emitter is the origin and it aims along +Z; "up" for the yoke and the bulb is +Y. One unit is about half
// of the fixture's body length, so the viewport scales a fixture by a world size that keeps it constant on screen.
//
// A part is either a triangle mesh (housing or emissive) or, for the beam cone, a set of lines (see isLines()).
class LightFixtureGeometry : public MeshGeometry
{
public:
    enum class Part
    {
        BulbGlass,   // emissive: glass envelope with a tapered neck
        BulbBase,    // housing: ribbed screw base
        BulbHalo,    // glow: a larger sphere drawn additively around the glass
        SpotBody,    // housing: can with a flared hood and a U-shaped yoke
        SpotLens,    // emissive: the lens disc inside the hood
        SunHead,     // emissive: sphere, spikes and a direction arrow
        BeamCone,    // lines: apex at the origin, rim at z = 1 with radius 1 (scale z and xy separately)
        BeamRim,     // lines: the circle at z = 1 with radius 1 (the inner cone angle)
        BeamFill,    // glow: the cone surface as triangles (apex at the origin, rim at z = 1, radius 1), drawn additively
        LampShade    // housing, drawn translucent: flat cone between the glass and the holder (the main light hangs the bulb upside down)
    };

    explicit LightFixtureGeometry(Part part);
    bool isLines() const { return _part == Part::BeamCone || _part == Part::BeamRim; }

private:
    // A point of a profile revolved around Z, with the surface normal in the (r, z) plane.
    struct ProfilePoint
    {
        float r, z, nr, nz;
    };
    using Profile = std::vector<ProfilePoint>;

    static void segment(Profile& profile, float r0, float z0, float r1, float z1, float nr, float nz);
    // Flat segment whose outward normal is derived from the direction of travel: `upward` for a profile walked from low to high z.
    static void segmentAuto(Profile& profile, float r0, float z0, float r1, float z1, bool upward);
    static void sphereArc(Profile& profile, float centreZ, float radius, float angle0, float angle1, int steps);

    void addLathe(const Profile& profile, int segments);
    void addBox(float cx, float cy, float cz, float hx, float hy, float hz, float rotZ);
    // A cone along the direction (dx, dy, dz) from the origin: base circle at `baseDistance`, tip at `tipDistance`.
    void addCone(float dx, float dy, float dz, float baseDistance, float tipDistance, float baseRadius, int segments);
    void addLine(float x0, float y0, float z0, float x1, float y1, float z1);
    void addCircle(float z, float radius, int segments);

    Part _part;
};
