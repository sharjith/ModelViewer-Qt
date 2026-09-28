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

	_addPlotButton = new QPushButton(tr("Add 3D Plot..."), this);
	_addPlotButton->setToolTip(tr("Import CSV or pasted tabular data and build a new 3D plot."));
	layout->addWidget(_addPlotButton);

	_plotSelector = new QComboBox(this);
	layout->addWidget(new QLabel(tr("Active plot:"), this));
	layout->addWidget(_plotSelector);
	_showPlotCheck = new QCheckBox(tr("Show plot"), this);
	layout->addWidget(_showPlotCheck);
	_showAxesCheck = new QCheckBox(tr("Show axes box"), this);
	_showAxesCheck->setToolTip(tr("Show or hide the active plot's axes without discarding its axis layout."));
	layout->addWidget(_showAxesCheck);

	auto* appearance = new QFormLayout();
	_colormap = new QComboBox(this);
	_colormap->addItem(tr("Sequential"), 0);
	_colormap->addItem(tr("Diverging"), 1);
	_bands = new QComboBox(this);
	_bands->addItem(tr("Smooth"), 0);
	for (int bands : { 4, 6, 8, 10, 12 }) _bands->addItem(tr("%1 bands").arg(bands), bands);
	_automaticRange = new QCheckBox(tr("Automatic colour range"), this);
	_rangeMinimum = new QDoubleSpinBox(this); _rangeMinimum->setRange(-1.0e12, 1.0e12); _rangeMinimum->setDecimals(6);
	_rangeMaximum = new QDoubleSpinBox(this); _rangeMaximum->setRange(-1.0e12, 1.0e12); _rangeMaximum->setDecimals(6);
	appearance->addRow(tr("Colour map:"), _colormap);
	appearance->addRow(tr("Colour bands:"), _bands);
	appearance->addRow(QString(), _automaticRange);
	appearance->addRow(tr("Minimum:"), _rangeMinimum);
	appearance->addRow(tr("Maximum:"), _rangeMaximum);
	layout->addLayout(appearance);

	// Axes are deliberately edited here, rather than in the transient import
	// dialog, because users commonly need to revisit labels/ranges after
	// comparing a generated plot with the rest of the document.
	auto* axesGroup = new QGroupBox(tr("Axes"), this);
	auto* axesLayout = new QVBoxLayout(axesGroup);
	for (int i = 0; i < 3; ++i)
	{
		auto* row = new QHBoxLayout();
		_axisLabels[i] = new QLineEdit(axesGroup);
		_axisScales[i] = new QComboBox(axesGroup);
		_axisScales[i]->addItem(tr("Linear"), static_cast<int>(Plot3DAxisScale::Linear));
		_axisScales[i]->addItem(tr("Log 10"), static_cast<int>(Plot3DAxisScale::Log10));
		_axisScales[i]->addItem(tr("SymLog"), static_cast<int>(Plot3DAxisScale::SymLog));
		_axisAutomatic[i] = new QCheckBox(tr("Auto"), axesGroup);
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
	connect(_plotSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { if (_viewer) _viewer->activatePlot3DSession(_plotSelector->currentData().toUuid()); });
	connect(_showPlotCheck, &QCheckBox::toggled, this, [this](bool visible) { if (_viewer) _viewer->setPlot3DSessionVisible(_plotSelector->currentData().toUuid(), visible); });
	connect(_showAxesCheck, &QCheckBox::toggled, this, [this](bool visible) { if (_viewer) _viewer->setPlot3DSessionAxesVisible(_plotSelector->currentData().toUuid(), visible); });
	connect(_colormap, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DControlsPanel::applyColourState);
	connect(_bands, qOverload<int>(&QComboBox::currentIndexChanged), this, &Plot3DControlsPanel::applyColourState);
	connect(_automaticRange, &QCheckBox::toggled, this, [this](bool) { applyColourState(); refreshState(); });
	connect(_rangeMinimum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_automaticRange->isChecked()) applyColourState(); });
	connect(_rangeMaximum, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!_automaticRange->isChecked()) applyColourState(); });
	setModelViewer(nullptr);
}

