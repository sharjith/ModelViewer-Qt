#pragma once

#include "Plot3DAssembly.h"
#include "Plot3DData.h"
#include "Plot3DFormula.h"
#include "Plot3DGenerate.h"
#include "Plot3DPathlines.h"
#include "Plot3DSession.h"

#include <QDialog>
#include <QHash>
#include <QUuid>
#include <QVector>

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

// The Add / Edit 3D Plot dialog. It only COLLECTS: the data source, its definition (expressions, ranges, parameters, or a CSV table
// with its column roles) and the presentation choices. Everything it turns those into goes through three layers it does not own:
//   Plot3DGenerate   (Core)  definition -> plot data                (generatePlot3D, plot3DMeshForDataset)
//   Plot3DAssembly   (UI)    plot data -> preview / scene plot       (plot3DShowPreview, plot3DCommit, plot3DRebuild)
// so Preview, Build and Rebuild share one path each. The implementation is split by concern:
//   Plot3DPanel.cpp         construction, CSV tables and their column roles, editing a CSV plot
//   Plot3DPanelSources.cpp  the data sources: which controls each shows, presets, the definition <-> widgets, status line
//   Plot3DPanelBuild.cpp    Preview / Build / Rebuild and reopening generated plots
class Plot3DPanel : public QDialog
{
	Q_OBJECT

public:
	// modelViewer receives the constructed plot and its data-derived axis layout. Null is tolerated defensively.
	explicit Plot3DPanel(ModelViewer* modelViewer, QWidget* parent = nullptr);
	~Plot3DPanel() override;
	// Closing (the X button, Close, or a rebuild finishing) remembers the dialog's position and size.
	void done(int result) override;
	void loadPlotForEditing(const QUuid& meshUuid);

private:
	// ---- Plot3DPanel.cpp: tables and column roles ------------------------------------------------------------------------------
	void loadCsvFile();
	void pasteData();
	void refreshPreview();
	// Repopulates the column-role combos from _table's headers. A new schema resets roles by their semantic header names
	// (x/y/z/u/v/w/base/etc.); re-parsing the same schema retains deliberate user selections.
	void refreshColumnCombos(bool resetForNewSchema = false);
	Plot3DColumnMapping columnMapping() const;
	void refreshTimeSeriesColumns();
	Plot3DTimeSeriesColumns timeSeriesColumns() const;
	void updateScatterOptions();
	void updateContourOverlayRow();
	void loadCsvPlotForEditing(const Plot3DSession& session);
	void loadTimeSeriesForEditing(const Plot3DSession& session);

	// ---- Plot3DPanelSources.cpp: the data sources -----------------------------------------------------------------------------
	Plot3DSourceKind currentSource() const;
	void updateSourceMode();                       // shows the controls the selected source uses (see its table of descriptors)
	QComboBox* presetCombo(Plot3DSourceKind kind) const;
	void applyPreset();                            // the selected source's preset -> the widgets
	void browseImage();                            // the image plane's file chooser
	void fitImageRanges();                         // the plane's two ranges -> the picture's aspect ratio
	void applySpec(const Plot3DGeneratedSpec& spec); // a definition -> the widgets (presets and Edit Plot both use it)
	Plot3DGeneratedSpec currentGeneratedSpec() const; // the widgets -> a definition
	void setParameterEditors(const std::vector<std::pair<QString, double>>& parameters);
	void refreshFormulaPreview();                  // the formula surface also shows its sampled table
	void refreshGeneratedStatus();                 // validates the definition and reports it in the status line
	void refreshSourcePreview();                   // whichever refresh the selected source needs
	Plot3DPrimitive formulaPrimitive() const;      // Surface or Contour for a formula surface
	Plot3DMeshOptions currentMeshOptions() const;
	bool generateCurrent(Plot3DGenerated& out, QString* error);

