#include "Plot3DControlsPanel.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

Plot3DControlsPanel::Plot3DControlsPanel(QWidget* parent)
	: QWidget(parent)
{
	// The document dock is intentionally resizable.  Keep this panel's wide
	// axis rows inside a scroll area so their layout never raises the dock's
	// minimum width or blocks the splitter from being dragged narrower.
	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	auto* content = new QWidget(scroll);
	content->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(6, 6, 6, 6);

	// No text is given to any widget below: applyTexts() assigns every user-visible string (see its declaration).
	_addPlotButton = new QPushButton(this);
	_addPlotButton->setIcon(QIcon(QStringLiteral(":/icons/res/plot3d.png")));
	layout->addWidget(_addPlotButton);
	_editPlotButton = new QPushButton(this);
	_editPlotButton->setIcon(QIcon(QStringLiteral(":/icons/res/plot3d.png")));
	layout->addWidget(_editPlotButton);

	_plotSelector = new QComboBox(this);
	_activePlotLabel = new QLabel(this);
	layout->addWidget(_activePlotLabel);
	layout->addWidget(_plotSelector);
	_showAxesCheck = new QCheckBox(this);
	layout->addWidget(_showAxesCheck);

	auto* appearance = new QFormLayout();
	_colormap = new QComboBox(this);
	_colormap->addItem(QString(), 0);
	_colormap->addItem(QString(), 1);
	_bands = new QComboBox(this);
	_bands->addItem(QString(), 0);
	for (int bands : { 4, 6, 8, 10, 12 }) _bands->addItem(QString(), bands);
	_automaticRange = new QCheckBox(this);
	_contourLevels = new QSpinBox(this); _contourLevels->setRange(1, 40);
	_contourLevelsLabel = new QLabel(this);
	_contourProjected = new QCheckBox(this);
	_contourOverlayLabel = new QLabel(this);
	_sectionProbe = new QCheckBox(this);
	_contourOverlay = new QComboBox(this);
	for (int mode : { 0, 1, 2 }) _contourOverlay->addItem(QString(), mode);
	_rangeMinimum = new QDoubleSpinBox(this); _rangeMinimum->setRange(-1.0e12, 1.0e12); _rangeMinimum->setDecimals(6);
	_rangeMaximum = new QDoubleSpinBox(this); _rangeMaximum->setRange(-1.0e12, 1.0e12); _rangeMaximum->setDecimals(6);
	_lineWidth = new QDoubleSpinBox(this); _lineWidth->setRange(0.5, 10.0); _lineWidth->setSingleStep(0.25); _lineWidth->setDecimals(2);
	_markerSize = new QDoubleSpinBox(this); _markerSize->setRange(1.0, 20.0); _markerSize->setSingleStep(0.5); _markerSize->setDecimals(1);
	_arrowScale = new QDoubleSpinBox(this); _arrowScale->setRange(0.25, 4.0); _arrowScale->setSingleStep(0.1); _arrowScale->setDecimals(2); _arrowScale->setSuffix(QStringLiteral("x"));
	_barWidthScale = new QDoubleSpinBox(this); _barWidthScale->setRange(0.1, 3.0); _barWidthScale->setSingleStep(0.05); _barWidthScale->setDecimals(2); _barWidthScale->setSuffix(QStringLiteral("x"));
	_barDepthScale = new QDoubleSpinBox(this); _barDepthScale->setRange(0.1, 3.0); _barDepthScale->setSingleStep(0.05); _barDepthScale->setDecimals(2); _barDepthScale->setSuffix(QStringLiteral("x"));
	_barWidthScaleLabel = new QLabel(this);
	_barDepthScaleLabel = new QLabel(this);
	_colormapLabel = new QLabel(this);
	_bandsLabel = new QLabel(this);
	_minimumLabel = new QLabel(this);
	_maximumLabel = new QLabel(this);
	_lineWidthLabel = new QLabel(this);
	_markerSizeLabel = new QLabel(this);
	_arrowScaleLabel = new QLabel(this);
	appearance->addRow(_colormapLabel, _colormap);
	appearance->addRow(_bandsLabel, _bands);
	appearance->addRow(QString(), _automaticRange);
	appearance->addRow(_contourOverlayLabel, _contourOverlay);
	appearance->addRow(QString(), _sectionProbe);
	appearance->addRow(_contourLevelsLabel, _contourLevels);
	appearance->addRow(QString(), _contourProjected);
	appearance->addRow(_minimumLabel, _rangeMinimum);
	appearance->addRow(_maximumLabel, _rangeMaximum);
	appearance->addRow(_lineWidthLabel, _lineWidth);
	appearance->addRow(_markerSizeLabel, _markerSize);
	appearance->addRow(_arrowScaleLabel, _arrowScale);
	appearance->addRow(_barWidthScaleLabel, _barWidthScale);
	appearance->addRow(_barDepthScaleLabel, _barDepthScale);
	layout->addLayout(appearance);

	// Axes are deliberately edited here, rather than in the transient import
	// dialog, because users commonly need to revisit labels/ranges after
	// comparing a generated plot with the rest of the document.
	_axesGroup = new QGroupBox(this);
	auto* axesGroup = _axesGroup;
	auto* axesLayout = new QVBoxLayout(axesGroup);
	auto* titleRow = new QHBoxLayout();
	_plotTitle = new QLineEdit(axesGroup);
	_titleLabel = new QLabel(axesGroup);
	titleRow->addWidget(_titleLabel);
	titleRow->addWidget(_plotTitle, 1);
	axesLayout->addLayout(titleRow);
	auto* referencePlaneRow = new QHBoxLayout();
	_planesLabel = new QLabel(axesGroup);
	referencePlaneRow->addWidget(_planesLabel);
	for (int i = 0; i < 3; ++i)
	{
		_referencePlanes[i] = new QCheckBox(axesGroup);
		referencePlaneRow->addWidget(_referencePlanes[i]);
	}
	referencePlaneRow->addStretch(1);
	_opacityLabel = new QLabel(axesGroup);
	referencePlaneRow->addWidget(_opacityLabel);
	_referencePlaneOpacity = new QSpinBox(axesGroup);
	_referencePlaneOpacity->setRange(0, 35);
	referencePlaneRow->addWidget(_referencePlaneOpacity);
	axesLayout->addLayout(referencePlaneRow);
	for (int i = 0; i < 3; ++i)
	{
		auto* row = new QHBoxLayout();
		_axisLabels[i] = new QLineEdit(axesGroup);
		_axisScales[i] = new QComboBox(axesGroup);
		_axisScales[i]->addItem(QString(), static_cast<int>(Plot3DAxisScale::Linear));
		_axisScales[i]->addItem(QString(), static_cast<int>(Plot3DAxisScale::Log10));
		_axisScales[i]->addItem(QString(), static_cast<int>(Plot3DAxisScale::SymLog));
		_axisAutomatic[i] = new QCheckBox(axesGroup);
		_axisMinimum[i] = new QDoubleSpinBox(axesGroup); _axisMinimum[i]->setRange(-1.0e12, 1.0e12); _axisMinimum[i]->setDecimals(6);
		_axisMaximum[i] = new QDoubleSpinBox(axesGroup); _axisMaximum[i]->setRange(-1.0e12, 1.0e12); _axisMaximum[i]->setDecimals(6);
		_axisTicks[i] = new QSpinBox(axesGroup); _axisTicks[i]->setRange(2, 20);
		row->addWidget(new QLabel(QString(QChar(u'X' + i)) + QStringLiteral(":"), axesGroup));
		row->addWidget(_axisLabels[i], 2); row->addWidget(_axisScales[i]); row->addWidget(_axisAutomatic[i]);
		row->addWidget(_axisMinimum[i]); row->addWidget(_axisMaximum[i]); row->addWidget(_axisTicks[i]);
		axesLayout->addLayout(row);
		connect(_axisLabels[i], &QLineEdit::editingFinished, this, &Plot3DControlsPanel::applyAxisState);
		connect(_axisScales[i], qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DControlsPanel::applyAxisState);
		connect(_axisAutomatic[i], &QCheckBox::toggled, this, [this](bool) { applyAxisState(); refreshState(); });
		connect(_axisMinimum[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, i](double) { if (!_axisAutomatic[i]->isChecked()) applyAxisState(); });
		connect(_axisMaximum[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, i](double) { if (!_axisAutomatic[i]->isChecked()) applyAxisState(); });
		connect(_axisTicks[i], qOverload<int>(&QSpinBox::valueChanged), this, &Plot3DControlsPanel::applyAxisState);
	}
	layout->addWidget(axesGroup);

	_axisStatus = new QLabel(this);
	_axisStatus->setWordWrap(true);
	layout->addWidget(_axisStatus);
	layout->addStretch(1);
	scroll->setWidget(content);
	outerLayout->addWidget(scroll);

	connect(_addPlotButton, &QPushButton::clicked, this, &Plot3DControlsPanel::addPlotRequested);
	connect(_editPlotButton, &QPushButton::clicked, this, [this] {
		if (_plotSelector->currentIndex() >= 0)
			emit editPlotRequested(_plotSelector->currentData().toUuid());
	});
	connect(_plotSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { if (_viewer) _viewer->activatePlot3DSession(_plotSelector->currentData().toUuid()); });
	connect(_showAxesCheck, &QCheckBox::toggled, this, [this](bool visible) { if (_viewer) _viewer->setPlot3DSessionAxesVisible(_plotSelector->currentData().toUuid(), visible); });
	connect(_colormap, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DControlsPanel::applyColourState);
	connect(_bands, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DControlsPanel::applyColourState);
	connect(_automaticRange, &QCheckBox::toggled, this, [this](bool automatic) {
		if (_viewer && _plotSelector->currentIndex() >= 0)
			_viewer->setPlot3DAutomaticColourRange(_plotSelector->currentData().toUuid(), automatic);
		applyColourState();
		refreshState();
	});
	connect(_contourLevels, qOverload<int>(&QSpinBox::valueChanged), this, [this](int levels) {
		if (!_viewer || _plotSelector->currentIndex() < 0)
			return;
		const QUuid uuid = _plotSelector->currentData().toUuid();
		_viewer->setPlot3DContourLevels(uuid, levels); // a Contour plot (ignored for any other)
		_viewer->setPlot3DContourOverlay(uuid, _contourOverlay->currentData().toInt(), levels); // a Surface's overlay (ignored for any other)
	});
	connect(_sectionProbe, &QCheckBox::toggled, this, [this](bool enabled) { if (_viewer && _plotSelector->currentIndex() >= 0) _viewer->setPlot3DSectionProbe(_plotSelector->currentData().toUuid(), enabled); });
	connect(_contourOverlay, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		if (!_viewer || _plotSelector->currentIndex() < 0)
			return;
		_viewer->setPlot3DContourOverlay(_plotSelector->currentData().toUuid(), _contourOverlay->currentData().toInt(), _contourLevels->value());
		refreshState();
	});
	connect(_contourProjected, &QCheckBox::toggled, this, [this](bool projected) { if (_viewer && _plotSelector->currentIndex() >= 0) _viewer->setPlot3DContourProjected(_plotSelector->currentData().toUuid(), projected); });
	connect(_rangeMinimum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_automaticRange->isChecked()) applyColourState(); });
	connect(_rangeMaximum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_automaticRange->isChecked()) applyColourState(); });
	connect(_lineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Plot3DControlsPanel::applyAppearanceState);
	connect(_markerSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Plot3DControlsPanel::applyAppearanceState);
	connect(_arrowScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Plot3DControlsPanel::applyAppearanceState);
	connect(_barWidthScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Plot3DControlsPanel::applyBarAppearanceState);
	connect(_barDepthScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &Plot3DControlsPanel::applyBarAppearanceState);
	for (QCheckBox* plane : _referencePlanes)
		connect(plane, &QCheckBox::toggled, this, &Plot3DControlsPanel::applyReferencePlaneState);
	connect(_referencePlaneOpacity, qOverload<int>(&QSpinBox::valueChanged), this, &Plot3DControlsPanel::applyReferencePlaneState);
	connect(_plotTitle, &QLineEdit::editingFinished, this, [this] {
		if (_viewer && _plotSelector->currentIndex() >= 0)
			_viewer->setPlot3DAxisTitle(_plotSelector->currentData().toUuid(), _plotTitle->text());
	});
	applyTexts();
	setModelViewer(nullptr);
}

