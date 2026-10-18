#pragma once

#include <QPointF>
#include <QVector>

// The opacity transfer function every Voxel plot is drawn with: used when the plot is built or previewed, when its colour
// map changes, and when a saved document is reopened - one definition so the three cannot drift apart.
//
// Zero is empty, while nonzero occupancy stays visible with an opacity that rises with the supplied value. A hard 0.5
// cutoff makes a deliberately soft-edged input such as voxel_sphere.csv look much smaller than its own grid.
inline QVector<QPointF> plot3DVoxelOpacity()
{
	return { QPointF(0.0, 0.0), QPointF(0.149, 0.0), QPointF(0.15, 0.12), QPointF(0.5, 0.58), QPointF(1.0, 0.85) };
}
