#include "Plot3DPanel.h"

#include "Plot3DData.h"
#include "Plot3DAxisController.h"
#include "Plot3DMeshBuilder.h"
#include "AnalysisColorRamp.h"
#include "Material.h"
#include "MeshVertex.h"
#include "ModelViewer.h"
#include "PathUtils.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SimulationGlyphs.h"
#include "ViewportWidget.h"

#include <QUuid>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

Plot3DPanel::Plot3DPanel(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Add 3D Plot"));
	resize(760, 560);

	auto* layout = new QVBoxLayout(this);
	auto* sourceButtons = new QHBoxLayout();
	auto* openButton = new QPushButton(tr("Open CSV..."), this);
	auto* pasteButton = new QPushButton(tr("Paste"), this);
	auto* parseButton = new QPushButton(tr("Refresh Preview"), this);
	sourceButtons->addWidget(openButton);
	sourceButtons->addWidget(pasteButton);
	sourceButtons->addStretch();
	sourceButtons->addWidget(parseButton);
	layout->addLayout(sourceButtons);

	auto* options = new QFormLayout();
	_delimiter = new QComboBox(this);
	_delimiter->addItem(tr("Comma"), QStringLiteral(","));
	_delimiter->addItem(tr("Semicolon"), QStringLiteral(";"));
	_delimiter->addItem(tr("Tab"), QStringLiteral("\t"));
	_header = new QCheckBox(tr("First row contains column names"), this);
	_header->setChecked(true);
	options->addRow(tr("Delimiter:"), _delimiter);
	options->addRow(QString(), _header);
	layout->addLayout(options);

	_source = new QPlainTextEdit(this);
	_source->setPlaceholderText(tr("Paste comma-separated X, Y, Z data here, or open a CSV file."));
	_source->setMinimumHeight(120);
	layout->addWidget(_source);

	_preview = new QTableWidget(this);
	_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
	_preview->setAlternatingRowColors(true);
	layout->addWidget(_preview, 1);

	_status = new QLabel(tr("Open or paste tabular data to preview it."), this);
	_status->setWordWrap(true);
	layout->addWidget(_status);

	// Primitive + column mapping. Roles that apply only to one primitive remain in this static form so switching
	// primitive never destroys a mapping the user has already chosen.
	auto* mapping = new QFormLayout();
	_primitive = new QComboBox(this);
	_primitive->addItem(tr("Surface"), QVariant::fromValue(static_cast<int>(Plot3DPrimitive::Surface)));
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
	layout->addLayout(mapping);

	_buildButton = new QPushButton(tr("Build Plot"), this);
	_buildButton->setEnabled(false); // enabled once refreshPreview() has a non-empty table
	auto* buttonRow = new QHBoxLayout();
	buttonRow->addWidget(_buildButton);
	buttonRow->addStretch();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	buttonRow->addWidget(buttons);
	layout->addLayout(buttonRow);

	connect(openButton, &QPushButton::clicked, this, &Plot3DPanel::loadCsvFile);
	connect(pasteButton, &QPushButton::clicked, this, &Plot3DPanel::pasteData);
	connect(parseButton, &QPushButton::clicked, this, &Plot3DPanel::refreshPreview);
	connect(_delimiter, &QComboBox::currentIndexChanged, this, &Plot3DPanel::refreshPreview);
	connect(_header, &QCheckBox::toggled, this, &Plot3DPanel::refreshPreview);
	connect(_buildButton, &QPushButton::clicked, this, &Plot3DPanel::buildPlot);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	refreshColumnCombos(); // seeds the column combos with their placeholder/default state before any data is loaded
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

	_table = std::move(table);
	_buildButton->setEnabled(!_table.empty());
	refreshColumnCombos();
}