void Plot3DControlsPanel::applyTexts()
{
	_addPlotButton->setText(tr("Add 3D Plot..."));
	_addPlotButton->setToolTip(tr("Import CSV or pasted tabular data and build a new 3D plot."));
	_editPlotButton->setText(tr("Edit Plot..."));
	_editPlotButton->setToolTip(tr("Reopen the active CSV plot's source data and column mapping."));
	_activePlotLabel->setText(tr("Active plot:"));
	_showAxesCheck->setText(tr("Show axes box"));
	_showAxesCheck->setToolTip(tr("Show or hide the active plot's axes without discarding its axis layout."));

	// Combo items: replace the text only. setItemText() neither changes the selection nor emits currentIndexChanged, so a
	// language change cannot be mistaken for the user picking a different colour map / band count / axis scale.
	_colormap->setItemText(0, tr("Sequential"));
	_colormap->setItemText(1, tr("Diverging"));
	for (int i = 0; i < _bands->count(); ++i)
		_bands->setItemText(i, i == 0 ? tr("Smooth") : tr("%1 bands").arg(_bands->itemData(i).toInt()));
	for (QComboBox* scale : _axisScales)
	{
		scale->setItemText(0, tr("Linear"));
		scale->setItemText(1, tr("Log 10"));
		scale->setItemText(2, tr("SymLog"));
	}

	_colormapLabel->setText(tr("Colour map:"));
	_bandsLabel->setText(tr("Colour bands:"));
	_automaticRange->setText(tr("Automatic colour range"));
	_contourOverlay->setItemText(0, tr("None"));
	_contourOverlay->setItemText(1, tr("On the surface"));
	_contourOverlay->setItemText(2, tr("On the base plane"));
	_contourOverlayLabel->setText(tr("Contour lines:"));
	_sectionProbe->setText(tr("Show section curves on hover"));
	_sectionProbe->setToolTip(tr("While the cursor is over this surface, draw the curves where the X, Y and Z planes\n"
	                             "through the hovered point cut it (red, green and blue like the axes), with the point's\n"
	                             "coordinates. The blue curve is the contour line through that point."));
	_contourLevelsLabel->setText(tr("Contour levels:"));
	_contourProjected->setText(tr("Project contours onto the base plane"));
	_minimumLabel->setText(tr("Minimum:"));
	_maximumLabel->setText(tr("Maximum:"));
	_lineWidthLabel->setText(tr("Line width:"));
	_markerSizeLabel->setText(tr("Marker size:"));
	_arrowScaleLabel->setText(tr("Arrow size:"));
	_lineWidth->setSuffix(tr(" px"));
	_markerSize->setSuffix(tr(" px"));
	_barWidthScaleLabel->setText(tr("Bar width:"));
	_barDepthScaleLabel->setText(tr("Bar depth:"));
	_barWidthScale->setToolTip(tr("Scale every bar's imported width while keeping its centre fixed."));
	_barDepthScale->setToolTip(tr("Scale every bar's imported depth while keeping its centre fixed."));

	_axesGroup->setTitle(tr("Axes"));
	_plotTitle->setPlaceholderText(tr("Plot title"));
	_titleLabel->setText(tr("Title:"));
	_planesLabel->setText(tr("Planes:"));
	_referencePlanes[0]->setText(tr("XY"));
	_referencePlanes[1]->setText(tr("XZ"));
	_referencePlanes[2]->setText(tr("YZ"));
	_opacityLabel->setText(tr("Opacity:"));
	_referencePlaneOpacity->setSuffix(tr(" %"));
	_referencePlaneOpacity->setToolTip(tr("Opacity of the selected reference planes."));
	for (QCheckBox* automatic : _axisAutomatic)
		automatic->setText(tr("Auto"));
}

