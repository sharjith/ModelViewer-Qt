// Plot3DPanel - Preview, Build and Rebuild. Each is the same three steps: generate the plot data from the dialog's current
// definition (generateCurrent), then hand it to the assembly layer (plot3DShowPreview / plot3DCommit / plot3DRebuild). See
// Plot3DPanel.h for how the dialog is split.

#include "Plot3DPanel.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>

void Plot3DPanel::clearPreview()
{
	if (_modelViewer)
		_modelViewer->clearPlot3DPreview();
}

QString Plot3DPanel::previewTitle() const
{
	// A generated source's title heads its axes box; a table plot has none.
	return plot3DSourceIsGenerated(currentSource()) ? _formulaTitle->text().trimmed() : QString();
}

Plot3DCsvBinding Plot3DPanel::csvBinding() const
{
	Plot3DCsvBinding binding;
	binding.text = _source->toPlainText();
	binding.options.delimiter = _delimiter->currentData().toString().front();
	binding.options.firstRowIsHeader = _header->isChecked();
	if (currentSource() == Plot3DSourceKind::CsvTimeSeries)
	{
		const Plot3DTimeSeriesColumns columns = timeSeriesColumns();
		binding.mapping.time = columns.time; binding.mapping.x = columns.x; binding.mapping.y = columns.y; binding.mapping.z = columns.z;
		binding.mapping.u = columns.u; binding.mapping.v = columns.v; binding.mapping.w = columns.w;
	}
	else
		binding.mapping = columnMapping();
	return binding;
}

QString Plot3DPanel::suggestedPlotName(const Plot3DGenerated& generated, const Plot3DMeshOptions& options) const
{
	const QString title = _formulaTitle->text().trimmed();
	QString type;
	switch (currentSource())
	{
	case Plot3DSourceKind::Csv:
	{
		const bool scatter = generated.primitive == Plot3DPrimitive::Scatter;
		type = scatter && options.filled ? tr("Filled Scatter")
			: (scatter && options.stems ? tr("Stem") : (scatter && options.errorBars ? tr("Error Bars") : plot3DPrimitiveName(generated.primitive)));
		break;
	}
	case Plot3DSourceKind::FormulaSurface:
		type = title.isEmpty() ? tr("Formula") : title;
		if (generated.primitive == Plot3DPrimitive::Contour)
			type += tr(" Contour");
		break;
	case Plot3DSourceKind::ParametricSurface: type = title.isEmpty() ? tr("Parametric") : title; break;
	case Plot3DSourceKind::ImplicitSurface: type = title.isEmpty() ? tr("Implicit Surface") : title; break;
	case Plot3DSourceKind::FormulaVectorField: type = title.isEmpty() ? tr("Vector Field") : title; break;
	case Plot3DSourceKind::FormulaPathlines: type = title.isEmpty() ? tr("Pathlines") : title; break;
	case Plot3DSourceKind::CsvTimeSeries: type = tr("Pathlines"); break;
	case Plot3DSourceKind::ParametricCurve:
	case Plot3DSourceKind::FormulaStreamlines:
	default: type = title.isEmpty() ? tr("Parametric Curve") : title; break;
	}
	return _modelViewer->getViewportWidget()->generateUniqueMeshName(tr("Plot3D %1").arg(type));
}

void Plot3DPanel::previewPlot()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget())
		return;
	const Plot3DSourceKind kind = currentSource();
	if (kind == Plot3DSourceKind::Csv || kind == Plot3DSourceKind::CsvTimeSeries)
		refreshPreview(); // pick up text edited since the last refresh
	Plot3DGenerated generated;
	QString error;
	if (!generateCurrent(generated, &error))
	{
		QMessageBox::warning(this, tr("Preview Plot"), error);
		return;
	}
	if (!plot3DShowPreview(_modelViewer, generated, currentMeshOptions(), previewTitle(), &error))
		QMessageBox::warning(this, tr("Preview Plot"), error.isEmpty() ? tr("The plot preview could not be created.") : error);
}

