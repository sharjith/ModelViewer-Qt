#pragma once

#include "Plot3DData.h"
#include "Plot3DFormula.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QSpinBox;
class QFormLayout;
class QGroupBox;
class QPushButton;
class QTableWidget;
class ModelViewer;

class Plot3DPanel : public QDialog
{
	Q_OBJECT

public:
	// modelViewer receives the constructed plot and its data-derived axis layout. Null is tolerated defensively.
	explicit Plot3DPanel(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~Plot3DPanel() override;

private:
	void loadCsvFile();
	void pasteData();
	void refreshPreview();
	void refreshFormulaPreview();
	void refreshParametricPreview();
	void refreshParametricCurvePreview();
	void refreshFormulaVectorPreview();
	void refreshImplicitPreview();
	void previewPlot();
	void clearPreview();
	void updateSourceMode();
	void applyFormulaPreset();
	void applyParametricPreset();
	void applyParametricCurvePreset();
	void applyFormulaVectorPreset();
	void applyImplicitPreset();
	void buildParametricPlot();
	void buildParametricCurvePlot();
	void buildFormulaVectorPlot();
	// Repopulates the column-role combos from _table's headers. A new schema
	// resets roles by their semantic header names (x/y/z/u/v/w/base/etc.);
	// re-parsing the same schema retains deliberate user selections.
	void refreshColumnCombos(bool resetForNewSchema = false);
	void updateScatterOptions();
	// Reads the primitive + column mapping, builds a Plot3DDataset then a mesh, adds it to the active document's
	// scene, and gives the viewport an axis-box layout derived from the built data's own bounds.
	void buildPlot();
	// Quiver's own path out of buildPlot(): unlike Surface/Line/Scatter, arrows are not a mesh Plot3D owns - they
	// reuse SimulationGlyphController directly (see docs/plot3d_blueprint.md section 5), anchored to a small
	// GL_POINTS SceneMesh built at the arrow base positions.
	void buildQuiverPlot(const Plot3DDataset& dataset, const QString& baseName);
	// Voxel plots reuse the volume ray-marcher. The scene mesh is an otherwise-hidden transform/visibility proxy
	// spanning the grid, while the occupancy field itself lives in the renderer's 3-D texture.
	void buildVoxelPlot(const Plot3DDataset& dataset, const QString& baseName);

	ModelViewer* _modelViewer = nullptr;
	QComboBox* _delimiter = nullptr;
	QComboBox* _sourceMode = nullptr;
	QWidget* _tableSourceWidget = nullptr;
	QWidget* _mappingWidget = nullptr;
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
	QComboBox* _columnError = nullptr;
	QCheckBox* _stemEnabled = nullptr;
	QCheckBox* _errorBarsEnabled = nullptr;
	QDoubleSpinBox* _stemBaseZ = nullptr;
	QPushButton* _buildButton = nullptr;
	QGroupBox* _formulaGroup = nullptr;
	QComboBox* _formulaPreset = nullptr;
	QComboBox* _parametricPreset = nullptr;
	QComboBox* _parametricCurvePreset = nullptr;
	QComboBox* _formulaVectorPreset = nullptr;
	QComboBox* _implicitPreset = nullptr;
	QLineEdit* _formulaTitle = nullptr;
	QLineEdit* _formulaExpression = nullptr;
	QLineEdit* _parametricX = nullptr;
	QLineEdit* _parametricY = nullptr;
	QLineEdit* _parametricZ = nullptr;
	QDoubleSpinBox* _formulaXMinimum = nullptr;
	QDoubleSpinBox* _formulaXMaximum = nullptr;
	QDoubleSpinBox* _formulaYMinimum = nullptr;
	QDoubleSpinBox* _formulaYMaximum = nullptr;
	QDoubleSpinBox* _formulaZMinimum = nullptr;
	QDoubleSpinBox* _formulaZMaximum = nullptr;
	QSpinBox* _formulaXSamples = nullptr;
	QSpinBox* _formulaYSamples = nullptr;
	QSpinBox* _formulaZSamples = nullptr;
	QFormLayout* _formulaLayout = nullptr;
	QLabel* _formulaPresetLabel = nullptr;
	QLabel* _parametricPresetLabel = nullptr;
	QLabel* _parametricCurvePresetLabel = nullptr;
	QLabel* _formulaVectorPresetLabel = nullptr;
	QLabel* _implicitPresetLabel = nullptr;
	QLabel* _formulaExpressionLabel = nullptr;
	QLabel* _parametricXLabel = nullptr;
	QLabel* _parametricYLabel = nullptr;
	QLabel* _parametricZLabel = nullptr;
	QLabel* _formulaXRangeLabel = nullptr;
	QLabel* _formulaYRangeLabel = nullptr;
	QLabel* _formulaZRangeLabel = nullptr;
	QLabel* _formulaParametersLabel = nullptr;
	QFormLayout* _formulaParameters = nullptr;
	QHash<QString, QDoubleSpinBox*> _formulaParameterEditors;
	QVector<Plot3DFormulaPreset> _formulaPresets;
	QVector<Plot3DParametricPreset> _parametricPresets;
	QVector<Plot3DParametricCurvePreset> _parametricCurvePresets;
	QVector<Plot3DFormulaVectorPreset> _formulaVectorPresets;
	QVector<Plot3DImplicitPreset> _implicitPresets;

	Plot3DCsvTable _table; // last successfully parsed table, kept for buildPlot() (refreshPreview() only shows it)
};
