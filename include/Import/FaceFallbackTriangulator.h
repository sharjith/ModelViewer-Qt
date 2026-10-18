#pragma once

#include <gp_Pnt.hxx>
#include <Poly_Triangulation.hxx>
#include <TopoDS_Face.hxx>
#include "OcctDeprecatedAliases.h"

#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// FaceFallbackTriangulator
//
// Last resort for a B-Rep face OpenCASCADE's mesher (BRepMesh) produced no triangles for - typically a face whose
// edge curves-on-surface (pcurves) are invalid in the source STEP/IGES file, which BRepCheck_Analyzer still calls
// "valid" but which the mesher cannot use. Leaving such a face out punches a hole in the tessellated part, so the
// part is no longer watertight and Mass Properties rejects it.
//
// captureBoundary() + triangulate() build the face from its BOUNDARY, without relying on its pcurves:
//   1. every edge's polyline is taken from the discretization a NEIGHBOURING face already carries on that shared
//      edge (Poly_PolygonOnTriangulation), so the new triangles use exactly the same points and the part stays
//      watertight against them; only an edge no neighbour discretized falls back to sampling its 3D curve;
//   2. the boundary points are projected onto the face's surface to get (u, v), unwrapping periodic surfaces along
//      each loop (a full cone/cylinder face is a rectangle in (u, v) thanks to its seam edge);
//   3. a constrained Delaunay triangulation of the (u, v) polygon (holes handled by nesting) gives the triangles;
//      on a curved surface interior points are added on a grid, at about the boundary's own spacing, and placed on
//      the surface, so a big curved face is not flattened into chords.
// The nodes are returned in the same coordinate frame the neighbouring faces' nodes end up in once their locations
// are applied - i.e. ALREADY transformed; use an identity location with the result. Triangles are counter-clockwise
// in (u, v) - the same convention as BRepMesh's - so the caller's usual "flip for a REVERSED face" applies.
//
// Returns a null handle if the face cannot be handled (no surface, an unclosed loop, a failure in the
// triangulation); the caller then leaves the face out as before.
// ---------------------------------------------------------------------------
namespace FaceFallbackTriangulator
{
	// The face's boundary loops as discretized by its NEIGHBOURS, captured up front: BRepTools::Clean() on the face
	// (which the converter does before re-meshing it) removes the shared edges' polygons, so this must run first.
	struct Boundary
	{
		std::vector<std::vector<gp_Pnt>> loops; // one closed polyline per wire (outer loop first, then holes), global coordinates
		double diag = 0.0;                      // extent of the loops
		bool valid = false;
		// per loop: (index of each piece's first point in the loop, a description of it) - for the import log
		std::vector<std::vector<std::pair<size_t, std::string>>> pieces;
		// per loop: indices of the points that start right after a degenerate edge (a cone/sphere pole, where the whole
		// (u, v) width of the face collapses into one 3D point)
		std::vector<std::vector<size_t>> poleBreaks;
		std::string failure;                    // why it is not valid (for the import log)
	};

	// edgeToFaces: edge -> faces map over the faces of the part being converted (TopExp::MapShapesAndAncestors).
	Boundary captureBoundary(const TopoDS_Face& face, const TopTools_IndexedDataMapOfShapeListOfShape& edgeToFaces);

	// The triangulation of `face` from its captured boundary (see the file comment); null if it cannot be built.
	// `failure`, if given, receives the reason when the result is null.
	Handle(Poly_Triangulation) triangulate(const TopoDS_Face& face, const Boundary& boundary, std::string* failure = nullptr);
}
