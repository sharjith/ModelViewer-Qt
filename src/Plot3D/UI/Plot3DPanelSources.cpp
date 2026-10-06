// Plot3DPanel - the data sources: which controls each one shows, its presets, the definition <-> widgets, and the status line that
// validates it. Everything a source MEANS (turning a definition into plot data) lives in Plot3DGenerate; this file only drives the
// widgets. See Plot3DPanel.h for how the dialog is split.

#include "Plot3DPanel.h"

#include "Plot3DGenerate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>

#include <algorithm>
#include <variant>

namespace
{
	// What the shared definition editor looks like for one generated source. Table-driven so a new source is one more row (and one
	// more case in generatePlot3D()), not another set of booleans threaded through the layout code.
	struct SourceUi
	{
		Plot3DSourceKind kind;
		const char* groupTitle;
		bool plotType;     // the Surface / Contour choice (formula surfaces)
		bool expression;   // the single "z = ..." / "f(x,y,z) = ..." field
		bool uvw;          // three expression fields (x/y/z of u,v or t; or the field's u, v, w)
		bool yRange;       // a second range row
		bool zRange;       // a third range row
		const char* xRangeLabel;
		const char* yRangeLabel;
		const char* zRangeLabel;
		int maxX, maxY, maxZ; // the sample spin boxes' limits
		const char* uvwLabels[3];
		Plot3DPrimitive primitive;
	};

