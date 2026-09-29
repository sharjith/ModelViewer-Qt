#include "Plot3DPanel.h"

#include "Plot3DData.h"
#include "Plot3DAxisController.h"
#include "Plot3DMeshBuilder.h"
#include "Plot3DFormula.h"
#include "AnalysisColorRamp.h"
#include "Material.h"
#include "MeshVertex.h"
#include "ModelViewer.h"
#include "PathUtils.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "SimulationGlyphs.h"
#include "SimulationVolume.h"
#include "ViewportWidget.h"

#include <QUuid>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointF>
#include <QPushButton>
#include <QTableWidget>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace
{
bool showPlot3DPreview(ModelViewer* viewer, const Plot3DMeshData& data, GLenum primitiveMode,
	const std::array<Plot3DAxisConfig, 3>& axes, const double minimum[3], const double maximum[3], const QString& title)
{
	ViewportWidget* viewport = viewer ? viewer->getViewportWidget() : nullptr;
	if (!viewport || data.empty())
		return false;

	std::vector<Vertex> vertices(data.vertexCount());
	std::vector<float> values(data.vertexCount());
	std::vector<bool> valid(data.vertexCount(), true);
	float valueMinimum = std::numeric_limits<float>::max();
	float valueMaximum = std::numeric_limits<float>::lowest();
	for (std::size_t i = 0; i < data.vertexCount(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = glm::vec3(data.normals[i * 3], data.normals[i * 3 + 1], data.normals[i * 3 + 2]);
		vertex.Tangent = glm::vec3(0.0f);
		vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords)
			uv = glm::vec2(0.0f);
		const bool finite = std::isfinite(data.values[i]);
		valid[i] = finite;
		values[i] = finite ? static_cast<float>(data.values[i]) : 0.0f;
		if (finite)
		{
			valueMinimum = std::min(valueMinimum, values[i]);
			valueMaximum = std::max(valueMaximum, values[i]);
		}
	}
	if (valueMaximum <= valueMinimum)
		valueMaximum = valueMinimum + 1.0f;

	Plot3DAxisController controller;
	Plot3DAxisLayout layout;
	QString error;
	if (!controller.buildLayout(axes, minimum, maximum, layout, &error, title))
		return false;

	viewport->makeCurrent();
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), QStringLiteral("Plot3D Preview"), vertices, data.indices, {}, Material(), true, primitiveMode);
	viewport->addToDisplay(mesh);
	mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(values, valid, valueMinimum, valueMaximum, AnalysisColormap::Sequential));
	mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
	const QUuid meshUuid = mesh->uuid();
	viewport->doneCurrent();
	viewer->setPlot3DPreview({ meshUuid }, layout);
	return true;
}
}

