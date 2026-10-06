#include "Plot3DPanel.h"

#include "Plot3DAssembly.h"
#include "Plot3DData.h"
#include "Plot3DFormula.h"
#include "Plot3DGenerate.h"
#include "ModelViewer.h"
#include "PathUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

Plot3DPanel::Plot3DPanel(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Add 3D Plot"));
	setWindowIcon(QIcon(QStringLiteral(":/icons/res/plot3d.png")));
	resize(760, 560);
	// Position and size are remembered between uses (one saved geometry shared by Add and Edit).
	{
		QSettings settings;
		const QByteArray geometry = settings.value(QStringLiteral("plot3dPanel/geometry")).toByteArray();
		if (!geometry.isEmpty())
			restoreGeometry(geometry);
	}

	auto* layout = new QVBoxLayout(this);
	_sourceMode = new QComboBox(this);
	auto addSource = [this](const QString& text, Plot3DSourceKind kind) { _sourceMode->addItem(text, plot3DSourceInt(kind)); };
	addSource(tr("CSV file or pasted data"), Plot3DSourceKind::Csv);
	addSource(tr("Formula surface"), Plot3DSourceKind::FormulaSurface);
	addSource(tr("Parametric surface"), Plot3DSourceKind::ParametricSurface);
	addSource(tr("Parametric curve"), Plot3DSourceKind::ParametricCurve);
	addSource(tr("Formula vector field"), Plot3DSourceKind::FormulaVectorField);
	addSource(tr("Implicit surface"), Plot3DSourceKind::ImplicitSurface);
	addSource(tr("Formula streamlines"), Plot3DSourceKind::FormulaStreamlines);
	addSource(tr("Formula pathlines (time-dependent)"), Plot3DSourceKind::FormulaPathlines);
	addSource(tr("CSV time series (pathlines)"), Plot3DSourceKind::CsvTimeSeries);
	// Labelled so it is clear the first combo chooses where the plot's data comes from (a file, or a formula / definition).
	auto* sourceRow = new QHBoxLayout();
	sourceRow->addWidget(new QLabel(tr("Data source:"), this));
	sourceRow->addWidget(_sourceMode, 1);
	layout->addLayout(sourceRow);
	_tableSourceWidget = new QWidget(this);
	auto* tableSourceLayout = new QVBoxLayout(_tableSourceWidget);
	tableSourceLayout->setContentsMargins(0, 0, 0, 0);
	auto* sourceButtons = new QHBoxLayout();
	auto* openButton = new QPushButton(tr("Open CSV..."), this);
	auto* pasteButton = new QPushButton(tr("Paste"), this);
	auto* parseButton = new QPushButton(tr("Refresh Preview"), this);
	sourceButtons->addWidget(openButton);
	sourceButtons->addWidget(pasteButton);
	sourceButtons->addStretch();
	sourceButtons->addWidget(parseButton);
	tableSourceLayout->addLayout(sourceButtons);

	auto* options = new QFormLayout();
	_delimiter = new QComboBox(this);
	_delimiter->addItem(tr("Comma"), QStringLiteral(","));
	_delimiter->addItem(tr("Semicolon"), QStringLiteral(";"));
	_delimiter->addItem(tr("Tab"), QStringLiteral("\t"));
	_header = new QCheckBox(tr("First row contains column names"), this);
	_header->setChecked(true);
	options->addRow(tr("Delimiter:"), _delimiter);
	options->addRow(QString(), _header);
	tableSourceLayout->addLayout(options);

	_source = new QPlainTextEdit(this);
	_source->setPlaceholderText(tr("Paste comma-separated X, Y, Z data here, or open a CSV file."));
	_source->setMinimumHeight(120);
	tableSourceLayout->addWidget(_source);

	_preview = new QTableWidget(this);
	_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
	_preview->setAlternatingRowColors(true);
	tableSourceLayout->addWidget(_preview, 1);

	_status = new QLabel(tr("Open or paste tabular data to preview it."), this);
	_status->setWordWrap(true);
	layout->addWidget(_tableSourceWidget, 1);
	layout->addWidget(_status);

	_formulaGroup = new QGroupBox(tr("Formula surface"), this);
	_formulaLayout = new QFormLayout(_formulaGroup);
	// One preset combo per source with presets (the vector field and the streamlines share theirs); each keeps its presets as
	// ready-to-use definitions (see plot3DPresetEntries()).
	auto makePresetCombo = [this](Plot3DSourceKind kind) {
		auto* combo = new QComboBox(_formulaGroup);
		const QVector<Plot3DPresetEntry> entries = plot3DPresetEntries(kind);
		for (const Plot3DPresetEntry& entry : entries)
			combo->addItem(entry.name);
		_presetEntries.insert(combo, entries);
		return combo;
	};
	_formulaPreset = makePresetCombo(Plot3DSourceKind::FormulaSurface);
	_parametricPreset = makePresetCombo(Plot3DSourceKind::ParametricSurface);
	_parametricCurvePreset = makePresetCombo(Plot3DSourceKind::ParametricCurve);
	_formulaVectorPreset = makePresetCombo(Plot3DSourceKind::FormulaVectorField);
	_implicitPreset = makePresetCombo(Plot3DSourceKind::ImplicitSurface);
	_pathlinePreset = makePresetCombo(Plot3DSourceKind::FormulaPathlines);
	_formulaTitle = new QLineEdit(_formulaGroup);
	_formulaExpression = new QLineEdit(_formulaGroup);
	_parametricX = new QLineEdit(_formulaGroup); _parametricY = new QLineEdit(_formulaGroup); _parametricZ = new QLineEdit(_formulaGroup);
	_formulaXMinimum = new QDoubleSpinBox(_formulaGroup); _formulaXMaximum = new QDoubleSpinBox(_formulaGroup);
	_formulaYMinimum = new QDoubleSpinBox(_formulaGroup); _formulaYMaximum = new QDoubleSpinBox(_formulaGroup);
	_formulaZMinimum = new QDoubleSpinBox(_formulaGroup); _formulaZMaximum = new QDoubleSpinBox(_formulaGroup);
	_formulaXSamples = new QSpinBox(_formulaGroup); _formulaYSamples = new QSpinBox(_formulaGroup); _formulaZSamples = new QSpinBox(_formulaGroup);
	for (QDoubleSpinBox* spin : { _formulaXMinimum, _formulaXMaximum, _formulaYMinimum, _formulaYMaximum, _formulaZMinimum, _formulaZMaximum }) { spin->setRange(-1.0e6, 1.0e6); spin->setDecimals(6); }
	for (QSpinBox* spin : { _formulaXSamples, _formulaYSamples, _formulaZSamples }) spin->setRange(2, 8192);
	_formulaPresetLabel = new QLabel(tr("Preset:"), _formulaGroup);
	_parametricPresetLabel = new QLabel(tr("Parametric preset:"), _formulaGroup);
	_parametricCurvePresetLabel = new QLabel(tr("Curve preset:"), _formulaGroup);
	_formulaVectorPresetLabel = new QLabel(tr("Vector preset:"), _formulaGroup);
	_implicitPresetLabel = new QLabel(tr("Implicit preset:"), _formulaGroup);
	_pathlinePresetLabel = new QLabel(tr("Pathline preset:"), _formulaGroup);
	_formulaExpressionLabel = new QLabel(tr("z ="), _formulaGroup);
	_parametricXLabel = new QLabel(tr("x(u,v) ="), _formulaGroup);
	_parametricYLabel = new QLabel(tr("y(u,v) ="), _formulaGroup);
	_parametricZLabel = new QLabel(tr("z(u,v) ="), _formulaGroup);
	_formulaXRangeLabel = new QLabel(tr("X range / samples:"), _formulaGroup);
	_formulaYRangeLabel = new QLabel(tr("Y range / samples:"), _formulaGroup);
	_formulaZRangeLabel = new QLabel(tr("Z range / samples:"), _formulaGroup);
	_formulaParametersLabel = new QLabel(tr("Parameters:"), _formulaGroup);
	_formulaLayout->addRow(_formulaPresetLabel, _formulaPreset);
	_formulaLayout->addRow(_parametricPresetLabel, _parametricPreset);
	_formulaLayout->addRow(_parametricCurvePresetLabel, _parametricCurvePreset);
	_formulaLayout->addRow(_formulaVectorPresetLabel, _formulaVectorPreset);
	_formulaLayout->addRow(_implicitPresetLabel, _implicitPreset);
	_formulaLayout->addRow(_pathlinePresetLabel, _pathlinePreset);
	// The Primitive combo below belongs to the table (CSV) mapping and is hidden for generated sources, so a formula
	// surface chooses between its two meaningful plot types here and drives that same combo.
	_formulaPlotTypeLabel = new QLabel(tr("Plot type:"), _formulaGroup);
	_formulaPlotType = new QComboBox(_formulaGroup);
	_formulaPlotType->addItem(tr("Surface"), static_cast<int>(Plot3DPrimitive::Surface));
	_formulaPlotType->addItem(tr("Contour (surface iso-lines)"), static_cast<int>(Plot3DPrimitive::Contour));
	_formulaLayout->addRow(_formulaPlotTypeLabel, _formulaPlotType);
	_formulaLayout->addRow(tr("Title:"), _formulaTitle);
	_formulaLayout->addRow(_formulaExpressionLabel, _formulaExpression);
	_formulaLayout->addRow(_parametricXLabel, _parametricX);
	_formulaLayout->addRow(_parametricYLabel, _parametricY);
	_formulaLayout->addRow(_parametricZLabel, _parametricZ);
	auto* xRange = new QHBoxLayout(); xRange->addWidget(_formulaXMinimum); xRange->addWidget(_formulaXMaximum); xRange->addWidget(_formulaXSamples); _formulaLayout->addRow(_formulaXRangeLabel, xRange);
	auto* yRange = new QHBoxLayout(); yRange->addWidget(_formulaYMinimum); yRange->addWidget(_formulaYMaximum); yRange->addWidget(_formulaYSamples); _formulaLayout->addRow(_formulaYRangeLabel, yRange);
	auto* zRange = new QHBoxLayout(); zRange->addWidget(_formulaZMinimum); zRange->addWidget(_formulaZMaximum); zRange->addWidget(_formulaZSamples); _formulaLayout->addRow(_formulaZRangeLabel, zRange);
	_formulaParameters = new QFormLayout(); _formulaLayout->addRow(_formulaParametersLabel, _formulaParameters);
	layout->addWidget(_formulaGroup);

	// Primitive + column mapping. Roles that apply only to one primitive remain in this static form so switching
	// primitive never destroys a mapping the user has already chosen.
	_mappingWidget = new QWidget(this);
	auto* mapping = new QFormLayout(_mappingWidget);
	_primitive = new QComboBox(this);
	_primitive->addItem(tr("Surface"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Surface)));
	_primitive->addItem(tr("Contour (surface iso-lines)"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Contour)));
	_primitive->addItem(tr("Line / Curve"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Line)));
	_primitive->addItem(tr("Scatter"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Scatter)));
	_primitive->addItem(tr("Bar / Histogram"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Bar)));
	_primitive->addItem(tr("Voxel / Volumetric"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Voxel)));
	_primitive->addItem(tr("Quiver (vector field)"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Quiver)));
	mapping->addRow(tr("Primitive:"), _primitive);

	auto* columnsRow = new QHBoxLayout();
	_columnX = new QComboBox(this);
	_columnY = new QComboBox(this);
	_columnZ = new QComboBox(this);
	_columnValue = new QComboBox(this);
	for (QComboBox* combo : { _columnX, _columnY, _columnZ, _columnValue })
		combo->setMinimumWidth(90);
	columnsRow->addWidget(new QLabel(tr("X:"), this)); columnsRow->addWidget(_columnX);
	columnsRow->addWidget(new QLabel(tr("Y:"), this)); columnsRow->addWidget(_columnY);
	columnsRow->addWidget(new QLabel(tr("Z:"), this)); columnsRow->addWidget(_columnZ);
	columnsRow->addWidget(new QLabel(tr("Colour value:"), this)); columnsRow->addWidget(_columnValue);
	columnsRow->addStretch();
	mapping->addRow(tr("Columns:"), columnsRow);

	// U/V/W are only meaningful for Quiver, but always shown (rather than dynamically hidden per primitive) for
	// the same reason "Colour value" always shows even though only some primitives use it - a simpler, static form.
	auto* vectorColumnsRow = new QHBoxLayout();
	_columnU = new QComboBox(this);
	_columnV = new QComboBox(this);
	_columnW = new QComboBox(this);
	for (QComboBox* combo : { _columnU, _columnV, _columnW })
		combo->setMinimumWidth(90);
	vectorColumnsRow->addWidget(new QLabel(tr("U:"), this)); vectorColumnsRow->addWidget(_columnU);
	vectorColumnsRow->addWidget(new QLabel(tr("V:"), this)); vectorColumnsRow->addWidget(_columnV);
	vectorColumnsRow->addWidget(new QLabel(tr("W:"), this)); vectorColumnsRow->addWidget(_columnW);
	vectorColumnsRow->addStretch();
	mapping->addRow(tr("Vector (Quiver):"), vectorColumnsRow);
	auto* barColumnsRow = new QHBoxLayout();
	_columnBase = new QComboBox(this);
	_columnWidth = new QComboBox(this);
	_columnDepth = new QComboBox(this);
	for (QComboBox* combo : { _columnBase, _columnWidth, _columnDepth }) combo->setMinimumWidth(90);
	barColumnsRow->addWidget(new QLabel(tr("Base:"), this)); barColumnsRow->addWidget(_columnBase);
	barColumnsRow->addWidget(new QLabel(tr("Width:"), this)); barColumnsRow->addWidget(_columnWidth);
	barColumnsRow->addWidget(new QLabel(tr("Depth:"), this)); barColumnsRow->addWidget(_columnDepth);
	barColumnsRow->addStretch();
	mapping->addRow(tr("Bar options:"), barColumnsRow);
	auto* scatterOptionsRow = new QHBoxLayout();
	_stemEnabled = new QCheckBox(tr("Draw stems to Base Z:"), this);
	_errorBarsEnabled = new QCheckBox(tr("Show error bars (±Z):"), this);
	_scatterFillEnabled = new QCheckBox(tr("Fill to Base Z:"), this);
	_columnError = new QComboBox(this);
	_stemBaseZ = new QDoubleSpinBox(this);
	_stemBaseZ->setRange(-1.0e12, 1.0e12);
	_stemBaseZ->setDecimals(6);
	_stemBaseZ->setValue(0.0);
	scatterOptionsRow->addWidget(_stemEnabled);
	scatterOptionsRow->addWidget(_stemBaseZ);
	scatterOptionsRow->addWidget(_errorBarsEnabled);
	scatterOptionsRow->addWidget(_columnError);
	scatterOptionsRow->addWidget(_scatterFillEnabled);
	scatterOptionsRow->addStretch();
	mapping->addRow(tr("Scatter / line options:"), scatterOptionsRow);
	layout->addWidget(_mappingWidget);

	// CSV time series: a vector field on a complete regular (t, x, y[, z]) grid, one row per node. Its own column choices (the
	// generic mapping above is hidden for it).
	_timeSeriesWidget = new QWidget(this);
	{
		auto* timeSeries = new QFormLayout(_timeSeriesWidget);
		timeSeries->setContentsMargins(0, 0, 0, 0);
		_tsTime = new QComboBox(_timeSeriesWidget); _tsX = new QComboBox(_timeSeriesWidget); _tsY = new QComboBox(_timeSeriesWidget);
		_tsZ = new QComboBox(_timeSeriesWidget); _tsU = new QComboBox(_timeSeriesWidget); _tsV = new QComboBox(_timeSeriesWidget);
		_tsW = new QComboBox(_timeSeriesWidget);
		for (QComboBox* combo : { _tsTime, _tsX, _tsY, _tsZ, _tsU, _tsV, _tsW })
			combo->setMinimumWidth(110);
		auto* positionRow = new QHBoxLayout();
		positionRow->addWidget(new QLabel(tr("X:"), this)); positionRow->addWidget(_tsX);
		positionRow->addWidget(new QLabel(tr("Y:"), this)); positionRow->addWidget(_tsY);
		positionRow->addWidget(new QLabel(tr("Z:"), this)); positionRow->addWidget(_tsZ);
		positionRow->addStretch();
		auto* velocityRow = new QHBoxLayout();
		velocityRow->addWidget(new QLabel(tr("U:"), this)); velocityRow->addWidget(_tsU);
		velocityRow->addWidget(new QLabel(tr("V:"), this)); velocityRow->addWidget(_tsV);
		velocityRow->addWidget(new QLabel(tr("W:"), this)); velocityRow->addWidget(_tsW);
		velocityRow->addStretch();
		timeSeries->addRow(tr("Time column:"), _tsTime);
		timeSeries->addRow(tr("Position columns:"), positionRow);
		timeSeries->addRow(tr("Velocity columns:"), velocityRow);
		_tsSeeds = new QSpinBox(_timeSeriesWidget); _tsSeeds->setRange(2, 128); _tsSeeds->setValue(12);
		_tsSteps = new QSpinBox(_timeSeriesWidget); _tsSteps->setRange(2, 2000); _tsSteps->setValue(400);
		auto* traceRow = new QHBoxLayout();
		traceRow->addWidget(_tsSeeds); traceRow->addWidget(new QLabel(tr("seeds"), this));
		traceRow->addWidget(_tsSteps); traceRow->addWidget(new QLabel(tr("time steps"), this));
		traceRow->addStretch();
		timeSeries->addRow(tr("Pathlines:"), traceRow);
		auto* hint = new QLabel(tr("One row per grid node of a complete regular grid in time, X and Y (and Z). Seeds are released along Y at the middle of the X range."), _timeSeriesWidget);
		hint->setWordWrap(true);
		timeSeries->addRow(hint);
		for (QComboBox* combo : { _tsTime, _tsX, _tsY, _tsZ, _tsU, _tsV, _tsW })
			connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::refreshGeneratedStatus);
		connect(_tsSeeds, qOverload<int>(&QSpinBox::valueChanged), this, &Plot3DPanel::refreshGeneratedStatus);
		connect(_tsSteps, qOverload<int>(&QSpinBox::valueChanged), this, &Plot3DPanel::refreshGeneratedStatus);
	}
	layout->addWidget(_timeSeriesWidget);

	// Every Surface-type plot (CSV, formula, parametric, implicit) can be built together with a contour overlay; the same
	// setting stays adjustable afterwards in the 3D Plot tab (which is the only place to change it for a reopened file).
	_contourOverlayRow = new QWidget(this);
	auto* contourOverlayLayout = new QFormLayout(_contourOverlayRow);
	contourOverlayLayout->setContentsMargins(0, 0, 0, 0);
	_contourOverlayMode = new QComboBox(_contourOverlayRow);
	_contourOverlayMode->addItem(tr("None"), 0);
	_contourOverlayMode->addItem(tr("On the surface"), 1);
	_contourOverlayMode->addItem(tr("On the base plane"), 2);
	contourOverlayLayout->addRow(tr("Contour lines:"), _contourOverlayMode);
	layout->addWidget(_contourOverlayRow);

	_buildButton = new QPushButton(tr("Build Plot"), this);
	_buildButton->setEnabled(false); // enabled once refreshPreview() has a non-empty table
	auto* previewButton = new QPushButton(tr("Preview"), this);
	auto* clearPreviewButton = new QPushButton(tr("Clear Preview"), this);
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addWidget(previewButton);
	buttonRow->addWidget(clearPreviewButton);
	buttonRow->addWidget(_buildButton);
	buttonRow->addStretch();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	buttonRow->addWidget(buttons);
	layout->addLayout(buttonRow);

	connect(openButton, &QPushButton::clicked, this, &Plot3DPanel::loadCsvFile);
	connect(pasteButton, &QPushButton::clicked, this, &Plot3DPanel::pasteData);
	connect(parseButton, &QPushButton::clicked, this, &Plot3DPanel::refreshSourcePreview);
	connect(_delimiter, &QComboBox::currentIndexChanged, this, &Plot3DPanel::refreshPreview);
	connect(_header, &QCheckBox::toggled, this, &Plot3DPanel::refreshPreview);
	connect(_sourceMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::updateSourceMode);
	for (QComboBox* combo : { _formulaPreset, _parametricPreset, _parametricCurvePreset, _formulaVectorPreset, _implicitPreset, _pathlinePreset })
		connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::applyPreset);
	connect(_formulaPlotType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		_primitive->setCurrentIndex(_primitive->findData(_formulaPlotType->currentData().toInt()));
		if (currentSource() == Plot3DSourceKind::FormulaSurface)
			refreshFormulaPreview();
	});
	connect(_primitive, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::updateScatterOptions);
	connect(_primitive, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::updateContourOverlayRow);
	connect(_stemEnabled, &QCheckBox::toggled, this, &Plot3DPanel::updateScatterOptions);
	connect(_errorBarsEnabled, &QCheckBox::toggled, this, &Plot3DPanel::updateScatterOptions);
	connect(_scatterFillEnabled, &QCheckBox::toggled, this, &Plot3DPanel::updateScatterOptions);
	connect(_stemEnabled, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked) { _errorBarsEnabled->setChecked(false); _scatterFillEnabled->setChecked(false); }
	});
	connect(_errorBarsEnabled, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked) { _stemEnabled->setChecked(false); _scatterFillEnabled->setChecked(false); }
	});
	connect(_scatterFillEnabled, &QCheckBox::toggled, this, [this](bool checked) {
		if (checked) { _stemEnabled->setChecked(false); _errorBarsEnabled->setChecked(false); }
	});
	connect(previewButton, &QPushButton::clicked, this, &Plot3DPanel::previewPlot);
	connect(clearPreviewButton, &QPushButton::clicked, this, &Plot3DPanel::clearPreview);
	connect(_buildButton, &QPushButton::clicked, this, &Plot3DPanel::buildPlot);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	refreshColumnCombos(); // seeds the column combos with their placeholder/default state before any data is loaded
	updateSourceMode();
	updateScatterOptions();
	updateContourOverlayRow();
}