void Plot3DPanel::refreshColumnCombos()
{
	auto populate = [this](QComboBox* combo, bool withNone, int defaultColumn) {
		// Do not preserve the initial placeholder selection.  Before the first successful
		// parse, an optional combo contains only "(none)" (-1); preserving that value
		// after columns arrive leaves a required role such as Y unset even though the
		// control subsequently displays ordinary column choices.  Once a real column was
		// available, preserve the user's mapping across later refreshes as intended.
		const bool previouslyHadColumns = combo->findData(0) >= 0;
		const int previousData = previouslyHadColumns && combo->currentData().isValid()
			? combo->currentData().toInt()
			: defaultColumn;
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
	populate(_columnX, false, 0);
	populate(_columnY, true, 1); // Bar/Histogram may select None for a one-dimensional plot; other builders reject it
	populate(_columnZ, false, 2);
	populate(_columnValue, true, -1);
	populate(_columnU, false, 3);
	populate(_columnV, false, 4);
	populate(_columnW, false, 5);
	populate(_columnBase, true, -1);
	populate(_columnWidth, true, -1);
	populate(_columnDepth, true, -1);
}

void Plot3DPanel::buildPlot()
{
	if (_table.empty())
	{
		QMessageBox::warning(this, tr("Build Plot"), tr("Open or paste tabular data and click Refresh Preview first."));
		return;
	}
	if (!_modelViewer || !_modelViewer->getViewportWidget() || !_modelViewer->sceneGraph())
		return;

	const Plot3DPrimitive primitive = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
	if (primitive != Plot3DPrimitive::Surface && primitive != Plot3DPrimitive::Line
		&& primitive != Plot3DPrimitive::Scatter && primitive != Plot3DPrimitive::Bar
		&& primitive != Plot3DPrimitive::Quiver)
	{
		QMessageBox::information(this, tr("Build Plot"),
			tr("%1 is not implemented yet - Surface, Line, Scatter, Bar and Quiver are available.").arg(_primitive->currentText()));
		return;
	}

	Plot3DColumnMapping columnMapping;
	columnMapping.x = _columnX->currentData().toInt();
	columnMapping.y = _columnY->currentData().toInt();
	columnMapping.z = _columnZ->currentData().toInt();
	columnMapping.value = _columnValue->currentData().toInt(); // -1 == none; buildPlot3DDataset() then falls back to Z
	columnMapping.u = _columnU->currentData().toInt();
	columnMapping.v = _columnV->currentData().toInt();
	columnMapping.w = _columnW->currentData().toInt();
	columnMapping.base = _columnBase->currentData().toInt();
	columnMapping.width = _columnWidth->currentData().toInt();
	columnMapping.depth = _columnDepth->currentData().toInt();

	Plot3DDataset dataset;
	QString error;
	if (!buildPlot3DDataset(_table, primitive, columnMapping, dataset, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}

	const QString baseName = _modelViewer->getViewportWidget()->generateUniqueMeshName(tr("Plot3D %1").arg(plot3DPrimitiveName(primitive)));
	if (primitive == Plot3DPrimitive::Quiver)
	{
		buildQuiverPlot(dataset, baseName);
		return;
	}

	Plot3DMeshData meshData;
	bool built = false;
	switch (primitive)
	{
	case Plot3DPrimitive::Surface:
		built = buildPlot3DSurfaceMesh(std::get<Plot3DSurfaceData>(dataset.content), meshData, &error);
		break;
	case Plot3DPrimitive::Line:
		built = buildPlot3DLineMesh(std::get<Plot3DLineData>(dataset.content), meshData, &error);
		break;
	case Plot3DPrimitive::Scatter:
		built = buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset.content), meshData, &error);
		break;
	case Plot3DPrimitive::Bar:
		built = buildPlot3DBarMesh(std::get<Plot3DBarData>(dataset.content), meshData, &error);
		break;
	default:
		break; // unreachable - excluded by the primitive check above
	}
	if (!built)
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	viewport->makeCurrent();

	// Colour-by-value is applied afterwards via setAnalysisOverlayColors(), the same mechanism a simulation
	// result's field colouring uses (ModelViewer::presentSimulationResult()) - the mesh's own vertex colour stays
	// plain white so the overlay is the only thing tinting it.
	std::vector<Vertex> vertices(meshData.vertexCount());
	for (std::size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex v{};
		v.Color = glm::vec4(1.0f);
		v.Position = glm::vec3(meshData.positions[i * 3], meshData.positions[i * 3 + 1], meshData.positions[i * 3 + 2]);
		v.Normal = glm::vec3(meshData.normals[i * 3], meshData.normals[i * 3 + 1], meshData.normals[i * 3 + 2]);
		v.Tangent = glm::vec3(0.0f);
		v.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : v.TexCoords)
			uv = glm::vec2(0.0f);
		vertices[i] = v;
	}

	// Line/Scatter draw as native GL_LINE_STRIP/GL_POINTS (meshData.indices is empty for them - see
	// Plot3DMeshBuilder.h) - the same primitive-mode path glTF line/point-cloud import already uses, which is
	// rendered at a fixed PIXEL size regardless of camera zoom (SceneMesh::draw()), unlike real 3D geometry.
	GLenum primitiveMode = GL_TRIANGLES;
	if (primitive == Plot3DPrimitive::Line)
		primitiveMode = GL_LINE_STRIP;
	else if (primitive == Plot3DPrimitive::Scatter)
		primitiveMode = GL_POINTS;
	// skipOptimization = true: setAnalysisOverlayColors() below is indexed by vertex, and the mesh optimiser would
	// reorder vertices (see SceneMesh::optimizeMesh()) - same reasoning presentSimulationResult() documents.
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, meshData.indices, {}, Material(), true, primitiveMode);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();

	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	const int position = parent->children.size();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, position);
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);

	// Colour by value, unless every sample's value is NaN (a plain uniform-Z surface with no separate colour data).
	std::vector<float> values(meshData.vertexCount());
	std::vector<bool> valid(meshData.vertexCount());
	float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
	bool anyValid = false;
	for (std::size_t i = 0; i < meshData.vertexCount(); ++i)
	{
		const bool ok = std::isfinite(meshData.values[i]);
		valid[i] = ok;
		values[i] = ok ? static_cast<float>(meshData.values[i]) : 0.0f;
		if (ok)
		{
			lo = std::min(lo, values[i]);
			hi = std::max(hi, values[i]);
			anyValid = true;
		}
	}
	if (anyValid)
	{
		if (hi <= lo)
			hi = lo + 1.0f; // a perfectly flat field still needs a non-degenerate range for the colour ramp
		// mapToRGBA() (not mapToNormalizedScalarRGBA()) is the one that produces FINAL, already-ramped colours -
		// setAnalysisOverlayBanding(0, ...) below is main_scene.frag's "bands < 2" continuous path, which just
		// displays v_analysisColor.rgb verbatim with no further ramp/HSV mapping applied. The other encoding
		// (raw normalized t in R, used with a real band count) is only for the per-pixel discrete-band path -
		// mixing the two, as an earlier version of this code did, renders as a bare red channel (t in R, nothing
		// in G/B), which reads as a black-to-red gradient instead of the intended blue-to-red ramp.
		const std::vector<float> encoded = AnalysisColorRamp::mapToRGBA(values, valid, lo, hi, AnalysisColormap::Sequential);
		mesh->setAnalysisOverlayColors(encoded);
		mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
	}

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();
	// A plot added to a document which already contains CAD geometry would
	// otherwise retain that document's prior camera framing.  In particular,
	// the negative half of a signed bar plot can sit outside the view and look
	// missing.  Match simulation-result insertion: fit the updated scene once
	// the new mesh participates in its bounds.
	viewport->fitAll();

	// Register the generated mesh with the document-owned session model.  That
	// keeps its axes and colour range adjustable from the persistent 3D Plot
	// tab after this import dialog is closed.
	double dataLo[3], dataHi[3];
	if (plot3DDataBounds(dataset, dataLo, dataHi))
	{
		Plot3DSession session;
		session.meshUuid = meshUuid;
		session.name = baseName;
		session.primitive = primitive;
		session.axes = dataset.axes;
		std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
		std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
		session.values = std::move(values);
		session.valid = std::move(valid);
		session.dataMinimumValue = anyValid ? lo : 0.0f;
		session.dataMaximumValue = anyValid ? hi : 1.0f;
		session.colourMinimum = session.dataMinimumValue;
		session.colourMaximum = session.dataMaximumValue;
		session.colormap = static_cast<int>(AnalysisColormap::Sequential);
		_modelViewer->addPlot3DSession(std::move(session));
	}

	_status->setStyleSheet(QString());
	if (primitive == Plot3DPrimitive::Bar)
		_status->setText(tr("Built '%1' (%2 bars).").arg(baseName).arg(std::get<Plot3DBarData>(dataset.content).bars.size()));
	else
		_status->setText(tr("Built '%1' (%2 points).").arg(baseName).arg(meshData.vertexCount()));
}