Plot3DPanel::Plot3DPanel(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Add 3D Plot"));
	resize(760, 560);

	auto* layout = new QVBoxLayout(this);
	_sourceMode = new QComboBox(this);
	_sourceMode->addItem(tr("CSV file or pasted data"), 0);
	_sourceMode->addItem(tr("Formula surface"), 1);
	_sourceMode->addItem(tr("Parametric surface"), 2);
	_sourceMode->addItem(tr("Parametric curve"), 3);
	layout->addWidget(_sourceMode);
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
	_formulaPreset = new QComboBox(_formulaGroup);
	_formulaPresets = plot3DFormulaPresets();
	for (const Plot3DFormulaPreset& preset : _formulaPresets) _formulaPreset->addItem(preset.name);
	_parametricPreset = new QComboBox(_formulaGroup);
	_parametricPresets = plot3DParametricPresets();
	for (const Plot3DParametricPreset& preset : _parametricPresets) _parametricPreset->addItem(preset.name);
	_parametricCurvePreset = new QComboBox(_formulaGroup);
	_parametricCurvePresets = plot3DParametricCurvePresets();
	for (const Plot3DParametricCurvePreset& preset : _parametricCurvePresets) _parametricCurvePreset->addItem(preset.name);
	_formulaTitle = new QLineEdit(_formulaGroup);
	_formulaExpression = new QLineEdit(_formulaGroup);
	_parametricX = new QLineEdit(_formulaGroup); _parametricY = new QLineEdit(_formulaGroup); _parametricZ = new QLineEdit(_formulaGroup);
	_formulaXMinimum = new QDoubleSpinBox(_formulaGroup); _formulaXMaximum = new QDoubleSpinBox(_formulaGroup);
	_formulaYMinimum = new QDoubleSpinBox(_formulaGroup); _formulaYMaximum = new QDoubleSpinBox(_formulaGroup);
	_formulaXSamples = new QSpinBox(_formulaGroup); _formulaYSamples = new QSpinBox(_formulaGroup);
	for (QDoubleSpinBox* spin : { _formulaXMinimum, _formulaXMaximum, _formulaYMinimum, _formulaYMaximum }) { spin->setRange(-1.0e6, 1.0e6); spin->setDecimals(6); }
	for (QSpinBox* spin : { _formulaXSamples, _formulaYSamples }) spin->setRange(2, 8192);
	_formulaPresetLabel = new QLabel(tr("Preset:"), _formulaGroup);
	_parametricPresetLabel = new QLabel(tr("Parametric preset:"), _formulaGroup);
	_parametricCurvePresetLabel = new QLabel(tr("Curve preset:"), _formulaGroup);
	_formulaExpressionLabel = new QLabel(tr("z ="), _formulaGroup);
	_parametricXLabel = new QLabel(tr("x(u,v) ="), _formulaGroup);
	_parametricYLabel = new QLabel(tr("y(u,v) ="), _formulaGroup);
	_parametricZLabel = new QLabel(tr("z(u,v) ="), _formulaGroup);
	_formulaXRangeLabel = new QLabel(tr("X range / samples:"), _formulaGroup);
	_formulaYRangeLabel = new QLabel(tr("Y range / samples:"), _formulaGroup);
	_formulaParametersLabel = new QLabel(tr("Parameters:"), _formulaGroup);
	_formulaLayout->addRow(_formulaPresetLabel, _formulaPreset);
	_formulaLayout->addRow(_parametricPresetLabel, _parametricPreset);
	_formulaLayout->addRow(_parametricCurvePresetLabel, _parametricCurvePreset);
	_formulaLayout->addRow(tr("Title:"), _formulaTitle);
	_formulaLayout->addRow(_formulaExpressionLabel, _formulaExpression);
	_formulaLayout->addRow(_parametricXLabel, _parametricX);
	_formulaLayout->addRow(_parametricYLabel, _parametricY);
	_formulaLayout->addRow(_parametricZLabel, _parametricZ);
	auto* xRange = new QHBoxLayout(); xRange->addWidget(_formulaXMinimum); xRange->addWidget(_formulaXMaximum); xRange->addWidget(_formulaXSamples); _formulaLayout->addRow(_formulaXRangeLabel, xRange);
	auto* yRange = new QHBoxLayout(); yRange->addWidget(_formulaYMinimum); yRange->addWidget(_formulaYMaximum); yRange->addWidget(_formulaYSamples); _formulaLayout->addRow(_formulaYRangeLabel, yRange);
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
	_stemBaseZ = new QDoubleSpinBox(this);
	_stemBaseZ->setRange(-1.0e12, 1.0e12);
	_stemBaseZ->setDecimals(6);
	_stemBaseZ->setValue(0.0);
	scatterOptionsRow->addWidget(_stemEnabled);
	scatterOptionsRow->addWidget(_stemBaseZ);
	scatterOptionsRow->addStretch();
	mapping->addRow(tr("Scatter options:"), scatterOptionsRow);
	layout->addWidget(_mappingWidget);

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
	connect(parseButton, &QPushButton::clicked, this, [this] { const int sourceMode = _sourceMode->currentData().toInt(); if (sourceMode == 3) refreshParametricCurvePreview(); else if (sourceMode == 2) refreshParametricPreview(); else if (sourceMode == 1) refreshFormulaPreview(); else refreshPreview(); });
	connect(_delimiter, &QComboBox::currentIndexChanged, this, &Plot3DPanel::refreshPreview);
	connect(_header, &QCheckBox::toggled, this, &Plot3DPanel::refreshPreview);
	connect(_sourceMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::updateSourceMode);
	connect(_formulaPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::applyFormulaPreset);
	connect(_parametricPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::applyParametricPreset);
	connect(_parametricCurvePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::applyParametricCurvePreset);
	connect(_primitive, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DPanel::updateScatterOptions);
	connect(_stemEnabled, &QCheckBox::toggled, this, &Plot3DPanel::updateScatterOptions);
	connect(previewButton, &QPushButton::clicked, this, &Plot3DPanel::previewPlot);
	connect(clearPreviewButton, &QPushButton::clicked, this, &Plot3DPanel::clearPreview);
	connect(_buildButton, &QPushButton::clicked, this, &Plot3DPanel::buildPlot);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	refreshColumnCombos(); // seeds the column combos with their placeholder/default state before any data is loaded
	applyFormulaPreset();
	applyParametricPreset();
	applyParametricCurvePreset();
	updateSourceMode();
	updateScatterOptions();
}

Plot3DPanel::~Plot3DPanel()
{
	clearPreview();
}

void Plot3DPanel::clearPreview()
{
	if (_modelViewer)
		_modelViewer->clearPlot3DPreview();
}

