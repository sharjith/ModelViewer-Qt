#pragma once

// The faces of the linear volume cells as rings of local corner indices - shared by the volume cutter (ResultSlice) and the streamline
// tracer (ResultStreamlines). The corner nodes come first in every cell layout, so a quadratic cell uses the table of its linear form.
// Only the node sets and the cyclic order matter, not the winding.

#include "ResultDataset.h"

#include <cstdint>

namespace resultcell
{
	struct FaceRing
	{
		std::uint8_t count;
		std::uint8_t v[4]; // local corner indices
	};

	inline constexpr FaceRing kTet[4] = { { 3, { 0, 1, 3, 0 } }, { 3, { 1, 2, 3, 0 } }, { 3, { 2, 0, 3, 0 } }, { 3, { 0, 2, 1, 0 } } };
	inline constexpr FaceRing kHex[6] = { { 4, { 0, 4, 7, 3 } }, { 4, { 1, 2, 6, 5 } }, { 4, { 0, 1, 5, 4 } }, { 4, { 3, 7, 6, 2 } }, { 4, { 0, 3, 2, 1 } }, { 4, { 4, 5, 6, 7 } } };
	inline constexpr FaceRing kWedge[5] = { { 3, { 0, 2, 1, 0 } }, { 3, { 3, 4, 5, 0 } }, { 4, { 0, 1, 4, 3 } }, { 4, { 1, 2, 5, 4 } }, { 4, { 2, 0, 3, 5 } } };
	inline constexpr FaceRing kPyramid[5] = { { 4, { 0, 3, 2, 1 } }, { 3, { 0, 1, 4, 0 } }, { 3, { 1, 2, 4, 0 } }, { 3, { 2, 3, 4, 0 } }, { 3, { 3, 0, 4, 0 } } };

	// The rings of a tetrahedron, hexahedron, wedge or pyramid (or the corner form of a quadratic one): their count, with `out` pointing at them.
	// 0 (and `out` null) for any other cell type, polyhedra included.
	inline int faceRingsFor(ResultCellType type, const FaceRing*& out)
	{
		switch (resultCellCornerType(type))
		{
		case ResultCellType::Tetra:      out = kTet;     return 4;
		case ResultCellType::Hexahedron: out = kHex;     return 6;
		case ResultCellType::Wedge:      out = kWedge;   return 5;
		case ResultCellType::Pyramid:    out = kPyramid; return 5;
		default: break;
		}
		out = nullptr;
		return 0;
	}
}
