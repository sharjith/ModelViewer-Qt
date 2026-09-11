#pragma once

#include <QVector3D>

class SceneMesh;

// This mesh's single representative color, for Filter by Color matching:
// the averaged per-vertex color if the mesh genuinely has one - CGAL Point
// Set Reconstruction output preserves vertex color and has no discrete
// material otherwise usable here - falling back to its material's flat
// albedo factor otherwise. "Genuinely has one" is NOT decided by
// RenderableMesh::hasVertexColors() alone - that flag is true for
// essentially every mesh (a plain, non-vertex-colored import still uploads
// a uniform white colors buffer), so it can't distinguish real per-vertex
// data from that default. Instead: a uniform buffer is only treated as the
// default placeholder if it's ALSO (close to) white - a genuinely uniform
// but non-white vertex-colored mesh (e.g. a Point Set Reconstruction scan
// of a flat-colored object) is real data and is trusted - see the .cpp for
// the two confirmed-real bugs this two-part check fixes (every mesh
// reporting white; a uniform non-white real color being discarded too).
// Deliberately never samples a bound albedo texture - matches this app's
// established "don't chase the long tail" pattern elsewhere (e.g.
// Cylindrical Diameter's fillet-boundary decision).
QVector3D meshRepresentativeColor(const SceneMesh* mesh);