void Plot3DPanel::previewPlot()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;

	const int sourceMode = _sourceMode->currentData().toInt();
	if (sourceMode == 3)
	{
		QHash<QString, double> parameters;
		for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it)
			parameters.insert(it.key(), it.value()->value());
		Plot3DLineData curve;
		Plot3DMeshData mesh;
		QString error;
		if (!buildPlot3DParametricCurve(_parametricX->text(), _parametricY->text(), _parametricZ->text(),
			_formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(), parameters, curve, &error)
			|| !buildPlot3DLineMesh(curve, mesh, &error))
		{
			QMessageBox::warning(this, tr("Preview Plot"), error);
			return;
		}
		double minimum[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
		double maximum[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
		for (const Plot3DSample& sample : curve.samples)
		{
			minimum[0] = std::min(minimum[0], sample.position.x); minimum[1] = std::min(minimum[1], sample.position.y); minimum[2] = std::min(minimum[2], sample.position.z);
			maximum[0] = std::max(maximum[0], sample.position.x); maximum[1] = std::max(maximum[1], sample.position.y); maximum[2] = std::max(maximum[2], sample.position.z);
		}
		const std::array<Plot3DAxisConfig, 3> axes = { Plot3DAxisConfig{ QStringLiteral("X") }, Plot3DAxisConfig{ QStringLiteral("Y") }, Plot3DAxisConfig{ QStringLiteral("Z") } };
		if (!showPlot3DPreview(_modelViewer, mesh, GL_LINE_STRIP, axes, minimum, maximum, _formulaTitle->text().trimmed()))
			QMessageBox::warning(this, tr("Preview Plot"), tr("The parametric-curve preview could not be created."));
		return;
	}
	if (sourceMode == 2)
	{
		QHash<QString, double> parameters;
		for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it)
			parameters.insert(it.key(), it.value()->value());
		Plot3DMeshData mesh;
		QString error;
		if (!buildPlot3DParametricSurface(_parametricX->text(), _parametricY->text(), _parametricZ->text(),
			_formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(),
			_formulaYMinimum->value(), _formulaYMaximum->value(), _formulaYSamples->value(), parameters, mesh, &error))
		{
			QMessageBox::warning(this, tr("Preview Plot"), error);
			return;
		}
		double minimum[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
		double maximum[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
		for (std::size_t i = 0; i < mesh.vertexCount(); ++i)
			for (int axis = 0; axis < 3; ++axis)
			{
				minimum[axis] = std::min(minimum[axis], static_cast<double>(mesh.positions[i * 3 + axis]));
				maximum[axis] = std::max(maximum[axis], static_cast<double>(mesh.positions[i * 3 + axis]));
			}
		const std::array<Plot3DAxisConfig, 3> axes = { Plot3DAxisConfig{ QStringLiteral("X") }, Plot3DAxisConfig{ QStringLiteral("Y") }, Plot3DAxisConfig{ QStringLiteral("Z") } };
		if (!showPlot3DPreview(_modelViewer, mesh, GL_TRIANGLES, axes, minimum, maximum, _formulaTitle->text().trimmed()))
			QMessageBox::warning(this, tr("Preview Plot"), tr("The parametric preview could not be created."));
		return;
	}

	if (sourceMode == 1)
		refreshFormulaPreview();
	else
		refreshPreview();
	if (_table.empty())
	{
		QMessageBox::warning(this, tr("Preview Plot"), tr("Open or paste tabular data and click Refresh Preview first."));
		return;
	}

	const Plot3DPrimitive primitive = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
	if (primitive == Plot3DPrimitive::Quiver || primitive == Plot3DPrimitive::Voxel)
	{
		QMessageBox::information(this, tr("Preview Plot"),
			tr("Interactive preview for %1 will be added with its renderer-specific preview path.").arg(plot3DPrimitiveName(primitive)));
		return;
	}
	Plot3DColumnMapping mapping;
	mapping.x = _columnX->currentData().toInt();
	mapping.y = _columnY->currentData().toInt();
	mapping.z = _columnZ->currentData().toInt();
	mapping.value = _columnValue->currentData().toInt();
	mapping.u = _columnU->currentData().toInt();
	mapping.v = _columnV->currentData().toInt();
	mapping.w = _columnW->currentData().toInt();
	mapping.base = _columnBase->currentData().toInt();
	mapping.width = _columnWidth->currentData().toInt();
	mapping.depth = _columnDepth->currentData().toInt();
	Plot3DDataset dataset;
	QString error;
	if (!buildPlot3DDataset(_table, primitive, mapping, dataset, &error))
	{
		QMessageBox::warning(this, tr("Preview Plot"), error);
		return;
	}

	Plot3DMeshData mesh;
	GLenum mode = GL_TRIANGLES;
	bool built = false;
	switch (primitive)
	{
	case Plot3DPrimitive::Surface:
		built = buildPlot3DSurfaceMesh(std::get<Plot3DSurfaceData>(dataset.content), mesh, &error);
		break;
	case Plot3DPrimitive::Contour:
		built = buildPlot3DContourMesh(std::get<Plot3DSurfaceData>(dataset.content), mesh, 10, &error);
		mode = GL_LINES;
		break;
	case Plot3DPrimitive::Line:
		built = buildPlot3DLineMesh(std::get<Plot3DLineData>(dataset.content), mesh, &error);
		mode = GL_LINE_STRIP;
		break;
	case Plot3DPrimitive::Scatter:
		if (_stemEnabled->isChecked())
		{
			built = buildPlot3DStemMesh(std::get<Plot3DScatterData>(dataset.content), _stemBaseZ->value(), mesh, &error);
			mode = GL_LINES;
		}
		else
		{
			built = buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset.content), mesh, &error);
			mode = GL_POINTS;
		}
		break;
	case Plot3DPrimitive::Bar:
		built = buildPlot3DBarMesh(std::get<Plot3DBarData>(dataset.content), mesh, &error);
		break;
	default:
		break;
	}
	if (!built)
	{
		QMessageBox::warning(this, tr("Preview Plot"), error);
		return;
	}

	double minimum[3], maximum[3];
	if (!plot3DDataBounds(dataset, minimum, maximum))
	{
		QMessageBox::warning(this, tr("Preview Plot"), tr("The plot has no valid preview bounds."));
		return;
	}
	if (primitive == Plot3DPrimitive::Scatter && _stemEnabled->isChecked())
	{
		minimum[2] = std::min(minimum[2], _stemBaseZ->value());
		maximum[2] = std::max(maximum[2], _stemBaseZ->value());
	}
	const QString title = sourceMode == 1 ? _formulaTitle->text().trimmed() : QString();
	if (!showPlot3DPreview(_modelViewer, mesh, mode, dataset.axes, minimum, maximum, title))
		QMessageBox::warning(this, tr("Preview Plot"), tr("The plot preview could not be created."));
}

void Plot3DPanel::updateScatterOptions()
{
	const bool scatter = _primitive && static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) == Plot3DPrimitive::Scatter;
	_stemEnabled->setEnabled(scatter);
	_stemBaseZ->setEnabled(scatter && _stemEnabled->isChecked());
}