void Plot3DPanel::done(int result)
{
	QSettings settings;
	settings.setValue(QStringLiteral("plot3dPanel/geometry"), saveGeometry());
	QDialog::done(result);
}

Plot3DPanel::~Plot3DPanel()
{
	clearPreview();
}

void Plot3DPanel::loadPlotForEditing(const QUuid& meshUuid)
{
	if (!_modelViewer)
		return;
	const QVector<Plot3DSession> sessions = _modelViewer->plot3DSessions();
	const auto it = std::find_if(sessions.cbegin(), sessions.cend(), [&meshUuid](const Plot3DSession& session) { return session.meshUuid == meshUuid; });
	if (it == sessions.cend())
		return;
	// A plot that came from a generated source reopens on its definition; a CSV time series on its table and column roles; any
	// other CSV-backed plot on its table. A plot with none of these cannot be edited.
	if (it->generated.valid && it->generated.sourceMode == plot3DSourceInt(Plot3DSourceKind::CsvTimeSeries))
		loadTimeSeriesForEditing(*it);
	else if (it->generated.valid && !it->editableCsv)
		loadGeneratedPlotForEditing(*it);
	else if (it->editableCsv)
		loadCsvPlotForEditing(*it);
}


void Plot3DPanel::loadCsvPlotForEditing(const Plot3DSession& session)
{
	clearPreview();
	_editingMeshUuid = session.meshUuid;
	updateContourOverlayRow(); // an existing plot's contour lines are changed in the 3D Plot tab, not here
	setWindowTitle(tr("Edit 3D Plot - %1").arg(session.name));
	_buildButton->setText(tr("Rebuild Plot"));
	_sourceMode->setCurrentIndex(_sourceMode->findData(0));
	_sourceMode->setEnabled(false);
	_primitive->setCurrentIndex(_primitive->findData(static_cast<int>(session.primitive)));
	_primitive->setEnabled(false);
	{
		const QSignalBlocker delimiterBlock(_delimiter), headerBlock(_header), sourceBlock(_source);
		_delimiter->setCurrentIndex(_delimiter->findData(QString(session.csvOptions.delimiter)));
		_header->setChecked(session.csvOptions.firstRowIsHeader);
		_source->setPlainText(session.csvSource);
	}
	updateSourceMode();
	refreshPreview();
	auto restoreColumn = [](QComboBox* combo, int column) {
		const int index = combo->findData(column);
		if (index >= 0)
			combo->setCurrentIndex(index);
	};
	restoreColumn(_columnX, session.columnMapping.x);
	restoreColumn(_columnY, session.columnMapping.y);
	restoreColumn(_columnZ, session.columnMapping.z);
	restoreColumn(_columnValue, session.columnMapping.value);
	restoreColumn(_columnU, session.columnMapping.u);
	restoreColumn(_columnV, session.columnMapping.v);
	restoreColumn(_columnW, session.columnMapping.w);
	restoreColumn(_columnBase, session.columnMapping.base);
	restoreColumn(_columnWidth, session.columnMapping.width);
	restoreColumn(_columnDepth, session.columnMapping.depth);
	restoreColumn(_columnError, session.columnMapping.error);
	_stemBaseZ->setValue(session.scatterBaseZ);
	_stemEnabled->setChecked(session.isStem);
	_errorBarsEnabled->setChecked(session.isErrorBars);
	_scatterFillEnabled->setChecked(session.isFilledScatter);
	// Rebuild keeps the renderer family stable. Data and role mappings remain fully editable, while changing a
	// point plot into stems/ribbons (or changing primitive) is still done by creating a new plot.
	_stemEnabled->setEnabled(false);
	_errorBarsEnabled->setEnabled(false);
	_scatterFillEnabled->setEnabled(false);
	_status->setText(tr("Editing '%1'. Rebuild updates the existing tree entry and keeps its presentation settings.").arg(session.name));
}

