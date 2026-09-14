#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class ModelViewer;
class SceneNode;

// ---------------------------------------------------------------------------
// ImportUnitsDialog (scene tree, right-click an imported file's node ->
// Import Units...)
//
// Sets/corrects ONE imported file's real-world length unit
// (SceneNode::importUnit) - the first link in resolveEffectiveImportUnit()'s
// resolution chain (see LengthUnits.h). Deliberately scoped to a single file
// node, not to whatever multi-mesh selection Mass Properties/Surface
// Analysis happen to have open at the time - a unit is a property of the
// imported FILE, not of a viewing dialog's current selection.
//
// Small modal utility dialog, pure C++ widget construction (no .ui file) -
// same convention MassPropertiesDialog/BatchRenderViewsDialog already use.
// ---------------------------------------------------------------------------
class ImportUnitsDialog : public QDialog
{
	Q_OBJECT
public:
	// modelViewer is needed only to preview the unit that's currently in
	// EFFECT for this file (fileNode's own importUnit if explicitly set,
	// else the document's defaultImportUnit, else Millimeter - the same
	// three-step order resolveEffectiveImportUnit() uses, just without its
	// mesh->file-node indirection since fileNode is already known here) and
	// to mark the document modified on accept.
	explicit ImportUnitsDialog(ModelViewer* modelViewer, SceneNode* fileNode, QWidget* parent = nullptr);

private slots:
	void onAccept();

private:
	ModelViewer* _modelViewer; // not owned
	SceneNode* _fileNode;      // not owned - the synthetic file node this dialog edits
	QComboBox* _unitCombo = nullptr;
};