void Plot3DPanel::updateSourceMode()
{
	const int sourceMode = _sourceMode->currentData().toInt();
	const bool generated = sourceMode != 0;
	const bool parametricSurface = sourceMode == 2;
	const bool parametricCurve = sourceMode == 3;
	const bool parametric = parametricSurface || parametricCurve;
	auto setVisible = [](QLabel* label, QWidget* field, bool visible)
	{
		label->setVisible(visible);
		field->setVisible(visible);
	};
	_tableSourceWidget->setVisible(!generated);
	_mappingWidget->setVisible(!generated);
	_formulaGroup->setVisible(generated);
	_formulaGroup->setTitle(parametricCurve ? tr("Parametric curve") : (parametricSurface ? tr("Parametric surface") : tr("Formula surface")));
	_delimiter->setEnabled(!generated);
	_header->setEnabled(!generated);
	_source->setEnabled(!generated);
	setVisible(_formulaPresetLabel, _formulaPreset, !parametric);
	setVisible(_parametricPresetLabel, _parametricPreset, parametricSurface);
	setVisible(_parametricCurvePresetLabel, _parametricCurvePreset, parametricCurve);
	setVisible(_formulaExpressionLabel, _formulaExpression, !parametric);
	setVisible(_parametricXLabel, _parametricX, parametric);
	setVisible(_parametricYLabel, _parametricY, parametric);
	setVisible(_parametricZLabel, _parametricZ, parametric);
	_formulaXRangeLabel->setText(parametricCurve ? tr("T range / samples:") : (parametricSurface ? tr("U range / samples:") : tr("X range / samples:")));
	_formulaYRangeLabel->setText(parametricSurface ? tr("V range / samples:") : tr("Y range / samples:"));
	_formulaLayout->setRowVisible(_formulaYRangeLabel, !parametricCurve);
	_formulaXSamples->setMaximum(parametricCurve ? 8192 : 512);
	_formulaYSamples->setMaximum(512);
	_parametricXLabel->setText(parametricCurve ? tr("x(t) =") : tr("x(u,v) ="));
	_parametricYLabel->setText(parametricCurve ? tr("y(t) =") : tr("y(u,v) ="));
	_parametricZLabel->setText(parametricCurve ? tr("z(t) =") : tr("z(u,v) ="));
	if (generated)
	{
		// Formula fields can also make contours. Parametric coordinates have a
		// single unambiguous output: a triangle surface or an ordered line.
		const Plot3DPrimitive expected = parametricCurve ? Plot3DPrimitive::Line : Plot3DPrimitive::Surface;
		if (parametric || (static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) != Plot3DPrimitive::Surface
			&& static_cast<Plot3DPrimitive>(_primitive->currentData().toInt()) != Plot3DPrimitive::Contour))
			_primitive->setCurrentIndex(_primitive->findData(static_cast<int>(expected)));
		// Formula and parametric presets have separate expression, title and
		// parameter sets. Reapply the selected preset when returning to its
		// source type so the visible controls never inherit the other mode.
		if (parametricSurface)
			applyParametricPreset();
		else if (parametricCurve)
			applyParametricCurvePreset();
		else
			applyFormulaPreset();
	}
	else
		refreshPreview();
}

void Plot3DPanel::applyFormulaPreset()
{
	const int index = _formulaPreset->currentIndex();
	if (index < 0 || index >= _formulaPresets.size()) return;
	const Plot3DFormulaPreset& preset = _formulaPresets[index];
	_formulaTitle->setText(preset.title);
	_formulaExpression->setText(preset.expression);
	_formulaXMinimum->setValue(preset.xMinimum); _formulaXMaximum->setValue(preset.xMaximum); _formulaXSamples->setValue(preset.xSamples);
	_formulaYMinimum->setValue(preset.yMinimum); _formulaYMaximum->setValue(preset.yMaximum); _formulaYSamples->setValue(preset.ySamples);
	while (QLayoutItem* item = _formulaParameters->takeAt(0)) { delete item->widget(); delete item; }
	_formulaParameterEditors.clear();
	for (const Plot3DFormulaParameter& parameter : preset.parameters)
	{
		auto* editor = new QDoubleSpinBox(_formulaGroup);
		editor->setRange(parameter.minimum, parameter.maximum); editor->setDecimals(6); editor->setValue(parameter.value);
		_formulaParameters->addRow(parameter.name + QStringLiteral(":"), editor);
		_formulaParameterEditors.insert(parameter.name.toLower(), editor);
	}
	if (_sourceMode->currentData().toInt() == 1) refreshFormulaPreview();
}

void Plot3DPanel::applyParametricPreset()
{
	const int index = _parametricPreset->currentIndex();
	if (index < 0 || index >= _parametricPresets.size()) return;
	const Plot3DParametricPreset& preset = _parametricPresets[index];
	_parametricX->setText(preset.xExpression); _parametricY->setText(preset.yExpression); _parametricZ->setText(preset.zExpression);
	_formulaTitle->setText(preset.title);
	_formulaXMinimum->setValue(preset.uMinimum); _formulaXMaximum->setValue(preset.uMaximum); _formulaXSamples->setValue(preset.uSamples);
	_formulaYMinimum->setValue(preset.vMinimum); _formulaYMaximum->setValue(preset.vMaximum); _formulaYSamples->setValue(preset.vSamples);
	while (QLayoutItem* item = _formulaParameters->takeAt(0)) { delete item->widget(); delete item; }
	_formulaParameterEditors.clear();
	for (const Plot3DFormulaParameter& parameter : preset.parameters) { auto* editor = new QDoubleSpinBox(_formulaGroup); editor->setRange(parameter.minimum, parameter.maximum); editor->setDecimals(6); editor->setValue(parameter.value); _formulaParameters->addRow(parameter.name + QStringLiteral(":"), editor); _formulaParameterEditors.insert(parameter.name.toLower(), editor); }
	if (_sourceMode->currentData().toInt() == 2) refreshParametricPreview();
}

void Plot3DPanel::applyParametricCurvePreset()
{
	const int index = _parametricCurvePreset->currentIndex();
	if (index < 0 || index >= _parametricCurvePresets.size()) return;
	const Plot3DParametricCurvePreset& preset = _parametricCurvePresets[index];
	_parametricX->setText(preset.xExpression); _parametricY->setText(preset.yExpression); _parametricZ->setText(preset.zExpression);
	_formulaTitle->setText(preset.title);
	_formulaXMinimum->setValue(preset.tMinimum); _formulaXMaximum->setValue(preset.tMaximum); _formulaXSamples->setValue(preset.samples);
	while (QLayoutItem* item = _formulaParameters->takeAt(0)) { delete item->widget(); delete item; }
	_formulaParameterEditors.clear();
	for (const Plot3DFormulaParameter& parameter : preset.parameters)
	{
		auto* editor = new QDoubleSpinBox(_formulaGroup);
		editor->setRange(parameter.minimum, parameter.maximum); editor->setDecimals(6); editor->setValue(parameter.value);
		_formulaParameters->addRow(parameter.name + QStringLiteral(":"), editor);
		_formulaParameterEditors.insert(parameter.name.toLower(), editor);
	}
	if (_sourceMode->currentData().toInt() == 3) refreshParametricCurvePreview();
}