void Plot3DPanel::loadCsvFile()
{
	// Defaults to the bundled sample data the same way the main Open dialog defaults to sample-models
	// (MainWindow::on_actionOpen_triggered()) - a convenience for trying the feature, not a remembered
	// "last used" directory (Plot3D data is typically a one-off CSV from wherever the user's data lives).
	const QString sampleDir = PathUtils::getDataDirectory() + QStringLiteral("/sample-models/Plot3D");
	const QString path = QFileDialog::getOpenFileName(this, tr("Open 3D Plot Data"), sampleDir,
		tr("Delimited text (*.csv *.tsv *.txt);;All files (*.*)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(this, tr("Open 3D Plot Data"), tr("The selected file could not be opened."));
		return;
	}
	_source->setPlainText(QString::fromUtf8(file.readAll()));
	if (path.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive))
		_delimiter->setCurrentIndex(2);
	refreshPreview();
}

void Plot3DPanel::pasteData()
{
	_source->setPlainText(QApplication::clipboard()->text());
	refreshPreview();
}

void Plot3DPanel::refreshPreview()
{
	const QStringList previousHeaders = _table.headers;
	_preview->clear();
	_preview->setRowCount(0);
	_preview->setColumnCount(0);
	_table = Plot3DCsvTable();
	_buildButton->setEnabled(false);
	if (_source->toPlainText().trimmed().isEmpty())
	{
		_status->setText(tr("Open or paste tabular data to preview it."));
		refreshColumnCombos();
		return;
	}

	Plot3DCsvOptions options;
	options.delimiter = _delimiter->currentData().toString().front();
	options.firstRowIsHeader = _header->isChecked();
	Plot3DCsvTable table;
	QString error;
	if (!parsePlot3DCsv(_source->toPlainText(), options, table, &error))
	{
		_status->setText(error);
		_status->setStyleSheet(QStringLiteral("color: #d9534f;"));
		refreshColumnCombos();
		return;
	}

	const int shownRows = std::min<int>(static_cast<int>(table.rows.size()), 200);
	_preview->setColumnCount(table.columnCount());
	_preview->setHorizontalHeaderLabels(table.headers);
	_preview->setRowCount(shownRows);
	for (int row = 0; row < shownRows; ++row)
		for (int column = 0; column < table.columnCount(); ++column)
			_preview->setItem(row, column, new QTableWidgetItem(table.rows[static_cast<std::size_t>(row)][column]));
	_preview->resizeColumnsToContents();
	_status->setStyleSheet(QString());
	_status->setText(tr("%1 rows and %2 columns loaded%3.")
		.arg(table.rows.size()).arg(table.columnCount())
		.arg(table.rows.size() > static_cast<std::size_t>(shownRows) ? tr("; showing the first 200 rows") : QString()));

	const bool schemaChanged = previousHeaders != table.headers;
	_table = std::move(table);
	_buildButton->setEnabled(!_table.empty());
	refreshColumnCombos(schemaChanged);
	if (currentSource() == Plot3DSourceKind::CsvTimeSeries)
	{
		if (schemaChanged || _tsTime->count() <= 1)
			refreshTimeSeriesColumns();
		refreshGeneratedStatus();
	}
}