	const SourceUi kSources[] = {
		{ Plot3DSourceKind::FormulaSurface, QT_TRANSLATE_NOOP("Plot3DPanel", "Formula surface"), true, true, false, true, false,
			QT_TRANSLATE_NOOP("Plot3DPanel", "X range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			512, 512, 8192, { "", "", "" }, Plot3DPrimitive::Surface },
		{ Plot3DSourceKind::ParametricSurface, QT_TRANSLATE_NOOP("Plot3DPanel", "Parametric surface"), false, false, true, true, false,
			QT_TRANSLATE_NOOP("Plot3DPanel", "U range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "V range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			512, 512, 8192, { QT_TRANSLATE_NOOP("Plot3DPanel", "x(u,v) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "y(u,v) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "z(u,v) =") }, Plot3DPrimitive::Surface },
		{ Plot3DSourceKind::ParametricCurve, QT_TRANSLATE_NOOP("Plot3DPanel", "Parametric curve"), false, false, true, false, false,
			QT_TRANSLATE_NOOP("Plot3DPanel", "T range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			8192, 512, 8192, { QT_TRANSLATE_NOOP("Plot3DPanel", "x(t) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "y(t) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "z(t) =") }, Plot3DPrimitive::Line },
		{ Plot3DSourceKind::FormulaVectorField, QT_TRANSLATE_NOOP("Plot3DPanel", "Formula vector field"), false, false, true, true, false,
			QT_TRANSLATE_NOOP("Plot3DPanel", "X range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			128, 128, 8192, { QT_TRANSLATE_NOOP("Plot3DPanel", "u(x,y) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "v(x,y) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "w(x,y) =") }, Plot3DPrimitive::Quiver },
		{ Plot3DSourceKind::ImplicitSurface, QT_TRANSLATE_NOOP("Plot3DPanel", "Implicit surface"), false, true, false, true, true,
			QT_TRANSLATE_NOOP("Plot3DPanel", "X range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			64, 64, 64, { "", "", "" }, Plot3DPrimitive::Surface },
		{ Plot3DSourceKind::FormulaStreamlines, QT_TRANSLATE_NOOP("Plot3DPanel", "Formula streamlines"), false, false, true, true, false,
			QT_TRANSLATE_NOOP("Plot3DPanel", "X range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Z range / samples:"),
			128, 128, 8192, { QT_TRANSLATE_NOOP("Plot3DPanel", "u(x,y) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "v(x,y) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "w(x,y) =") }, Plot3DPrimitive::Line },
		{ Plot3DSourceKind::FormulaPathlines, QT_TRANSLATE_NOOP("Plot3DPanel", "Formula pathlines"), false, false, true, true, true,
			QT_TRANSLATE_NOOP("Plot3DPanel", "X range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "Y range / samples:"), QT_TRANSLATE_NOOP("Plot3DPanel", "T range / steps:"),
			128, 128, 2000, { QT_TRANSLATE_NOOP("Plot3DPanel", "u(x,y,z,t) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "v(x,y,z,t) ="), QT_TRANSLATE_NOOP("Plot3DPanel", "w(x,y,z,t) =") }, Plot3DPrimitive::Line },
	};

	const SourceUi* sourceUi(Plot3DSourceKind kind)
	{
		for (const SourceUi& ui : kSources)
			if (ui.kind == kind)
				return &ui;
		return nullptr;
	}

	QString translated(const char* text)
	{
		return text && *text ? QCoreApplication::translate("Plot3DPanel", text) : QString();
	}
}

Plot3DSourceKind Plot3DPanel::currentSource() const
{
	Plot3DSourceKind kind = Plot3DSourceKind::Csv;
	plot3DSourceFromInt(_sourceMode->currentData().toInt(), kind);
	return kind;
}

QComboBox* Plot3DPanel::presetCombo(Plot3DSourceKind kind) const
{
	switch (kind)
	{
	case Plot3DSourceKind::FormulaSurface: return _formulaPreset;
	case Plot3DSourceKind::ParametricSurface: return _parametricPreset;
	case Plot3DSourceKind::ParametricCurve: return _parametricCurvePreset;
	case Plot3DSourceKind::FormulaVectorField:
	case Plot3DSourceKind::FormulaStreamlines: return _formulaVectorPreset;
	case Plot3DSourceKind::ImplicitSurface: return _implicitPreset;
	case Plot3DSourceKind::FormulaPathlines: return _pathlinePreset;
	default: return nullptr;
	}
}

void Plot3DPanel::updateSourceMode()
{
	const Plot3DSourceKind kind = currentSource();
	const bool timeSeries = kind == Plot3DSourceKind::CsvTimeSeries; // a table source with its own column choices
	const bool generated = plot3DSourceIsGenerated(kind);
	_tableSourceWidget->setVisible(!generated);
	_mappingWidget->setVisible(!generated && !timeSeries);
	_timeSeriesWidget->setVisible(timeSeries);
	_formulaGroup->setVisible(generated);
	_delimiter->setEnabled(!generated);
	_header->setEnabled(!generated);
	_source->setEnabled(!generated);

	if (const SourceUi* ui = generated ? sourceUi(kind) : nullptr)
	{
		auto setVisible = [](QLabel* label, QWidget* field, bool visible) { label->setVisible(visible); field->setVisible(visible); };
		_formulaGroup->setTitle(translated(ui->groupTitle));
		// Each source shows only its own preset combo.
		const struct { QLabel* label; QComboBox* combo; } presets[] = {
			{ _formulaPresetLabel, _formulaPreset }, { _parametricPresetLabel, _parametricPreset }, { _parametricCurvePresetLabel, _parametricCurvePreset },
			{ _formulaVectorPresetLabel, _formulaVectorPreset }, { _implicitPresetLabel, _implicitPreset }, { _pathlinePresetLabel, _pathlinePreset } };
		for (const auto& preset : presets)
			setVisible(preset.label, preset.combo, preset.combo == presetCombo(kind));
		setVisible(_formulaPlotTypeLabel, _formulaPlotType, ui->plotType);
		setVisible(_formulaExpressionLabel, _formulaExpression, ui->expression);
		setVisible(_parametricXLabel, _parametricX, ui->uvw);
		setVisible(_parametricYLabel, _parametricY, ui->uvw);
		setVisible(_parametricZLabel, _parametricZ, ui->uvw);
		_formulaXRangeLabel->setText(translated(ui->xRangeLabel));
		_formulaYRangeLabel->setText(translated(ui->yRangeLabel));
		_formulaZRangeLabel->setText(translated(ui->zRangeLabel));
		_formulaLayout->setRowVisible(_formulaYRangeLabel, ui->yRange);
		_formulaLayout->setRowVisible(_formulaZRangeLabel, ui->zRange);
		_formulaXSamples->setMaximum(ui->maxX);
		_formulaYSamples->setMaximum(ui->maxY);
		_formulaZSamples->setMaximum(ui->maxZ);
		if (ui->uvw)
		{
			_parametricXLabel->setText(translated(ui->uvwLabels[0]));
			_parametricYLabel->setText(translated(ui->uvwLabels[1]));
			_parametricZLabel->setText(translated(ui->uvwLabels[2]));
		}
		// A formula surface keeps a Surface or Contour primitive; every other source has exactly one.
		const Plot3DPrimitive current = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
		if (!ui->plotType || (current != Plot3DPrimitive::Surface && current != Plot3DPrimitive::Contour))
			_primitive->setCurrentIndex(_primitive->findData(static_cast<int>(ui->primitive)));
		{
			// Keep the formula surface's own Plot type in step with the primitive (Surface unless it is a Contour).
			const QSignalBlocker plotTypeBlock(_formulaPlotType);
			_formulaPlotType->setCurrentIndex(_formulaPlotType->findData(_primitive->currentData().toInt() == static_cast<int>(Plot3DPrimitive::Contour)
				? static_cast<int>(Plot3DPrimitive::Contour) : static_cast<int>(Plot3DPrimitive::Surface)));
		}
		// Reapply the selected preset when returning to a source so the visible controls never inherit another source's definition.
		applyPreset();
	}
	else
		refreshPreview();
	updateContourOverlayRow();
}

void Plot3DPanel::setParameterEditors(const std::vector<std::pair<QString, double>>& parameters)
{
	while (QLayoutItem* item = _formulaParameters->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	_formulaParameterEditors.clear();
	for (const auto& parameter : parameters)
	{
		auto* editor = new QDoubleSpinBox(_formulaGroup);
		editor->setRange(-1.0e6, 1.0e6);
		editor->setDecimals(6);
		editor->setValue(parameter.second);
		_formulaParameters->addRow(parameter.first + QStringLiteral(":"), editor);
		_formulaParameterEditors.insert(parameter.first.toLower(), editor);
	}
}

void Plot3DPanel::applySpec(const Plot3DGeneratedSpec& spec)
{
	_parametricX->setText(spec.xExpression);
	_parametricY->setText(spec.yExpression);
	_parametricZ->setText(spec.zExpression);
	_formulaExpression->setText(spec.expression);
	_formulaTitle->setText(spec.title);
	_formulaXMinimum->setValue(spec.xMinimum); _formulaXMaximum->setValue(spec.xMaximum); _formulaXSamples->setValue(spec.xSamples);
	_formulaYMinimum->setValue(spec.yMinimum); _formulaYMaximum->setValue(spec.yMaximum); _formulaYSamples->setValue(spec.ySamples);
	_formulaZMinimum->setValue(spec.zMinimum); _formulaZMaximum->setValue(spec.zMaximum); _formulaZSamples->setValue(spec.zSamples);
	setParameterEditors(spec.parameters);
}

void Plot3DPanel::applyPreset()
{
	const Plot3DSourceKind kind = currentSource();
	const QComboBox* combo = presetCombo(kind);
	if (!combo)
		return;
	const QVector<Plot3DPresetEntry> entries = _presetEntries.value(combo);
	const int index = combo->currentIndex();
	if (index < 0 || index >= entries.size())
		return;
	Plot3DGeneratedSpec spec = entries[index].spec;
	spec.sourceMode = plot3DSourceInt(kind); // the vector field and the streamlines share their presets
	applySpec(spec);
	refreshSourcePreview();
}

Plot3DGeneratedSpec Plot3DPanel::currentGeneratedSpec() const
{
	Plot3DGeneratedSpec spec;
	const Plot3DSourceKind kind = currentSource();
	if (kind == Plot3DSourceKind::Csv)
		return spec; // a table plot has no definition to keep
	spec.valid = true;
	spec.sourceMode = plot3DSourceInt(kind);
	if (const QComboBox* preset = presetCombo(kind))
		spec.presetIndex = preset->currentIndex();
	spec.title = _formulaTitle->text().trimmed();
	spec.expression = _formulaExpression->text();
	spec.xExpression = _parametricX->text(); spec.yExpression = _parametricY->text(); spec.zExpression = _parametricZ->text();
	spec.xMinimum = _formulaXMinimum->value(); spec.xMaximum = _formulaXMaximum->value();
	spec.yMinimum = _formulaYMinimum->value(); spec.yMaximum = _formulaYMaximum->value();
	spec.zMinimum = _formulaZMinimum->value(); spec.zMaximum = _formulaZMaximum->value();
	spec.xSamples = _formulaXSamples->value(); spec.ySamples = _formulaYSamples->value(); spec.zSamples = _formulaZSamples->value();
	if (kind == Plot3DSourceKind::CsvTimeSeries) // a time series keeps its seed and step counts here (its table and columns live in the session's CSV fields)
	{
		spec.title.clear(); // the formula title field is hidden for it and holds the last preset's title
		spec.ySamples = _tsSeeds->value();
		spec.zSamples = _tsSteps->value();
	}
	// In the order the dialog lists them (the editor map is unordered).
	for (int row = 0; row < _formulaParameters->rowCount(); ++row)
	{
		const QLayoutItem* label = _formulaParameters->itemAt(row, QFormLayout::LabelRole);
		const QLayoutItem* field = _formulaParameters->itemAt(row, QFormLayout::FieldRole);
		const auto* name = label ? qobject_cast<QLabel*>(label->widget()) : nullptr;
		const auto* editor = field ? qobject_cast<QDoubleSpinBox*>(field->widget()) : nullptr;
		if (name && editor)
		{
			QString text = name->text();
			if (text.endsWith(QLatin1Char(':')))
				text.chop(1);
			spec.parameters.emplace_back(text, editor->value());
		}
	}
	return spec;
}

Plot3DPrimitive Plot3DPanel::formulaPrimitive() const
{
	return static_cast<Plot3DPrimitive>(_formulaPlotType->currentData().toInt());
}

Plot3DMeshOptions Plot3DPanel::currentMeshOptions() const
{
	Plot3DMeshOptions options;
	options.stems = _stemEnabled->isChecked();
	options.errorBars = _errorBarsEnabled->isChecked();
	// The fill / stem / error-bar controls belong to the table source; a generated curve keeps its plain line.
	const bool table = currentSource() == Plot3DSourceKind::Csv;
	options.filled = table && _scatterFillEnabled->isChecked();
	options.baseZ = _stemBaseZ->value();
	return options;
}

bool Plot3DPanel::generateCurrent(Plot3DGenerated& out, QString* error)
{
	out = Plot3DGenerated();
	const Plot3DSourceKind kind = currentSource();
	if (kind == Plot3DSourceKind::Csv)
	{
		if (_table.empty())
		{
			if (error)
				*error = tr("Open or paste tabular data and click Refresh Preview first.");
			return false;
		}
		const Plot3DPrimitive primitive = static_cast<Plot3DPrimitive>(_primitive->currentData().toInt());
		Plot3DDataset dataset;
		if (!buildPlot3DDataset(_table, primitive, columnMapping(), dataset, error))
			return false;
		out.primitive = primitive;
		out.hasDataset = true;
		out.dataset = std::move(dataset);
		return true;
	}
	if (kind == Plot3DSourceKind::CsvTimeSeries)
	{
		const Plot3DTimeSeriesColumns columns = timeSeriesColumns();
		return generatePlot3D(currentGeneratedSpec(), Plot3DPrimitive::Line, &_table, &columns, out, error);
	}
	return generatePlot3D(currentGeneratedSpec(), formulaPrimitive(), nullptr, nullptr, out, error);
}

void Plot3DPanel::refreshFormulaPreview()
{
	_table = Plot3DCsvTable(); _preview->clear(); _preview->setRowCount(0); _preview->setColumnCount(0); _buildButton->setEnabled(false);
	Plot3DGeneratedSpec spec = currentGeneratedSpec();
	spec.sourceMode = plot3DSourceInt(Plot3DSourceKind::FormulaSurface);
	Plot3DGenerated generated;
	QString error;
	if (!generatePlot3D(spec, Plot3DPrimitive::Surface, nullptr, nullptr, generated, &error))
	{
		_status->setText(error); _status->setStyleSheet(QStringLiteral("color: #d9534f;")); return;
	}
	const Plot3DSurfaceData& surface = std::get<Plot3DSurfaceData>(generated.dataset.content);
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

void Plot3DPanel::refreshGeneratedStatus()
{
	const Plot3DSourceKind kind = currentSource();
	if (kind == Plot3DSourceKind::Csv)
		return;
	if (kind == Plot3DSourceKind::FormulaSurface)
	{
		refreshFormulaPreview();
		return;
	}
	if (kind == Plot3DSourceKind::CsvTimeSeries && _table.empty())
	{
		_buildButton->setEnabled(false); // refreshPreview() already said so
		return;
	}
	Plot3DGenerated generated;
	QString error;
	if (!generateCurrent(generated, &error))
	{
		_status->setText(error); _status->setStyleSheet(QStringLiteral("color: #d9534f;")); _buildButton->setEnabled(false);
		return;
	}
	QString text;
	switch (kind)
	{
	case Plot3DSourceKind::ParametricSurface:
		text = tr("Parametric surface evaluated on a %1 x %2 grid (%3 points).").arg(_formulaXSamples->value()).arg(_formulaYSamples->value()).arg(generated.mesh.vertexCount());
		break;
	case Plot3DSourceKind::ParametricCurve:
		text = tr("Parametric curve evaluated at %1 points.").arg(std::get<Plot3DLineData>(generated.dataset.content).samples.size());
		break;
	case Plot3DSourceKind::FormulaVectorField:
		text = tr("Formula vector field evaluated on a %1 x %2 grid (%3 arrows).").arg(_formulaXSamples->value()).arg(_formulaYSamples->value())
			.arg(std::get<Plot3DQuiverData>(generated.dataset.content).arrows.size());
		break;
	case Plot3DSourceKind::ImplicitSurface:
		text = tr("Implicit surface evaluated on a %1 x %2 x %3 grid (%4 triangles).").arg(_formulaXSamples->value()).arg(_formulaYSamples->value())
			.arg(_formulaZSamples->value()).arg(generated.mesh.indices.size() / 3);
		break;
	case Plot3DSourceKind::FormulaStreamlines:
		text = tr("Formula streamlines will use %1 seeds.").arg(_formulaYSamples->value());
		break;
	case Plot3DSourceKind::FormulaPathlines:
		text = tr("Pathlines from %1 seeds over t = %2 to %3 (%4 segments), coloured by time.").arg(_formulaYSamples->value())
			.arg(_formulaZMinimum->value()).arg(_formulaZMaximum->value()).arg(generated.mesh.vertexCount() / 2);
		break;
	case Plot3DSourceKind::CsvTimeSeries:
		text = tr("%1 rows form a complete grid. Pathlines from %2 seeds (%3 segments), coloured by time.").arg(_table.rows.size()).arg(_tsSeeds->value())
			.arg(generated.mesh.vertexCount() / 2);
		break;
	default:
		break;
	}
	_status->setStyleSheet(QString());
	_status->setText(text);
	_buildButton->setEnabled(true);
}

void Plot3DPanel::refreshSourcePreview()
{
	const Plot3DSourceKind kind = currentSource();
	if (kind == Plot3DSourceKind::Csv || kind == Plot3DSourceKind::CsvTimeSeries)
		refreshPreview(); // re-parses the table (a time series then validates itself)
	else
		refreshGeneratedStatus();
}