void Plot3DPanel::refreshParametricPreview()
{
	QHash<QString, double> parameters; for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it) parameters.insert(it.key(), it.value()->value());
	Plot3DMeshData mesh; QString error;
	if (!buildPlot3DParametricSurface(_parametricX->text(), _parametricY->text(), _parametricZ->text(), _formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(), _formulaYMinimum->value(), _formulaYMaximum->value(), _formulaYSamples->value(), parameters, mesh, &error)) { _status->setText(error); _status->setStyleSheet(QStringLiteral("color: #d9534f;")); _buildButton->setEnabled(false); return; }
	_status->setStyleSheet(QString()); _status->setText(tr("Parametric surface evaluated on a %1 x %2 grid (%3 points).").arg(_formulaXSamples->value()).arg(_formulaYSamples->value()).arg(mesh.vertexCount())); _buildButton->setEnabled(true);
}

void Plot3DPanel::refreshParametricCurvePreview()
{
	QHash<QString, double> parameters;
	for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it)
		parameters.insert(it.key(), it.value()->value());
	Plot3DLineData curve;
	QString error;
	if (!buildPlot3DParametricCurve(_parametricX->text(), _parametricY->text(), _parametricZ->text(),
		_formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(), parameters, curve, &error))
	{
		_status->setText(error); _status->setStyleSheet(QStringLiteral("color: #d9534f;")); _buildButton->setEnabled(false); return;
	}
	_status->setStyleSheet(QString());
	_status->setText(tr("Parametric curve evaluated at %1 points.").arg(curve.samples.size()));
	_buildButton->setEnabled(true);
}

void Plot3DPanel::buildParametricPlot()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget() || !_modelViewer->sceneGraph())
		return;

	QHash<QString, double> parameters;
	for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it)
		parameters.insert(it.key(), it.value()->value());

	Plot3DMeshData data;
	QString error;
	if (!buildPlot3DParametricSurface(_parametricX->text(), _parametricY->text(), _parametricZ->text(),
		_formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(),
		_formulaYMinimum->value(), _formulaYMaximum->value(), _formulaYSamples->value(), parameters, data, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}
	clearPreview();

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	const QString title = _formulaTitle->text().trimmed();
	const QString baseName = viewport->generateUniqueMeshName(
		tr("Plot3D %1").arg(title.isEmpty() ? tr("Parametric") : title));
	std::vector<Vertex> vertices(data.vertexCount());
	std::vector<float> values(data.vertexCount());
	std::vector<bool> valid(data.vertexCount(), true);
	float valueMinimum = std::numeric_limits<float>::max();
	float valueMaximum = std::numeric_limits<float>::lowest();
	double dataMinimum[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double dataMaximum[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	for (std::size_t i = 0; i < data.vertexCount(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = glm::vec3(data.normals[i * 3], data.normals[i * 3 + 1], data.normals[i * 3 + 2]);
		vertex.Tangent = glm::vec3(0.0f);
		vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords)
			uv = glm::vec2(0.0f);
		values[i] = static_cast<float>(data.values[i]);
		valueMinimum = std::min(valueMinimum, values[i]);
		valueMaximum = std::max(valueMaximum, values[i]);
		for (int axis = 0; axis < 3; ++axis)
		{
			dataMinimum[axis] = std::min(dataMinimum[axis], static_cast<double>(data.positions[i * 3 + axis]));
			dataMaximum[axis] = std::max(dataMaximum[axis], static_cast<double>(data.positions[i * 3 + axis]));
		}
	}
	if (valueMaximum <= valueMinimum)
		valueMaximum = valueMinimum + 1.0f;

	viewport->makeCurrent();
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, data.indices, {}, Material(), true, GL_TRIANGLES);
	viewport->addToDisplay(mesh);
	mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(values, valid, valueMinimum, valueMaximum, AnalysisColormap::Sequential));
	mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
	const QUuid meshUuid = mesh->uuid();

	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, parent->children.size());
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);
	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();
	viewport->fitAll();

	Plot3DSession session;
	session.meshUuid = meshUuid;
	session.name = baseName;
	session.title = title;
	session.primitive = Plot3DPrimitive::Surface;
	session.axes = { Plot3DAxisConfig{ QStringLiteral("X") }, Plot3DAxisConfig{ QStringLiteral("Y") }, Plot3DAxisConfig{ QStringLiteral("Z") } };
	for (int axis = 0; axis < 3; ++axis)
	{
		session.dataMinimum[axis] = dataMinimum[axis];
		session.dataMaximum[axis] = dataMaximum[axis];
	}
	session.values = std::move(values);
	session.valid = std::move(valid);
	session.dataMinimumValue = valueMinimum;
	session.dataMaximumValue = valueMaximum;
	session.colourMinimum = valueMinimum;
	session.colourMaximum = valueMaximum;
	session.colormap = static_cast<int>(AnalysisColormap::Sequential);
	_modelViewer->addPlot3DSession(std::move(session));

	_status->setStyleSheet(QString());
	_status->setText(tr("Built '%1' (%2 vertices).").arg(baseName).arg(data.vertexCount()));
}