void Plot3DControlsPanel::retranslate()
{
	applyTexts();
	refreshState();
}

void Plot3DControlsPanel::setModelViewer(ModelViewer* viewer)
{
	disconnect(_stateConnection);
	_viewer = viewer;
	if (_viewer && _viewer->getViewportWidget())
	{
		_stateConnection = connect(_viewer, &ModelViewer::plot3DSessionsChanged,
			this, [this](bool) { refreshState(); });
	}
	refreshState();
}

void Plot3DControlsPanel::refreshState()
{
	const QVector<Plot3DSession> sessions = _viewer ? _viewer->plot3DSessions() : QVector<Plot3DSession>();
	const QUuid active = _viewer ? _viewer->activePlot3DMeshUuid() : QUuid();
	const QSignalBlocker selectorBlock(_plotSelector), axesBlock(_showAxesCheck), titleBlock(_plotTitle), mapBlock(_colormap), bandsBlock(_bands), autoBlock(_automaticRange), contourBlock(_contourLevels), projectedBlock(_contourProjected), overlayBlock(_contourOverlay), probeBlock(_sectionProbe), minBlock(_rangeMinimum), maxBlock(_rangeMaximum), lineBlock(_lineWidth), markerBlock(_markerSize), arrowBlock(_arrowScale), barWidthBlock(_barWidthScale), barDepthBlock(_barDepthScale);
	const std::array<QSignalBlocker, 4> referencePlaneBlockers{ QSignalBlocker(_referencePlanes[0]),
		QSignalBlocker(_referencePlanes[1]), QSignalBlocker(_referencePlanes[2]), QSignalBlocker(_referencePlaneOpacity) };
	std::array<QSignalBlocker, 18> axisBlockers{
		QSignalBlocker(_axisLabels[0]), QSignalBlocker(_axisScales[0]), QSignalBlocker(_axisAutomatic[0]), QSignalBlocker(_axisMinimum[0]), QSignalBlocker(_axisMaximum[0]), QSignalBlocker(_axisTicks[0]),
		QSignalBlocker(_axisLabels[1]), QSignalBlocker(_axisScales[1]), QSignalBlocker(_axisAutomatic[1]), QSignalBlocker(_axisMinimum[1]), QSignalBlocker(_axisMaximum[1]), QSignalBlocker(_axisTicks[1]),
		QSignalBlocker(_axisLabels[2]), QSignalBlocker(_axisScales[2]), QSignalBlocker(_axisAutomatic[2]), QSignalBlocker(_axisMinimum[2]), QSignalBlocker(_axisMaximum[2]), QSignalBlocker(_axisTicks[2]) };
	_addPlotButton->setEnabled(_viewer);
	_editPlotButton->setEnabled(false);
	_plotSelector->clear();
	for (const Plot3DSession& session : sessions) _plotSelector->addItem(session.name, session.meshUuid);
	const int activeIndex = _plotSelector->findData(active);
	_plotSelector->setCurrentIndex(activeIndex);
	const Plot3DSession* session = activeIndex >= 0 ? &sessions[activeIndex] : nullptr;
	const bool available = session != nullptr;
	_editPlotButton->setEnabled(available && (session->editableCsv || session->generated.valid));
	// Keep the type explicit: MSVC cannot deduce a mixed derived-QWidget pointer
	// initializer list here under /permissive-.
	const std::array<QWidget*, 14> controls{ _plotSelector, _showAxesCheck, _plotTitle, _colormap,
		_bands, _automaticRange, _contourLevels, _rangeMinimum, _rangeMaximum, _lineWidth, _markerSize, _arrowScale,
		_barWidthScale, _barDepthScale };
	for (QWidget* control : controls)
		control->setEnabled(available);
	if (!available)
	{
		_contourLevelsLabel->setVisible(false);
		_contourLevels->setVisible(false);
		_contourProjected->setVisible(false);
		_contourOverlayLabel->setVisible(false);
		_contourOverlay->setVisible(false);
		_sectionProbe->setVisible(false);
		_barWidthScaleLabel->setVisible(false); _barWidthScale->setVisible(false);
		_barDepthScaleLabel->setVisible(false); _barDepthScale->setVisible(false);
		for (QCheckBox* plane : _referencePlanes) plane->setEnabled(false);
		_referencePlaneOpacity->setEnabled(false);
		for (int i = 0; i < 3; ++i)
		{
			const std::array<QWidget*, 6> axisControls{ _axisLabels[i], _axisScales[i], _axisAutomatic[i], _axisMinimum[i], _axisMaximum[i], _axisTicks[i] };
			for (QWidget* control : axisControls)
				control->setEnabled(false);
		}
		_axisStatus->setText(tr("No 3D plots are available in this document."));
		return;
	}
	_showAxesCheck->setChecked(session->axesVisible);
	for (int i = 0; i < 3; ++i)
	{
		_referencePlanes[i]->setChecked(session->referencePlanes[i]);
		_referencePlanes[i]->setEnabled(true);
	}
	_referencePlaneOpacity->setValue(qRound(session->referencePlaneOpacity * 100.0f));
	_referencePlaneOpacity->setEnabled(true);
	_plotTitle->setText(session->title);
	const bool isContour = session->primitive == Plot3DPrimitive::Contour;
	const bool isSurface = session->primitive == Plot3DPrimitive::Surface;
	const bool showLevels = isContour || (isSurface && session->contourOverlayMode != 0);
	_sectionProbe->setVisible(isSurface);
	_sectionProbe->setEnabled(isSurface);
	_sectionProbe->setChecked(session->sectionProbe);
	_contourOverlayLabel->setVisible(isSurface);
	_contourOverlay->setVisible(isSurface);
	_contourOverlay->setEnabled(isSurface);
	_contourOverlay->setCurrentIndex(_contourOverlay->findData(session->contourOverlayMode));
	_contourLevelsLabel->setVisible(showLevels);
	_contourLevels->setVisible(showLevels);
	_contourLevels->setEnabled(showLevels);
	_contourLevels->setValue(isContour ? session->contourLevels : session->contourOverlayLevels);
	_contourProjected->setVisible(isContour);
	_contourProjected->setEnabled(isContour);
	_contourProjected->setChecked(session->contourProjected);
	const bool supportsColourControls = true;
	const bool supportsColourRange = supportsColourControls && session->primitive != Plot3DPrimitive::Voxel;
	_colormap->setEnabled(supportsColourControls); _bands->setEnabled(supportsColourRange);
	_automaticRange->setEnabled(supportsColourRange);
	_colormap->setCurrentIndex(_colormap->findData(session->colormap)); _bands->setCurrentIndex(_bands->findData(session->bands));
	_automaticRange->setChecked(session->automaticColourRange);
	_rangeMinimum->setValue(session->colourMinimum); _rangeMaximum->setValue(session->colourMaximum);
	_rangeMinimum->setEnabled(supportsColourRange && !_automaticRange->isChecked()); _rangeMaximum->setEnabled(supportsColourRange && !_automaticRange->isChecked());
	_lineWidth->setValue(session->lineWidth);
	_markerSize->setValue(session->markerSize);
	_arrowScale->setValue(session->arrowScale);
	_barWidthScale->setValue(session->barWidthScale);
	_barDepthScale->setValue(session->barDepthScale);
	const bool linePlot = session->primitive == Plot3DPrimitive::Line || session->primitive == Plot3DPrimitive::Contour
		|| (session->primitive == Plot3DPrimitive::Surface && session->contourOverlayMode != 0)
		|| (session->primitive == Plot3DPrimitive::Scatter && !session->markerMeshUuid.isNull());
	_lineWidth->setEnabled(linePlot);
	_markerSize->setEnabled(session->primitive == Plot3DPrimitive::Scatter && !session->isFilledScatter);
	_arrowScale->setEnabled(session->primitive == Plot3DPrimitive::Quiver);
	const bool barPlot = session->primitive == Plot3DPrimitive::Bar;
	_barWidthScaleLabel->setVisible(barPlot); _barWidthScale->setVisible(barPlot); _barWidthScale->setEnabled(barPlot);
	_barDepthScaleLabel->setVisible(barPlot); _barDepthScale->setVisible(barPlot); _barDepthScale->setEnabled(barPlot);
	for (int i = 0; i < 3; ++i)
	{
		const Plot3DAxisConfig& axis = session->axes[i];
		_axisLabels[i]->setText(axis.label); _axisScales[i]->setCurrentIndex(_axisScales[i]->findData(static_cast<int>(axis.scale)));
		_axisAutomatic[i]->setChecked(axis.automaticRange); _axisMinimum[i]->setValue(axis.minimum); _axisMaximum[i]->setValue(axis.maximum); _axisTicks[i]->setValue(axis.targetTicks);
		_axisLabels[i]->setEnabled(true); _axisScales[i]->setEnabled(true); _axisAutomatic[i]->setEnabled(true); _axisTicks[i]->setEnabled(true);
		_axisMinimum[i]->setEnabled(!axis.automaticRange); _axisMaximum[i]->setEnabled(!axis.automaticRange);
	}
	_axisStatus->setText(tr("%1. Use the scene tree checkbox to show or hide this plot.")
		.arg(session->isFilledScatter ? tr("Filled Scatter") : (session->isStem ? tr("Stem") : plot3DPrimitiveName(session->primitive))));
}

