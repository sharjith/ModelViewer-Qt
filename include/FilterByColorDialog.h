#pragma once

#include <QDialog>
#include <QVector3D>

class QLabel;
class QPushButton;
class QSlider;
class ModelViewer;
class QMdiSubWindow;
class QShowEvent;

// "Selection -> Filter by Color..." - lets the user pick a target color and
// live-previews the viewport selection as every mesh in the scene whose
// representative color (see MeshColorUtils.h - material albedo, or averaged
// per-vertex color for Point Set Reconstruction meshes) matches within a
// tolerance, so Show Only/Hide can act on the result immediately.
//
// Non-modal, per-document singleton, MDI-activation-synced, live-preview
// undo-merging - same shape as FilterByMaterialDialog; see that class's
// header doc comment for the full rationale.
class FilterByColorDialog : public QDialog
{
	Q_OBJECT
public:
	// initialColor: pre-fills the target swatch, e.g. the first currently-
	// selected mesh's own representative color if there is one - covers
	// "sample from a mesh" without a separate live viewport-pick interaction
	// (select the mesh first, then open this dialog).
	explicit FilterByColorDialog(ModelViewer* modelViewer,
	                              const QVector3D& initialColor,
	                              QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;

private slots:
	void onPickColorClicked();
	void onToleranceChanged(int sliderValue);
	void onShowOnlyClicked();
	void onHideClicked();
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);

private:
	// Recomputes _matchingIndices against a FRESH mesh store (the scene may
	// have changed while this dialog was open/hidden), updates the match
	// count label and button enablement, and pushes the result as the live
	// viewport selection (undo-merged, see applyLiveSelection() in
	// FilterByMaterialDialog.cpp for the identical mechanism).
	void updateMatches();
	void updateSwatch();

	ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
	QVector3D _targetColor;
	float _tolerance = 0.05f; // Euclidean distance in linear RGB

	// The swatch itself IS the "pick a color" control - a wide, colored
	// button spanning the Target Color group, rather than a small fixed-size
	// swatch beside a separate button (the two used to leave a large dead
	// empty area in the group box).
	QPushButton* _swatch = nullptr;
	QSlider* _toleranceSlider = nullptr;
	QLabel* _toleranceValueLabel = nullptr; // live numeric readout beside the slider
	QLabel* _matchCountLabel = nullptr;
	QPushButton* _showOnlyButton = nullptr;
	QPushButton* _hideButton = nullptr;
};
