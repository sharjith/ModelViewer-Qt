#pragma once

#include "Plot3DData.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class ModelViewer;

class Plot3DPanel : public QDialog
{
	Q_OBJECT

public:
	// modelViewer receives the constructed plot and its data-derived axis layout. Null is tolerated defensively.
	explicit Plot3DPanel(ModelViewer* modelViewer, QWidget* parent = nullptr);

private:
	void loadCsvFile();
	void pasteData();
	void refreshPreview();
	// Repopulates the column-role combos from _table's headers, keeping each combo's current selection if the
	// column count/name is still valid (so re-parsing after a delimiter change doesn't reset the user's mapping).
	void refreshColumnCombos();
	// Reads the primitive + column mapping, builds a Plot3DDataset then a mesh, adds it to the active document's
	// scene, and gives the viewport an axis-box layout derived from the built data's own bounds.
	void buildPlot();
	// Quiver's own path out of buildPlot(): unlike Surface/Line/Scatter, arrows are not a mesh Plot3D owns - they
	// reuse SimulationGlyphController directly (see docs/plot3d_blueprint.md section 5), anchored to a small
	// GL_POINTS SceneMesh built at the arrow base positions.
	void buildQuiverPlot(const Plot3DDataset& dataset, const QString& baseName);

	ModelViewer* _modelViewer = nullptr;
	QComboBox* _delimiter = nullptr;
	QCheckBox* _header = nullptr;
	QPlainTextEdit* _source = nullptr;
	QTableWidget* _preview = nullptr;
	QLabel* _status = nullptr;

	QComboBox* _primitive = nullptr;
	QComboBox* _columnX = nullptr;
	QComboBox* _columnY = nullptr;
	QComboBox* _columnZ = nullptr;
	QComboBox* _columnValue = nullptr; // optional; first entry means "none - use Z"
	QComboBox* _columnU = nullptr; // Quiver only: vector component columns
	QComboBox* _columnV = nullptr;
	QComboBox* _columnW = nullptr;
	QComboBox* _columnBase = nullptr;  // Bar only; optional, defaults to 0
	QComboBox* _columnWidth = nullptr; // Bar only; optional, defaults to 0.8
	QComboBox* _columnDepth = nullptr;
	QPushButton* _buildButton = nullptr;

	Plot3DCsvTable _table; // last successfully parsed table, kept for buildPlot() (refreshPreview() only shows it)
};