void Plot3DPanel::buildPlot()
{
	if (!_modelViewer || !_modelViewer->getViewportWidget() || !_modelViewer->sceneGraph())
		return;
	if (!_editingMeshUuid.isNull())
	{
		rebuildCurrent();
		return;
	}
	const Plot3DSourceKind kind = currentSource();
	Plot3DGenerated generated;
	QString error;
	if (!generateCurrent(generated, &error))
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}
	// Build commits the preview. Remove it before adding the normal scene mesh so the viewport never briefly contains both copies.
	clearPreview();

	Plot3DCommitOptions options;
	options.mesh = currentMeshOptions();
	options.baseName = suggestedPlotName(generated, options.mesh);
	options.title = previewTitle();
	Plot3DCsvBinding binding;
	if (kind == Plot3DSourceKind::Csv || kind == Plot3DSourceKind::CsvTimeSeries)
	{
		binding = csvBinding(); // the table travels with the plot so Edit Plot can reopen it
		options.csv = &binding;
	}
	if (plot3DSourceIsGenerated(kind) || kind == Plot3DSourceKind::CsvTimeSeries)
		options.generated = currentGeneratedSpec(); // ... and a generated plot remembers its definition
	options.contourOverlayMode = _contourOverlayRow->isVisible() ? _contourOverlayMode->currentData().toInt() : 0;

	QString status;
	if (plot3DCommit(_modelViewer, generated, options, &status, &error).isNull())
	{
		QMessageBox::warning(this, tr("Build Plot"), error);
		return;
	}
	_status->setStyleSheet(QString());
	_status->setText(status);
}

void Plot3DPanel::rebuildCurrent()
{
	const Plot3DSourceKind kind = currentSource();
	Plot3DGenerated generated;
	QString error;
	if (!generateCurrent(generated, &error))
	{
		QMessageBox::warning(this, tr("Rebuild Plot"), error);
		return;
	}
	clearPreview();
	Plot3DCsvBinding binding;
	const Plot3DCsvBinding* csv = nullptr;
	if (kind == Plot3DSourceKind::Csv || kind == Plot3DSourceKind::CsvTimeSeries)
	{
		binding = csvBinding();
		csv = &binding;
	}
	if (!plot3DRebuild(_modelViewer, _editingMeshUuid, generated, csv, &error))
	{
		QMessageBox::warning(this, tr("Rebuild Plot"), error);
		return;
	}
	if (plot3DSourceIsGenerated(kind) || kind == Plot3DSourceKind::CsvTimeSeries)
		_modelViewer->setPlot3DGeneratedSpec(_editingMeshUuid, currentGeneratedSpec());
	_status->setStyleSheet(QString());
	_status->setText(tr("Rebuilt the existing plot."));
	_editingMeshUuid = QUuid();
	close();
}

void Plot3DPanel::loadGeneratedPlotForEditing(const Plot3DSession& session)
{
	const Plot3DGeneratedSpec& spec = session.generated;
	clearPreview();
	_editingMeshUuid = session.meshUuid;
	setWindowTitle(tr("Edit 3D Plot - %1").arg(session.name));
	_buildButton->setText(tr("Rebuild Plot"));
	// Choosing the source runs updateSourceMode(), which applies that source's preset; the saved definition is put back after it.
	_sourceMode->setCurrentIndex(_sourceMode->findData(spec.sourceMode));
	_sourceMode->setEnabled(false);
	_primitive->setCurrentIndex(_primitive->findData(static_cast<int>(session.primitive)));
	_primitive->setEnabled(false);
	_formulaPlotType->setEnabled(false); // a rebuild keeps the plot's type; changing it means building a new plot
	updateSourceMode();
	// Show the preset the plot was started from. Blocked: selecting it must not reapply that preset over the saved definition.
	if (QComboBox* preset = presetCombo(currentSource()))
	{
		if (spec.presetIndex >= 0 && spec.presetIndex < preset->count())
		{
			const QSignalBlocker presetBlock(preset);
			preset->setCurrentIndex(spec.presetIndex);
		}
		// Locked while editing: picking another preset would overwrite the definition being edited. Change the expressions, ranges
		// and parameters directly instead, or build a new plot from the other preset.
		preset->setEnabled(false);
	}
	applySpec(spec);
	refreshSourcePreview(); // validates the restored definition and enables Rebuild
	_status->setText(tr("Editing '%1'. Rebuild updates the existing tree entry and keeps its presentation settings.").arg(session.name));
}
