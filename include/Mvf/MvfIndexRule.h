#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace Mvf
{
	// Whether a primitive that came back with no index data is legitimately unindexed (and so should still be loaded),
	// as opposed to a triangle mesh or a damaged file (which should be dropped).
	//
	// Point and line primitives are unindexed by nature: SceneMesh draws them with glDrawArrays when its index list is
	// empty (glTF point clouds / line sets and Plot3D's Line / Scatter / Quiver sites), and the writer saves them with a
	// zero-length index accessor. The index reader returns empty for a zero-element accessor but ALSO for a missing,
	// out-of-range or damaged one, so emptiness alone cannot say "unindexed". Only a file that names no index accessor, or
	// names one that explicitly holds zero elements, qualifies; a non-empty accessor that failed to read is damage and would
	// otherwise draw as garbage.
	inline bool primitiveMayBeUnindexed(const QJsonObject& primitive, const QJsonArray& accessors)
	{
		constexpr int kPoints = 0, kLines = 1, kLineLoop = 2, kLineStrip = 3, kTriangles = 4; // glTF / GL primitive modes
		const int mode = primitive.value(QStringLiteral("mode")).toInt(kTriangles);
		const bool pointOrLine = mode == kPoints || mode == kLines || mode == kLineLoop || mode == kLineStrip;
		if (!pointOrLine)
			return false;
		if (!primitive.contains(QStringLiteral("indices")))
			return true;
		const int accessor = primitive.value(QStringLiteral("indices")).toInt(-1);
		return accessor >= 0 && accessor < accessors.size()
			&& accessors[accessor].toObject().value(QStringLiteral("count")).toInt(-1) == 0;
	}
}