void Plot3DPanel::refreshColumnCombos(bool resetForNewSchema)
{
	auto headerIndex = [this](const QStringList& aliases) {
		for (int column = 0; column < _table.headers.size(); ++column)
			for (const QString& alias : aliases)
				if (_table.headers[column].compare(alias, Qt::CaseInsensitive) == 0)
					return column;
		return -1;
	};
	auto populate = [this, &headerIndex, resetForNewSchema](QComboBox* combo, bool withNone, int defaultColumn,
		const QStringList& aliases) {
		// Do not preserve the initial placeholder selection.  Before the first successful
		// parse, an optional combo contains only "(none)" (-1); preserving that value
		// after columns arrive leaves a required role such as Y unset even though the
		// control subsequently displays ordinary column choices.  Once a real column was
		// available, preserve the user's mapping across later refreshes as intended.
		const bool previouslyHadColumns = combo->findData(0) >= 0;
		int previousData = previouslyHadColumns && combo->currentData().isValid()
			? combo->currentData().toInt()
			: defaultColumn;
		if (resetForNewSchema)
		{
			const int namedColumn = headerIndex(aliases);
			if (namedColumn >= 0)
				previousData = namedColumn;
			else if (withNone && defaultColumn < 0)
				previousData = -1;
			else
				previousData = defaultColumn;
		}
		combo->blockSignals(true);
		combo->clear();
		if (withNone)
			combo->addItem(tr("(none)"), -1);
		for (int i = 0; i < _table.columnCount(); ++i)
			combo->addItem(_table.headers.value(i, tr("Column %1").arg(i + 1)), i);
		if (combo->count() > 0)
		{
			const int restoreIndex = combo->findData(previousData);
			int fallback = 0;
			if (!withNone)
				fallback = std::min(defaultColumn, combo->count() - 1);
			else if (defaultColumn >= 0)
				fallback = std::min(defaultColumn + 1, combo->count() - 1); // +1 for the leading "None" entry
			combo->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : fallback);
		}
		combo->blockSignals(false);
		};
	populate(_columnX, false, 0, { QStringLiteral("x"), QStringLiteral("i") });
	populate(_columnY, true, 1, { QStringLiteral("y"), QStringLiteral("j") }); // Bar/Histogram may select None for a one-dimensional plot; other builders reject it
	populate(_columnZ, false, 2, { QStringLiteral("z"), QStringLiteral("height"), QStringLiteral("k") });
	populate(_columnValue, true, -1, { QStringLiteral("value"), QStringLiteral("colour"), QStringLiteral("color"), QStringLiteral("occupancy") });
	populate(_columnU, false, 3, { QStringLiteral("u") });
	populate(_columnV, false, 4, { QStringLiteral("v") });
	populate(_columnW, false, 5, { QStringLiteral("w") });
	populate(_columnBase, true, -1, { QStringLiteral("base") });
	populate(_columnWidth, true, -1, { QStringLiteral("width") });
	populate(_columnDepth, true, -1, { QStringLiteral("depth") });
	populate(_columnError, true, -1, { QStringLiteral("error"), QStringLiteral("errorz"), QStringLiteral("uncertainty"), QStringLiteral("stddev") });
}