void Plot3DControlsPanel::applyColourState()
{
	if (!_viewer || _plotSelector->currentIndex() < 0) return;
	const QVector<Plot3DSession> sessions = _viewer->plot3DSessions();
	const int index = _plotSelector->currentIndex(); if (index >= sessions.size()) return;
	const Plot3DSession& session = sessions[index];
	const float minimum = _automaticRange->isChecked() ? session.dataMinimumValue : static_cast<float>(_rangeMinimum->value());
	const float maximum = _automaticRange->isChecked() ? session.dataMaximumValue : static_cast<float>(_rangeMaximum->value());
	_viewer->applyPlot3DColourState(session.meshUuid, minimum, maximum, _colormap->currentData().toInt(), _bands->currentData().toInt());
}

void Plot3DControlsPanel::applyAppearanceState()
{
	if (!_viewer || _plotSelector->currentIndex() < 0)
		return;
	_viewer->applyPlot3DAppearance(_plotSelector->currentData().toUuid(), static_cast<float>(_lineWidth->value()),
		static_cast<float>(_markerSize->value()), static_cast<float>(_arrowScale->value()));
}

void Plot3DControlsPanel::applyBarAppearanceState()
{
	if (!_viewer || _plotSelector->currentIndex() < 0)
		return;
	_viewer->applyPlot3DBarAppearance(_plotSelector->currentData().toUuid(),
		static_cast<float>(_barWidthScale->value()), static_cast<float>(_barDepthScale->value()));
}

