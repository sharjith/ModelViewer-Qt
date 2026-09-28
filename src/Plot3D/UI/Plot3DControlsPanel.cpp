#include "Plot3DControlsPanel.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

Plot3DControlsPanel::Plot3DControlsPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);

	_addPlotButton = new QPushButton(tr("Add 3D Plot..."), this);
	_addPlotButton->setToolTip(tr("Import CSV or pasted tabular data and build a new 3D plot."));
	layout->addWidget(_addPlotButton);

	_showAxesCheck = new QCheckBox(tr("Show axes box"), this);
	_showAxesCheck->setToolTip(tr("Show or hide the active plot's axes without discarding its axis layout."));
	layout->addWidget(_showAxesCheck);

	_axisStatus = new QLabel(this);
	_axisStatus->setWordWrap(true);
	layout->addWidget(_axisStatus);
	layout->addStretch(1);

	connect(_addPlotButton, &QPushButton::clicked, this, &Plot3DControlsPanel::addPlotRequested);
	connect(_showAxesCheck, &QCheckBox::toggled, this, [this](bool visible) {
		if (_viewer && _viewer->getViewportWidget())
			_viewer->getViewportWidget()->setPlot3DAxisVisible(visible);
	});
	setModelViewer(nullptr);
}

void Plot3DControlsPanel::retranslate()
{
	_addPlotButton->setText(tr("Add 3D Plot..."));
	_addPlotButton->setToolTip(tr("Import CSV or pasted tabular data and build a new 3D plot."));
	_showAxesCheck->setText(tr("Show axes box"));
	_showAxesCheck->setToolTip(tr("Show or hide the active plot's axes without discarding its axis layout."));
	refreshAxisState();
}

void Plot3DControlsPanel::setModelViewer(ModelViewer* viewer)
{
	disconnect(_axisStateConnection);
	_viewer = viewer;
	if (_viewer && _viewer->getViewportWidget())
	{
		_axisStateConnection = connect(_viewer->getViewportWidget(), &ViewportWidget::plot3DAxisStateChanged,
			this, [this](bool, bool) { refreshAxisState(); });
	}
	refreshAxisState();
}

void Plot3DControlsPanel::refreshAxisState()
{
	ViewportWidget* viewport = _viewer ? _viewer->getViewportWidget() : nullptr;
	const bool available = viewport && viewport->hasPlot3DAxisLayout();
	const QSignalBlocker blocker(_showAxesCheck);
	_addPlotButton->setEnabled(_viewer);
	_showAxesCheck->setEnabled(available);
	_showAxesCheck->setChecked(available && viewport->plot3DAxisVisible());
	_axisStatus->setText(available
		? tr("The axes use the current plot's data bounds and remain available after the import window is closed.")
		: tr("No 3D plot axes are available in this document."));
}