Plot3DColumnMapping Plot3DPanel::columnMapping() const
{
	Plot3DColumnMapping mapping;
	mapping.x = _columnX->currentData().toInt();
	mapping.y = _columnY->currentData().toInt();
	mapping.z = _columnZ->currentData().toInt();
	mapping.value = _columnValue->currentData().toInt(); // -1 == none; buildPlot3DDataset() then falls back to Z
	mapping.u = _columnU->currentData().toInt();
	mapping.v = _columnV->currentData().toInt();
	mapping.w = _columnW->currentData().toInt();
	mapping.base = _columnBase->currentData().toInt();
	mapping.width = _columnWidth->currentData().toInt();
	mapping.depth = _columnDepth->currentData().toInt();
	mapping.error = _columnError->currentData().toInt();
	return mapping;
}


Plot3DTimeSeriesColumns Plot3DPanel::timeSeriesColumns() const
{
	Plot3DTimeSeriesColumns columns;
	// An empty combo (no table loaded yet) has no data; that must read as "not chosen", not as column 0.
	auto column = [](const QComboBox* combo) { return combo->currentData().isValid() ? combo->currentData().toInt() : -1; };
	columns.time = column(_tsTime); columns.x = column(_tsX); columns.y = column(_tsY); columns.z = column(_tsZ);
	columns.u = column(_tsU); columns.v = column(_tsV); columns.w = column(_tsW);
	return columns;
}