void Plot3DControlsPanel::retranslate()
{
	_addPlotButton->setText(tr("Add 3D Plot..."));
	_addPlotButton->setToolTip(tr("Import CSV or pasted tabular data and build a new 3D plot."));
	_showAxesCheck->setText(tr("Show axes box"));
	_showAxesCheck->setToolTip(tr("Show or hide the active plot's axes without discarding its axis layout."));
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
	const QSignalBlocker selectorBlock(_plotSelector), visibleBlock(_showPlotCheck), axesBlock(_showAxesCheck), mapBlock(_colormap), bandsBlock(_bands), autoBlock(_automaticRange), minBlock(_rangeMinimum), maxBlock(_rangeMaximum);
	std::array<QSignalBlocker, 18> axisBlockers{
		QSignalBlocker(_axisLabels[0]), QSignalBlocker(_axisScales[0]), QSignalBlocker(_axisAutomatic[0]), QSignalBlocker(_axisMinimum[0]), QSignalBlocker(_axisMaximum[0]), QSignalBlocker(_axisTicks[0]),
		QSignalBlocker(_axisLabels[1]), QSignalBlocker(_axisScales[1]), QSignalBlocker(_axisAutomatic[1]), QSignalBlocker(_axisMinimum[1]), QSignalBlocker(_axisMaximum[1]), QSignalBlocker(_axisTicks[1]),
		QSignalBlocker(_axisLabels[2]), QSignalBlocker(_axisScales[2]), QSignalBlocker(_axisAutomatic[2]), QSignalBlocker(_axisMinimum[2]), QSignalBlocker(_axisMaximum[2]), QSignalBlocker(_axisTicks[2]) };
	_addPlotButton->setEnabled(_viewer);
	_plotSelector->clear();
	for (const Plot3DSession& session : sessions) _plotSelector->addItem(session.name, session.meshUuid);
	const int activeIndex = _plotSelector->findData(active);
	_plotSelector->setCurrentIndex(activeIndex);
	const Plot3DSession* session = activeIndex >= 0 ? &sessions[activeIndex] : nullptr;
	const bool available = session != nullptr;
	// Keep the type explicit: MSVC cannot deduce a mixed derived-QWidget pointer
	// initializer list here under /permissive-.
	const std::array<QWidget*, 8> controls{ _plotSelector, _showPlotCheck, _showAxesCheck, _colormap,
		_bands, _automaticRange, _rangeMinimum, _rangeMaximum };
	for (QWidget* control : controls)
		control->setEnabled(available);
	if (!available)
	{
		for (int i = 0; i < 3; ++i)
		{
			const std::array<QWidget*, 6> axisControls{ _axisLabels[i], _axisScales[i], _axisAutomatic[i], _axisMinimum[i], _axisMaximum[i], _axisTicks[i] };
			for (QWidget* control : axisControls)
				control->setEnabled(false);
		}
		_axisStatus->setText(tr("No 3D plots are available in this document."));
		return;
	}
	_showPlotCheck->setChecked(session->visible); _showAxesCheck->setChecked(session->axesVisible);
	const bool supportsMeshColourControls = session->primitive != Plot3DPrimitive::Quiver;
	_colormap->setEnabled(supportsMeshColourControls); _bands->setEnabled(supportsMeshColourControls);
	_automaticRange->setEnabled(supportsMeshColourControls);
	_colormap->setCurrentIndex(_colormap->findData(session->colormap)); _bands->setCurrentIndex(_bands->findData(session->bands));
	_automaticRange->setChecked(session->colourMinimum == session->dataMinimumValue && session->colourMaximum == session->dataMaximumValue);
	_rangeMinimum->setValue(session->colourMinimum); _rangeMaximum->setValue(session->colourMaximum);
	_rangeMinimum->setEnabled(supportsMeshColourControls && !_automaticRange->isChecked()); _rangeMaximum->setEnabled(supportsMeshColourControls && !_automaticRange->isChecked());
	for (int i = 0; i < 3; ++i)
	{
		const Plot3DAxisConfig& axis = session->axes[i];
		_axisLabels[i]->setText(axis.label); _axisScales[i]->setCurrentIndex(_axisScales[i]->findData(static_cast<int>(axis.scale)));
		_axisAutomatic[i]->setChecked(axis.automaticRange); _axisMinimum[i]->setValue(axis.minimum); _axisMaximum[i]->setValue(axis.maximum); _axisTicks[i]->setValue(axis.targetTicks);
		_axisLabels[i]->setEnabled(true); _axisScales[i]->setEnabled(true); _axisAutomatic[i]->setEnabled(true); _axisTicks[i]->setEnabled(true);
		_axisMinimum[i]->setEnabled(!axis.automaticRange); _axisMaximum[i]->setEnabled(!axis.automaticRange);
	}
	_axisStatus->setText(tr("%1: %2").arg(plot3DPrimitiveName(session->primitive), session->visible ? tr("visible") : tr("hidden")));
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