void Plot3DPanel::buildParametricCurvePlot()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget() || !_modelViewer->sceneGraph())
		return;

	QHash<QString, double> parameters;
	for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it)
		parameters.insert(it.key(), it.value()->value());
	Plot3DLineData curve;
	Plot3DMeshData data;
	QString error;
	if (!buildPlot3DParametricCurve(_parametricX->text(), _parametricY->text(), _parametricZ->text(),
		_formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(), parameters, curve, &error)
		|| !buildPlot3DLineMesh(curve, data, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}
	clearPreview();

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	const QString title = _formulaTitle->text().trimmed();
	const QString baseName = viewport->generateUniqueMeshName(
		tr("Plot3D %1").arg(title.isEmpty() ? tr("Parametric Curve") : title));
	std::vector<Vertex> vertices(data.vertexCount());
	std::vector<float> values(data.vertexCount());
	std::vector<bool> valid(data.vertexCount(), true);
	float valueMinimum = std::numeric_limits<float>::max();
	float valueMaximum = std::numeric_limits<float>::lowest();
	double dataMinimum[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max() };
	double dataMaximum[3] = { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest() };
	for (std::size_t i = 0; i < data.vertexCount(); ++i)
	{
		Vertex& vertex = vertices[i];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3(data.positions[i * 3], data.positions[i * 3 + 1], data.positions[i * 3 + 2]);
		vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		vertex.Tangent = glm::vec3(0.0f); vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords) uv = glm::vec2(0.0f);
		values[i] = static_cast<float>(data.values[i]);
		valueMinimum = std::min(valueMinimum, values[i]); valueMaximum = std::max(valueMaximum, values[i]);
		for (int axis = 0; axis < 3; ++axis)
		{
			dataMinimum[axis] = std::min(dataMinimum[axis], static_cast<double>(data.positions[i * 3 + axis]));
			dataMaximum[axis] = std::max(dataMaximum[axis], static_cast<double>(data.positions[i * 3 + axis]));
		}
	}
	if (valueMaximum <= valueMinimum) valueMaximum = valueMinimum + 1.0f;

	viewport->makeCurrent();
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, {}, {}, Material(), true, GL_LINE_STRIP);
	viewport->addToDisplay(mesh);
	mesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(values, valid, valueMinimum, valueMaximum, AnalysisColormap::Sequential));
	mesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
	const QUuid meshUuid = mesh->uuid();
	SceneNode* node = new SceneNode(); node->nodeUuid = QUuid::createUuid(); node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, parent->children.size());
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);
	viewport->doneCurrent();
	viewport->updateView();
	_modelViewer->updateDisplayList();
	viewport->fitAll();

	Plot3DSession session;
	session.meshUuid = meshUuid; session.name = baseName; session.title = title; session.primitive = Plot3DPrimitive::Line;
	session.axes = { Plot3DAxisConfig{ QStringLiteral("X") }, Plot3DAxisConfig{ QStringLiteral("Y") }, Plot3DAxisConfig{ QStringLiteral("Z") } };
	for (int axis = 0; axis < 3; ++axis) { session.dataMinimum[axis] = dataMinimum[axis]; session.dataMaximum[axis] = dataMaximum[axis]; }
	session.values = std::move(values); session.valid = std::move(valid);
	session.dataMinimumValue = valueMinimum; session.dataMaximumValue = valueMaximum;
	session.colourMinimum = valueMinimum; session.colourMaximum = valueMaximum;
	session.colormap = static_cast<int>(AnalysisColormap::Sequential);
	_modelViewer->addPlot3DSession(std::move(session));
	_status->setStyleSheet(QString());
	_status->setText(tr("Built '%1' (%2 points).").arg(baseName).arg(data.vertexCount()));
}
void Plot3DPanel::refreshFormulaPreview()
{
	_table = Plot3DCsvTable(); _preview->clear(); _preview->setRowCount(0); _preview->setColumnCount(0); _buildButton->setEnabled(false);
	QHash<QString, double> parameters;
	for (auto it = _formulaParameterEditors.cbegin(); it != _formulaParameterEditors.cend(); ++it) parameters.insert(it.key(), it.value()->value());
	Plot3DSurfaceData surface; QString error;
	if (!buildPlot3DFormulaSurface(_formulaExpression->text(), _formulaXMinimum->value(), _formulaXMaximum->value(), _formulaXSamples->value(),
		_formulaYMinimum->value(), _formulaYMaximum->value(), _formulaYSamples->value(), parameters, surface, &error))
	{
		_status->setText(error); _status->setStyleSheet(QStringLiteral("color: #d9534f;")); return;
	}
	_table.headers = { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("Value") };
	_table.rows.reserve(surface.samples.size());
	for (const Plot3DSample& sample : surface.samples)
		_table.rows.push_back({ QString::number(sample.position.x, 'g', 12), QString::number(sample.position.y, 'g', 12), QString::number(sample.position.z, 'g', 12), QString::number(sample.value, 'g', 12) });
	const int shownRows = std::min<int>(static_cast<int>(_table.rows.size()), 200);
	_preview->setColumnCount(_table.columnCount()); _preview->setHorizontalHeaderLabels(_table.headers); _preview->setRowCount(shownRows);
	for (int row = 0; row < shownRows; ++row) for (int col = 0; col < _table.columnCount(); ++col) _preview->setItem(row, col, new QTableWidgetItem(_table.rows[static_cast<size_t>(row)][col]));
	_preview->resizeColumnsToContents(); _status->setStyleSheet(QString());
	_status->setText(tr("Formula evaluated on a %1 x %2 grid (%3 points). ").arg(_formulaXSamples->value()).arg(_formulaYSamples->value()).arg(_table.rows.size()));
	_buildButton->setEnabled(true); refreshColumnCombos(true);
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
}