void Plot3DPanel::refreshTimeSeriesColumns()
{
	// Offer the table's headers in every column combo and, for a new table, pick the likely column by its name. The Z combo
	// also offers "(none)" for a planar field.
	const QStringList& headers = _table.headers;
	auto fill = [&headers](QComboBox* combo, const QStringList& names, bool optional) {
		const QSignalBlocker block(combo);
		const int previous = combo->currentData().isValid() ? combo->currentData().toInt() : -1;
		combo->clear();
		combo->addItem(optional ? QCoreApplication::translate("Plot3DPanel", "(none)") : QCoreApplication::translate("Plot3DPanel", "(choose a column)"), -1);
		int guess = -1;
		for (int c = 0; c < headers.size(); ++c)
		{
			combo->addItem(headers[c], c);
			if (guess < 0 && names.contains(headers[c].trimmed().toLower()))
				guess = c;
		}
		const int keep = previous >= 0 && previous < headers.size() ? previous : guess;
		combo->setCurrentIndex(keep >= 0 ? combo->findData(keep) : 0);
	};
	fill(_tsTime, { QStringLiteral("t"), QStringLiteral("time") }, false);
	fill(_tsX, { QStringLiteral("x"), QStringLiteral("px"), QStringLiteral("pos_x") }, false);
	fill(_tsY, { QStringLiteral("y"), QStringLiteral("py"), QStringLiteral("pos_y") }, false);
	fill(_tsZ, { QStringLiteral("z"), QStringLiteral("pz"), QStringLiteral("pos_z") }, true);
	fill(_tsU, { QStringLiteral("u"), QStringLiteral("ux"), QStringLiteral("vx"), QStringLiteral("vel_x") }, false);
	fill(_tsV, { QStringLiteral("v"), QStringLiteral("uy"), QStringLiteral("vy"), QStringLiteral("vel_y") }, false);
	fill(_tsW, { QStringLiteral("w"), QStringLiteral("uz"), QStringLiteral("vz"), QStringLiteral("vel_z") }, false);
}

