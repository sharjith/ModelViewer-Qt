#pragma once

#include <QDialog>

class QTableWidget;
class QLabel;
class QPushButton;
class ModelViewer;

// ---------------------------------------------------------------------------
// MassPropertiesDialog (Tools -> Mass Properties...)
//
// Per-mesh + assembly-total report for the current selection: volume,
// surface area, mass, geometric centroid, and mass-weighted center of mass -
// built on the corrected MeshProperties math (see that class's own doc
// comments for the centroid-sign/stale-cache/fake-density bugs this
// replaces). Split out of the old tree-context-menu "Mesh Info" dump
// (ModelViewer::displaySelectedMeshInfo(), now trimmed to just points/
// triangles/memory) specifically because THIS data needs real widgets, not
// a single QMessageBox string: an open mesh has no valid volume, a mesh
// whose material has no assigned density has no valid mass, and a mixed
// selection needs a known-value subtotal + excluded-mesh count rather than
// a single number that silently ignores the excluded ones.
//
// Pure C++ widget construction (no .ui file), matching BatchRenderViewsDialog's
// convention from this same session (a hand-written .ui XML file can't be
// visually verified without Designer, while plain C++ construction is
// exactly the same code either way and stays fully inspectable).
//
// Modal, one-shot report - recomputed fresh every time it's opened (same
// "snapshot, not a live-tracking panel" shape as the old context-menu
// dialog it replaces), not a persistent dockable tool.
// ---------------------------------------------------------------------------
class MassPropertiesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit MassPropertiesDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

private:
	void populate();

	ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

	QLabel* _noSelectionLabel = nullptr;
	QTableWidget* _table = nullptr;
	QLabel* _totalsLabel = nullptr;
	QPushButton* _closeButton = nullptr;
};
