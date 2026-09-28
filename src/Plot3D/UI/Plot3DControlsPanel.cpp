#include "Plot3DControlsPanel.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <array>

Plot3DControlsPanel::Plot3DControlsPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
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

	_axisStatus = new QLabel(this);
	_axisStatus->setWordWrap(true);
	layout->addWidget(_axisStatus);
	layout->addStretch(1);

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
	if (!available) { _axisStatus->setText(tr("No 3D plots are available in this document.")); return; }
	_showPlotCheck->setChecked(session->visible); _showAxesCheck->setChecked(session->axesVisible);
	const bool supportsMeshColourControls = session->primitive != Plot3DPrimitive::Quiver;
	_colormap->setEnabled(supportsMeshColourControls); _bands->setEnabled(supportsMeshColourControls);
	_automaticRange->setEnabled(supportsMeshColourControls);
	_colormap->setCurrentIndex(_colormap->findData(session->colormap)); _bands->setCurrentIndex(_bands->findData(session->bands));
	_automaticRange->setChecked(session->colourMinimum == session->dataMinimumValue && session->colourMaximum == session->dataMaximumValue);
	_rangeMinimum->setValue(session->colourMinimum); _rangeMaximum->setValue(session->colourMaximum);
	_rangeMinimum->setEnabled(supportsMeshColourControls && !_automaticRange->isChecked()); _rangeMaximum->setEnabled(supportsMeshColourControls && !_automaticRange->isChecked());
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