void Plot3DPanel::loadTimeSeriesForEditing(const Plot3DSession& session)
{
	clearPreview();
	_editingMeshUuid = session.meshUuid;
	setWindowTitle(tr("Edit 3D Plot - %1").arg(session.name));
	_buildButton->setText(tr("Rebuild Plot"));
	_sourceMode->setCurrentIndex(_sourceMode->findData(8));
	_sourceMode->setEnabled(false);
	{
		const QSignalBlocker delimiterBlock(_delimiter), headerBlock(_header), sourceBlock(_source);
		_delimiter->setCurrentIndex(_delimiter->findData(QString(session.csvOptions.delimiter)));
		_header->setChecked(session.csvOptions.firstRowIsHeader);
		_source->setPlainText(session.csvSource);
	}
	updateSourceMode();
	refreshPreview(); // parses the table and fills the column combos (with their guesses)
	auto restore = [](QComboBox* combo, int column) {
		const QSignalBlocker block(combo);
		const int index = combo->findData(column);
		if (index >= 0)
			combo->setCurrentIndex(index);
	};
	restore(_tsTime, session.columnMapping.time); restore(_tsX, session.columnMapping.x); restore(_tsY, session.columnMapping.y);
	restore(_tsZ, session.columnMapping.z); restore(_tsU, session.columnMapping.u); restore(_tsV, session.columnMapping.v);
	restore(_tsW, session.columnMapping.w);
	{
		const QSignalBlocker seedsBlock(_tsSeeds), stepsBlock(_tsSteps);
		_tsSeeds->setValue(session.generated.ySamples);
		_tsSteps->setValue(session.generated.zSamples);
	}
	refreshGeneratedStatus();
	_status->setText(tr("Editing '%1'. Rebuild updates the existing tree entry and keeps its presentation settings.").arg(session.name));
}