void Plot3DPanel::buildPlot()
{
	if (_sourceMode->currentData().toInt() == 3)
	{
		buildParametricCurvePlot();
		return;
	}
	if (_sourceMode->currentData().toInt() == 2)
	{
		buildParametricPlot();
		return;
	}
	if (_sourceMode->currentData().toInt() == 1)
	{
		refreshFormulaPreview();
		const Plot3DPrimitive formulaPrimitive = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
		if (formulaPrimitive != Plot3DPrimitive::Surface && formulaPrimitive != Plot3DPrimitive::Contour)
		{
			QMessageBox::warning(this, tr("Build Plot"), tr("Formula source currently supports Surface and Contour plots."));
			return;
		}
	}
	if (_table.empty())
	{
		QMessageBox::warning(this, tr("Build Plot"), tr("Open or paste tabular data and click Refresh Preview first."));
		return;
	}
	if (!_modelViewer || !_modelViewer->getViewportWidget() || !_modelViewer->sceneGraph())
		return;

	const Plot3DPrimitive primitive = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
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
	// Build commits the preview. Remove it before adding the normal scene mesh
	// so the viewport never briefly contains both copies of the same plot.
	clearPreview();

	const bool drawStems = primitive == Plot3DPrimitive::Scatter && _stemEnabled->isChecked();
	QString plotTypeName = drawStems ? tr("Stem") : plot3DPrimitiveName(primitive);
	if (_sourceMode->currentData().toInt() == 1)
	{
		const QString formulaTitle = _formulaTitle->text().trimmed();
		plotTypeName = formulaTitle.isEmpty() ? tr("Formula") : formulaTitle;
		if (primitive == Plot3DPrimitive::Contour)
			plotTypeName += tr(" Contour");
	}
	const QString baseName = _modelViewer->getViewportWidget()->generateUniqueMeshName(tr("Plot3D %1").arg(plotTypeName));
	if (primitive == Plot3DPrimitive::Quiver)
	{
		buildQuiverPlot(dataset, baseName);
		return;
	}
	if (primitive == Plot3DPrimitive::Voxel)
	{
		buildVoxelPlot(dataset, baseName);
		return;
	}

	Plot3DMeshData meshData;
	bool built = false;
	switch (primitive)
	{
	case Plot3DPrimitive::Surface:
		built = buildPlot3DSurfaceMesh(std::get<Plot3DSurfaceData>(dataset.content), meshData, &error);
		break;
	case Plot3DPrimitive::Contour:
		built = buildPlot3DContourMesh(std::get<Plot3DSurfaceData>(dataset.content), meshData, 10, &error);
		break;
	case Plot3DPrimitive::Line:
		built = buildPlot3DLineMesh(std::get<Plot3DLineData>(dataset.content), meshData, &error);
		break;
	case Plot3DPrimitive::Scatter:
		built = drawStems
			? buildPlot3DStemMesh(std::get<Plot3DScatterData>(dataset.content), _stemBaseZ->value(), meshData, &error)
			: buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset.content), meshData, &error);
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
	else if (primitive == Plot3DPrimitive::Contour)
		primitiveMode = GL_LINES;
	else if (primitive == Plot3DPrimitive::Scatter && !drawStems)
		primitiveMode = GL_POINTS;
	else if (drawStems)
		primitiveMode = GL_LINES;
	// skipOptimization = true: setAnalysisOverlayColors() below is indexed by vertex, and the mesh optimiser would
	// reorder vertices (see SceneMesh::optimizeMesh()) - same reasoning presentSimulationResult() documents.
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, meshData.indices, {}, Material(), true, primitiveMode);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();
	SceneMesh* markerMesh = nullptr;
	QUuid markerMeshUuid;
	std::vector<float> markerValues;
	std::vector<bool> markerValid;
	if (drawStems)
	{
		// GL_LINES cannot draw endpoint dots. Keep a companion native-points mesh in the same scene node so stems
		// retain their thin, zoom-invariant lines while their sample locations remain immediately readable.
		Plot3DMeshData markerData;
		if (!buildPlot3DScatterMesh(std::get<Plot3DScatterData>(dataset.content), markerData, &error))
		{
			viewport->doneCurrent();
			QMessageBox::warning(this, tr("Build Plot"), error);
			return;
		}
		std::vector<Vertex> markerVertices(markerData.vertexCount());
		markerValues.resize(markerData.vertexCount());
		markerValid.resize(markerData.vertexCount());
		for (std::size_t i = 0; i < markerData.vertexCount(); ++i)
		{
			Vertex& vertex = markerVertices[i];
			vertex.Color = glm::vec4(1.0f);
			vertex.Position = glm::vec3(markerData.positions[i * 3], markerData.positions[i * 3 + 1], markerData.positions[i * 3 + 2]);
			vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
			vertex.Tangent = glm::vec3(0.0f); vertex.Bitangent = glm::vec3(0.0f);
			for (glm::vec2& uv : vertex.TexCoords) uv = glm::vec2(0.0f);
			markerValid[i] = std::isfinite(markerData.values[i]);
			markerValues[i] = markerValid[i] ? static_cast<float>(markerData.values[i]) : 0.0f;
		}
		markerMesh = new SceneMesh(viewport->getShader(), baseName + tr(" Markers"), markerVertices, {}, {}, Material(), true, GL_POINTS);
		viewport->addToDisplay(markerMesh);
		markerMeshUuid = markerMesh->uuid();
	}

	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	const int position = parent->children.size();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, position);
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);
	if (markerMesh)
		_modelViewer->sceneGraph()->restoreMeshUuid(node, markerMeshUuid, 1);

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
		if (markerMesh)
		{
			markerMesh->setAnalysisOverlayColors(AnalysisColorRamp::mapToRGBA(markerValues, markerValid, lo, hi, AnalysisColormap::Sequential));
			markerMesh->setAnalysisOverlayBanding(0, static_cast<int>(AnalysisColormap::Sequential));
		}
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
		if (drawStems)
		{
			dataLo[2] = std::min(dataLo[2], _stemBaseZ->value());
			dataHi[2] = std::max(dataHi[2], _stemBaseZ->value());
		}
		Plot3DSession session;
		session.meshUuid = meshUuid;
		session.markerMeshUuid = markerMeshUuid;
		session.name = baseName;
		if (_sourceMode->currentData().toInt() == 1)
			session.title = _formulaTitle->text().trimmed();
		session.primitive = primitive;
		session.axes = dataset.axes;
		std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
		std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
		session.values = std::move(values);
		session.valid = std::move(valid);
		session.markerValues = std::move(markerValues);
		session.markerValid = std::move(markerValid);
		session.dataMinimumValue = anyValid ? lo : 0.0f;
		session.dataMaximumValue = anyValid ? hi : 1.0f;
		session.colourMinimum = session.dataMinimumValue;
		session.colourMaximum = session.dataMaximumValue;
		session.colormap = static_cast<int>(AnalysisColormap::Sequential);
		session.isStem = drawStems;
		if (primitive == Plot3DPrimitive::Contour)
			session.contourSource = std::get<Plot3DSurfaceData>(dataset.content);
		_modelViewer->addPlot3DSession(std::move(session));
	}

	_status->setStyleSheet(QString());
	if (primitive == Plot3DPrimitive::Bar)
		_status->setText(tr("Built '%1' (%2 bars).").arg(baseName).arg(std::get<Plot3DBarData>(dataset.content).bars.size()));
	else if (drawStems)
		_status->setText(tr("Built '%1' (%2 stems).").arg(baseName).arg(std::get<Plot3DScatterData>(dataset.content).samples.size()));
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