	// ---- Plot3DPanelBuild.cpp: preview, build, rebuild ----------------------------------------------------------------------
	void previewPlot();
	void clearPreview();
	void buildPlot();
	void rebuildCurrent();
	void loadGeneratedPlotForEditing(const Plot3DSession& session);
	QString previewTitle() const;
	QString suggestedPlotName(const Plot3DGenerated& generated, const Plot3DMeshOptions& options) const;
	Plot3DCsvBinding csvBinding() const;

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
	QComboBox* _columnFillTo = nullptr; // Line fill: the second curve's Z column (none = the base plane)
	QLabel* _fillToLabel = nullptr;
	QCheckBox* _stemEnabled = nullptr;
	QCheckBox* _errorBarsEnabled = nullptr;
	QCheckBox* _scatterFillEnabled = nullptr;
	QDoubleSpinBox* _stemBaseZ = nullptr;
	QPushButton* _buildButton = nullptr;
	QWidget* _contourOverlayRow = nullptr;
	QComboBox* _contourOverlayMode = nullptr;

	// CSV time series (pathlines): which columns hold time, position and velocity, and how many seeds / time steps to trace.
	QWidget* _timeSeriesWidget = nullptr;
	QComboBox* _tsTime = nullptr; QComboBox* _tsX = nullptr; QComboBox* _tsY = nullptr; QComboBox* _tsZ = nullptr;
	QComboBox* _tsU = nullptr; QComboBox* _tsV = nullptr; QComboBox* _tsW = nullptr;
	QSpinBox* _tsSeeds = nullptr; QSpinBox* _tsSteps = nullptr;

	// The one definition editor the generated sources share (each source shows the rows it uses).
	QGroupBox* _formulaGroup = nullptr;
	QFormLayout* _formulaLayout = nullptr;
	QComboBox* _formulaPreset = nullptr;
	QComboBox* _parametricPreset = nullptr;
	QComboBox* _parametricCurvePreset = nullptr;
	QComboBox* _formulaVectorPreset = nullptr; // shared by the vector field and the streamlines
	QComboBox* _implicitPreset = nullptr;
	QComboBox* _pathlinePreset = nullptr;
	QLabel* _formulaPresetLabel = nullptr;
	QLabel* _parametricPresetLabel = nullptr;
	QLabel* _parametricCurvePresetLabel = nullptr;
	QLabel* _formulaVectorPresetLabel = nullptr;
	QLabel* _implicitPresetLabel = nullptr;
	QLabel* _pathlinePresetLabel = nullptr;
	QComboBox* _formulaPlotType = nullptr;
	QLabel* _formulaPlotTypeLabel = nullptr;
	QLineEdit* _formulaTitle = nullptr;
	QLineEdit* _formulaExpression = nullptr;
	QLabel* _formulaExpressionLabel = nullptr;
	QLineEdit* _parametricX = nullptr;
	QLineEdit* _parametricY = nullptr;
	QLineEdit* _parametricZ = nullptr;
	QLabel* _parametricXLabel = nullptr;
	QLabel* _parametricYLabel = nullptr;
	QLabel* _parametricZLabel = nullptr;
	QDoubleSpinBox* _formulaXMinimum = nullptr;
	QDoubleSpinBox* _formulaXMaximum = nullptr;
	QDoubleSpinBox* _formulaYMinimum = nullptr;
	QDoubleSpinBox* _formulaYMaximum = nullptr;
	QDoubleSpinBox* _formulaZMinimum = nullptr;
	QDoubleSpinBox* _formulaZMaximum = nullptr;
	QSpinBox* _formulaXSamples = nullptr;
	QSpinBox* _formulaYSamples = nullptr;
	QSpinBox* _formulaZSamples = nullptr;
	QLabel* _formulaXRangeLabel = nullptr;
	QLabel* _formulaYRangeLabel = nullptr;
	QLabel* _formulaZRangeLabel = nullptr;
	QLabel* _formulaParametersLabel = nullptr;
	QLabel* _imageFileLabel = nullptr;
	QLineEdit* _imageFile = nullptr;
	QPushButton* _imageBrowse = nullptr;
	QLabel* _imagePlaneLabel = nullptr;
	QComboBox* _imagePlane = nullptr;
	QFormLayout* _formulaParameters = nullptr;
	QHash<QString, QDoubleSpinBox*> _formulaParameterEditors;
	QHash<const QComboBox*, QVector<Plot3DPresetEntry>> _presetEntries; // each preset combo's presets, as definitions

	Plot3DCsvTable _table; // last successfully parsed table, kept for Preview / Build (refreshPreview() only shows it)
	QUuid _editingMeshUuid;
};