void Plot3DPanel::updateScatterOptions()
{
	const bool scatter = _primitive && static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) == Plot3DPrimitive::Scatter;
	_stemEnabled->setEnabled(scatter);
	const bool line = _primitive && static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) == Plot3DPrimitive::Line;
	_stemBaseZ->setEnabled((scatter && _stemEnabled->isChecked()) || ((scatter || line) && _scatterFillEnabled->isChecked()));
	_errorBarsEnabled->setEnabled(scatter);
	_columnError->setEnabled(scatter && _errorBarsEnabled->isChecked());
	_scatterFillEnabled->setEnabled(scatter || line); // a filled line is a ribbon down to the base plane, like a filled scatter
}

void Plot3DPanel::updateContourOverlayRow()
{
	if (!_contourOverlayRow || !_sourceMode || !_primitive)
		return;
	// Offered whenever the plot about to be built is a Surface: parametric and implicit surfaces always are; a CSV or formula plot is
	// when its primitive is Surface (a Contour plot has its own contour controls).
	const Plot3DSourceKind kind = currentSource();
	const bool tableOrFormula = kind == Plot3DSourceKind::Csv || kind == Plot3DSourceKind::FormulaSurface;
	const bool surface = kind == Plot3DSourceKind::ParametricSurface || kind == Plot3DSourceKind::ImplicitSurface
		|| (tableOrFormula && static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) == Plot3DPrimitive::Surface);
	_contourOverlayRow->setVisible(surface && _editingMeshUuid.isNull());
}