void Plot3DPanel::buildVoxelPlot(const Plot3DDataset& dataset, const QString& baseName)
{
	Plot3DVoxelGrid plotGrid;
	QString error;
	if (!buildPlot3DVoxelGrid(std::get<Plot3DVoxelData>(dataset.content), plotGrid, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}

	// SimulationVolumeController associates a grid with an ordinary SceneMesh so it naturally follows scene-tree
	// visibility, transforms, deletion and undo. Its normal rendering pass suppresses meshes with a registered
	// volume, therefore these eight bounds vertices are only the proxy's transform and fit-all extent, never visible
	// point markers.
	std::vector<Vertex> vertices(8);
	const float minimum[3] = { plotGrid.origin[0], plotGrid.origin[1], plotGrid.origin[2] };
	const float maximum[3] = { plotGrid.origin[0] + plotGrid.dimX, plotGrid.origin[1] + plotGrid.dimY, plotGrid.origin[2] + plotGrid.dimZ };
	for (int corner = 0; corner < 8; ++corner)
	{
		Vertex& vertex = vertices[static_cast<std::size_t>(corner)];
		vertex.Color = glm::vec4(1.0f);
		vertex.Position = glm::vec3((corner & 1) ? maximum[0] : minimum[0],
			(corner & 2) ? maximum[1] : minimum[1], (corner & 4) ? maximum[2] : minimum[2]);
		vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
		vertex.Tangent = glm::vec3(0.0f); vertex.Bitangent = glm::vec3(0.0f);
		for (glm::vec2& uv : vertex.TexCoords) uv = glm::vec2(0.0f);
	}

	ViewportWidget* viewport = _modelViewer->getViewportWidget();
	viewport->makeCurrent();
	SceneMesh* mesh = new SceneMesh(viewport->getShader(), baseName, vertices, {}, {}, Material(), true, GL_POINTS);
	viewport->addToDisplay(mesh);
	const QUuid meshUuid = mesh->uuid();
	SceneNode* node = new SceneNode();
	node->nodeUuid = QUuid::createUuid();
	node->name = baseName;
	SceneNode* parent = _modelViewer->sceneGraph()->root();
	_modelViewer->sceneGraph()->insertChildNode(parent, node, parent->children.size());
	_modelViewer->sceneGraph()->restoreMeshUuid(node, meshUuid, 0);

	VolumeGrid volume;
	volume.values = std::move(plotGrid.values);
	volume.dimX = plotGrid.dimX; volume.dimY = plotGrid.dimY; volume.dimZ = plotGrid.dimZ;
	for (int axis = 0; axis < 3; ++axis) { volume.origin[axis] = plotGrid.origin[axis]; volume.voxelSize[axis] = 1.0f; }
	volume.fieldMin = 0.0f; volume.fieldMax = 1.0f;
	volume.label = tr("Occupancy");
	// Zero is empty, while nonzero occupancy stays visible with an opacity that rises with the supplied value. A hard
	// 0.5 cutoff made a deliberately soft-edged input such as voxel_sphere.csv look much smaller than its own grid.
	const QVector<QPointF> opacity{ QPointF(0.0, 0.0), QPointF(0.149, 0.0), QPointF(0.15, 0.12), QPointF(0.5, 0.58), QPointF(1.0, 0.85) };
	viewport->setSimulationVolume(meshUuid, std::move(volume), static_cast<int>(AnalysisColormap::Sequential), opacity);
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
		session.primitive = Plot3DPrimitive::Voxel;
		session.axes = dataset.axes;
		std::copy(dataLo, dataLo + 3, session.dataMinimum.begin());
		std::copy(dataHi, dataHi + 3, session.dataMaximum.begin());
		session.dataMinimumValue = 0.0f; session.dataMaximumValue = 1.0f;
		session.colourMinimum = 0.0f; session.colourMaximum = 1.0f;
		session.colormap = static_cast<int>(AnalysisColormap::Sequential);
		_modelViewer->addPlot3DSession(std::move(session));
	}
	_status->setStyleSheet(QString());
	_status->setText(tr("Built '%1' (%2 supplied voxels, %3 x %4 x %5 grid).").arg(baseName)
		.arg(std::get<Plot3DVoxelData>(dataset.content).voxels.size()).arg(plotGrid.dimX).arg(plotGrid.dimY).arg(plotGrid.dimZ));
}
