#pragma once

#include <QUuid>
#include <QString>
#include <QSet>

// A named, user-saved mesh selection - "Selection -> Save Selection Set...".
// Document-level, not per-file (see SceneGraph.h's Measurements/Annotations,
// same reasoning: a set can span meshes imported from multiple loaded
// files, so it isn't owned by any single one). Stores mesh UUIDs, not the
// runtime int ids SelectionManager/ViewportWidget use elsewhere - ids are
// session-local and would go stale across a save/reload or even a plain
// mesh add/remove, while UUIDs are stable identity.
struct SelectionSet
{
	QUuid id;
	QString name;
	QSet<QUuid> meshUuids;
};