void Plot3DControlsPanel::applyReferencePlaneState()
{
	if (!_viewer || _plotSelector->currentIndex() < 0)
		return;
	const std::array<bool, 3> visible{ _referencePlanes[0]->isChecked(), _referencePlanes[1]->isChecked(),
		_referencePlanes[2]->isChecked() };
	_viewer->applyPlot3DReferencePlanes(_plotSelector->currentData().toUuid(), visible,
		static_cast<float>(_referencePlaneOpacity->value()) / 100.0f);
}

void Plot3DControlsPanel::applyAxisState()
{
	if (!_viewer || _plotSelector->currentIndex() < 0)
		return;
	const QVector<Plot3DSession> sessions = _viewer->plot3DSessions();
	const int index = _plotSelector->currentIndex();
	if (index >= sessions.size())
		return;
	std::array<Plot3DAxisConfig, 3> axes = sessions[index].axes;
	for (int i = 0; i < 3; ++i)
	{
		axes[i].label = _axisLabels[i]->text().trimmed();
		axes[i].scale = static_cast<Plot3DAxisScale>(_axisScales[i]->currentData().toInt());
		axes[i].automaticRange = _axisAutomatic[i]->isChecked();
		axes[i].minimum = _axisMinimum[i]->value(); axes[i].maximum = _axisMaximum[i]->value();
		axes[i].targetTicks = _axisTicks[i]->value();
	}
	_viewer->applyPlot3DAxisConfig(sessions[index].meshUuid, axes);
}
