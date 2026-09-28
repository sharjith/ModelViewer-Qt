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
	// modelViewer is used to reach its ViewportWidget for the axis-box preview (see previewAxisBox()) - null is
	// tolerated (the preview button is disabled), the same defensive style RtRenderDialog's modelViewer takes.
	explicit Plot3DPanel(ModelViewer* modelViewer, QWidget* parent = nullptr);

protected:
	// Clears whatever axis-box preview this panel pushed to the viewport, so it doesn't linger once the dialog
	// that owns it is gone - closeEvent() rather than the destructor alone, since QDialog::reject()/accept() calls
	// close() but a WA_DeleteOnClose dialog's destructor can run after the viewport has already redrawn once.
	void closeEvent(QCloseEvent* event) override;

private:
	void loadCsvFile();
	void pasteData();
	void refreshPreview();
	// Builds a hardcoded axis-box layout (fixed range, not from the loaded data) and pushes it to the viewport -
	// lets the axis box/tick/label rendering be checked visually before any real primitive builder exists, per
	// docs/plot3d_blueprint.md section 6 step 3.
	void previewAxisBox();
	// Repopulates the column-role combos from _table's headers, keeping each combo's current selection if the
	// column count/name is still valid (so re-parsing after a delimiter change doesn't reset the user's mapping).
	void refreshColumnCombos();
	// Reads the primitive + column mapping, builds a Plot3DDataset then a mesh, adds it to the active document's
	// scene, and points the axis-box preview at the built data's own bounds instead of previewAxisBox()'s fixed
	// test range.
	void buildPlot();

	ModelViewer* _modelViewer = nullptr;
	QComboBox* _delimiter = nullptr;
	QCheckBox* _header = nullptr;
	QPlainTextEdit* _source = nullptr;
	QTableWidget* _preview = nullptr;
	QLabel* _status = nullptr;
	QPushButton* _previewAxisButton = nullptr;

	QComboBox* _primitive = nullptr;
	QComboBox* _columnX = nullptr;
	QComboBox* _columnY = nullptr;
	QComboBox* _columnZ = nullptr;
	QComboBox* _columnValue = nullptr; // optional; first entry means "none - use Z"
	QPushButton* _buildButton = nullptr;

	Plot3DCsvTable _table; // last successfully parsed table, kept for buildPlot() (refreshPreview() only shows it)
};