void Plot3DPanel::buildQuiverPlot(const Plot3DDataset& dataset, const QString& baseName)
{
	const Plot3DQuiverData& quiver = std::get<Plot3DQuiverData>(dataset.content);

	Plot3DMeshData siteMesh;
	QString error;
	if (!buildPlot3DQuiverSiteMesh(quiver, siteMesh, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	viewport->makeCurrent();

	// The arrow sites, drawn as a small GL_POINTS SceneMesh (same constant-screen-size reasoning as Scatter) -
	// GlyphSet's anchors below are mesh-vertex indices, so the arrows are anchored to and follow THIS mesh.
	std::vector<Vertex> vertices(siteMesh.vertexCount());
	for (std::size_t i = 0; i < vertices.size(); ++i)
	{
		Vertex v{};
		v.Color = glm::vec4(1.0f);
		v.Position = glm::vec3(siteMesh.positions[i * 3], siteMesh.positions[i * 3 + 1], siteMesh.positions[i * 3 + 2]);
		v.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		v.Tangent = glm::vec3(0.0f);
		v.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : v.TexCoords)
			uv = glm::vec2(0.0f);
		vertices[i] = v;
	}
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, siteMesh.indices, {}, Material(), true, GL_POINTS);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();

	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	const int position = parent->children.size();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, position);
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);

	// Colour the site markers themselves by magnitude too, same mapToRGBA() path as the other primitives.
	std::vector<float> siteValues(siteMesh.vertexCount());
	std::vector<bool> siteValid(siteMesh.vertexCount());
	float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
	for (std::size_t i = 0; i < siteMesh.vertexCount(); ++i)
	{
		const bool ok = std::isfinite(siteMesh.values[i]);
		siteValid[i] = ok;
		siteValues[i] = ok ? static_cast<float>(siteMesh.values[i]) : 0.0f;
		if (ok) { lo = std::min(lo, siteValues[i]); hi = std::max(hi, siteValues[i]); }
	}
	if (hi <= lo)
		hi = lo + 1.0f;
	mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(siteValues, siteValid, lo, hi, AnalysisColormap::Sequential));
	mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));

	// GlyphSet: one arrow per site, anchored to the mesh vertex just built for it (all 3 anchor slots the same
	// index - "a node arrow repeats one vertex", GlyphSet's own convention). An earlier version used the CSV's own
	// vector magnitudes as the arrow length directly (matplotlib's own default quiver behaviour); the user found
	// this made the cone heads (SimulationGlyphController sizes them as a FRACTION of each arrow's own shaft
	// length, so a long shaft means a long, wide head too) dominate the plot, since a Plot3D CSV's raw vector units
	// have no reason to already be "reasonable arrow length" for this data's own grid spacing. Fixed the same way
	// buildGlyphSet() sizes simulation vector-field arrows: the LARGEST magnitude becomes a fixed fraction of the
	// data's own bounding-box diagonal, every other arrow scaled down from that by its magnitude ratio - so arrow
	// (and head) size is always proportionate to the plot regardless of the CSV's own vector units.
	double diagonalLo[3], diagonalHi[3];
	double diagonal = 1.0;
	if (plot3DDataBounds(dataset, diagonalLo, diagonalHi))
	{
		const double dx = diagonalHi[0] - diagonalLo[0], dy = diagonalHi[1] - diagonalLo[1], dz = diagonalHi[2] - diagonalLo[2];
		const double computed = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (computed > 1.0e-9)
			diagonal = computed;
	}
	const float maxArrowLength = static_cast<float>(diagonal * 0.06); // matches buildGlyphSet()'s own "5% of diagonal" order of magnitude

	GlyphSet glyphs;
	glyphs.anchors.reserve(quiver.arrows.size() * 3);
	glyphs.vectors.reserve(quiver.arrows.size() * 3);
	glyphs.values.reserve(quiver.arrows.size());
	glyphs.colors.reserve(quiver.arrows.size() * 3);
	float magnitudeLo = std::numeric_limits<float>::max(), magnitudeHi = std::numeric_limits<float>::lowest();
	std::vector<float> magnitudes(quiver.arrows.size());
	for (std::size_t i = 0; i < quiver.arrows.size(); ++i)
	{
		const Plot3DQuiver& arrow = quiver.arrows[i];
		const float mx = static_cast<float>(arrow.vector.x), my = static_cast<float>(arrow.vector.y), mz = static_cast<float>(arrow.vector.z);
		magnitudes[i] = std::sqrt(mx * mx + my * my + mz * mz);
		magnitudeLo = std::min(magnitudeLo, magnitudes[i]);
		magnitudeHi = std::max(magnitudeHi, magnitudes[i]);
	}
	if (magnitudeHi <= magnitudeLo)
		magnitudeHi = magnitudeLo + 1.0f;
	for (std::size_t i = 0; i < quiver.arrows.size(); ++i)
	{
		const Plot3DQuiver& arrow = quiver.arrows[i];
		glyphs.anchors.insert(glyphs.anchors.end(), { static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i) });
		const float rawLength = magnitudes[i];
		const float scale = rawLength > 1.0e-9f ? (maxArrowLength * (magnitudes[i] / magnitudeHi)) / rawLength : 0.0f;
		glyphs.vectors.insert(glyphs.vectors.end(), {
			static_cast<float>(arrow.vector.x) * scale, static_cast<float>(arrow.vector.y) * scale, static_cast<float>(arrow.vector.z) * scale });
		glyphs.values.push_back(magnitudes[i]);
		const QColor c = AnalysisColorRamp::colorForNormalized((magnitudes[i] - magnitudeLo) / (magnitudeHi - magnitudeLo), AnalysisColormap::Sequential);
		glyphs.colors.insert(glyphs.colors.end(), { static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()) });
	}
	glyphs.fieldMin = magnitudeLo;
	glyphs.fieldMax = magnitudeHi;
	glyphs.referenceLength = maxArrowLength;
	viewport->setSimulationGlyphs(meshUuid, std::move(glyphs));

	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();
	viewport->fitAll();

	double dataLo[3], dataHi[3];
	if (plot3DDataBounds(dataset, dataLo, dataHi))
	{
		Plot3DSession session;
		session.meshUuid = meshUuid;
		session.name = baseName;
		session.primitive = Plot3DPrimitive::Quiver;
		session.axes = dataset.axes;
		std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
		std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
		session.values = std::move(siteValues);
		session.valid = std::move(siteValid);
		session.dataMinimumValue = lo;
		session.dataMaximumValue = hi;
		session.colourMinimum = lo;
		session.colourMaximum = hi;
		session.colormap = static_cast<int>(AnalysisColormap::Sequential);
		_modelViewer->addPlot3DSession(std::move(session));
	}

	_status->setStyleSheet(QString());
	_status->setText(tr("Built '%1' (%2 arrows).").arg(baseName).arg(quiver.arrows.size()));
}
