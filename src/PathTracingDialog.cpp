#include "PathTracingDialog.h"
#include "ui_PathTracingDialog.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"

#include <QTimer>
#include <QFileDialog>
#include <QImage>
#include <QCloseEvent>
#include <QSettings>

#include <algorithm>

PathTracingDialog::PathTracingDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, _progressTimer(new QTimer(this))
	, ui(std::make_unique<Ui::PathTracingDialog>())
{
	ui->setupUi(this);
	setModal(false); // watch the viewport update live while adjusting settings

	loadSettings(); // pulls last-used values from QSettings into the viewport, and restores window geometry - before the UI below reads them

	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (viewport)
	{
		// Initialize from whatever's currently stored on the viewport
		// (now freshly loaded from QSettings above, or whatever a previous
		// dialog instance in this same session left behind if that load was
		// a no-op) - ViewportWidget is the in-session source of truth this
		// dialog reads/writes through.
		ui->spinBoxMaxSamples->setValue(static_cast<int>(viewport->pathTracingMaxSamples()));
		ui->spinBoxMaxBounces->setValue(viewport->pathTracingMaxBounces());
		ui->checkBoxDenoiser->setChecked(viewport->pathTracingDenoiserEnabled());
		ui->doubleSpinBoxFireflyClamp->setValue(static_cast<double>(viewport->pathTracingFireflyClampThreshold()));
		ui->spinBoxMaxTransmissionBounces->setValue(viewport->pathTracingMaxTransmissionBounces());
		ui->spinBoxRussianRouletteDepth->setValue(viewport->pathTracingRussianRouletteStartDepth());
		ui->checkBoxEnvImportanceSampling->setChecked(viewport->pathTracingEnvImportanceSamplingEnabled());
	}

	connect(ui->spinBoxMaxSamples, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxSamplesChanged);
	connect(ui->spinBoxMaxBounces, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxBouncesChanged);
	connect(ui->checkBoxDenoiser, &QCheckBox::toggled, this, &PathTracingDialog::onDenoiserToggled);
	connect(ui->doubleSpinBoxFireflyClamp, &QDoubleSpinBox::valueChanged, this, &PathTracingDialog::onFireflyClampChanged);
	connect(ui->spinBoxMaxTransmissionBounces, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxTransmissionBouncesChanged);
	connect(ui->spinBoxRussianRouletteDepth, &QSpinBox::valueChanged, this, &PathTracingDialog::onRussianRouletteDepthChanged);
	connect(ui->checkBoxEnvImportanceSampling, &QCheckBox::toggled, this, &PathTracingDialog::onEnvImportanceSamplingToggled);
	connect(ui->pushButtonRender, &QPushButton::clicked, this, &PathTracingDialog::onRenderClicked);
	connect(ui->pushButtonStop, &QPushButton::clicked, this, &PathTracingDialog::onStopClicked);
	connect(ui->pushButtonExport, &QPushButton::clicked, this, &PathTracingDialog::onExportClicked);
	connect(ui->pushButtonRestoreDefaults, &QPushButton::clicked, this, &PathTracingDialog::onRestoreDefaultsClicked);

	_progressTimer->setInterval(200);
	connect(_progressTimer, &QTimer::timeout, this, &PathTracingDialog::onProgressTimer);
	_progressTimer->start();

	onProgressTimer(); // reflect whatever's already running (dialog reopened mid-render) immediately
}

PathTracingDialog::~PathTracingDialog() = default;

void PathTracingDialog::closeEvent(QCloseEvent* event)
{
	// Only stop an actively-RUNNING render on close - closing while already
	// converged/idle leaves the finished result on screen untouched (the
	// same thing closing any other floating tool window would do). Closing
	// mid-render, though, would otherwise leave it running in the
	// background with no visible control, and reopening the dialog would
	// show whatever stale progress the session still has (see
	// _stoppedByUser's doc comment) instead of a fresh Idle state - so that
	// case reuses the exact same stop path Stop itself uses.
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	uint32_t current = 0, target = 0;
	bool running = false;
	if (viewport)
		viewport->pathTracingProgress(current, target, running);

	if (running && !_stoppedByUser)
		onStopClicked();

	saveSettings();
	QDialog::closeEvent(event);
}

void PathTracingDialog::loadSettings()
{
	QSettings settings;

	const QByteArray geometry = settings.value("pathtracing/geometry", QByteArray()).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);

	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	// Only ever narrows an existing value from QSettings, or leaves the
	// viewport's current (default-constructed) value untouched if this key
	// was never saved before - first run behaves exactly as if this
	// function didn't exist.
	viewport->setPathTracingMaxSamples(settings.value("pathtracing/maxSamples", viewport->pathTracingMaxSamples()).toUInt());
	viewport->setPathTracingMaxBounces(settings.value("pathtracing/maxBounces", viewport->pathTracingMaxBounces()).toInt());
	viewport->setPathTracingDenoiserEnabled(settings.value("pathtracing/denoiserEnabled", viewport->pathTracingDenoiserEnabled()).toBool());
	viewport->setPathTracingFireflyClampThreshold(settings.value("pathtracing/fireflyClamp", viewport->pathTracingFireflyClampThreshold()).toFloat());
	viewport->setPathTracingMaxTransmissionBounces(settings.value("pathtracing/maxTransmissionBounces", viewport->pathTracingMaxTransmissionBounces()).toInt());
	viewport->setPathTracingRussianRouletteStartDepth(settings.value("pathtracing/russianRouletteDepth", viewport->pathTracingRussianRouletteStartDepth()).toInt());
	viewport->setPathTracingEnvImportanceSamplingEnabled(settings.value("pathtracing/envImportanceSampling", viewport->pathTracingEnvImportanceSamplingEnabled()).toBool());
}

void PathTracingDialog::saveSettings()
{
	QSettings settings;
	settings.setValue("pathtracing/geometry", saveGeometry());

	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	settings.setValue("pathtracing/maxSamples", viewport->pathTracingMaxSamples());
	settings.setValue("pathtracing/maxBounces", viewport->pathTracingMaxBounces());
	settings.setValue("pathtracing/denoiserEnabled", viewport->pathTracingDenoiserEnabled());
	settings.setValue("pathtracing/fireflyClamp", viewport->pathTracingFireflyClampThreshold());
	settings.setValue("pathtracing/maxTransmissionBounces", viewport->pathTracingMaxTransmissionBounces());
	settings.setValue("pathtracing/russianRouletteDepth", viewport->pathTracingRussianRouletteStartDepth());
	settings.setValue("pathtracing/envImportanceSampling", viewport->pathTracingEnvImportanceSamplingEnabled());
}

void PathTracingDialog::onMaxSamplesChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingMaxSamples(static_cast<uint32_t>(value));
	ui->progressBarSamples->setMaximum(value);
}

void PathTracingDialog::onMaxBouncesChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingMaxBounces(value);
}

void PathTracingDialog::onDenoiserToggled(bool checked)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingDenoiserEnabled(checked);
}

void PathTracingDialog::onFireflyClampChanged(double value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingFireflyClampThreshold(static_cast<float>(value));
}

void PathTracingDialog::onMaxTransmissionBouncesChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingMaxTransmissionBounces(value);
}

void PathTracingDialog::onRussianRouletteDepthChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingRussianRouletteStartDepth(value);
}

void PathTracingDialog::onEnvImportanceSamplingToggled(bool checked)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingEnvImportanceSamplingEnabled(checked);
}

void PathTracingDialog::onRenderClicked()
{
	if (!_modelViewer)
		return;

	// Routes through the same entry point the toolbar's render-mode menu
	// uses (see ModelViewer::onRenderingModeSelected()) rather than calling
	// ViewportWidget's arm methods directly, so the toolbar's active-mode
	// indicator stays in sync with this dialog's Render/Stop state.
	_modelViewer->onRenderingModeSelected("PathTraced");

	if (ViewportWidget* viewport = _modelViewer->getViewportWidget())
		viewport->requestPathTracedRenderNow(); // start immediately, don't wait for the idle-settle countdown
	_stoppedByUser = false;
}

void PathTracingDialog::onStopClicked()
{
	if (!_modelViewer)
		return;

	// Falls all the way back to plain PBR raster, matching what selecting
	// "PBR" from the toolbar's own menu would do - leaves no dangling
	// "armed but stopped" state for the toolbar to disagree with.
	_modelViewer->onRenderingModeSelected("PBR");
	_stoppedByUser = true; // see this flag's doc comment - display Idle/0 despite the stale published sample count
}

void PathTracingDialog::onExportClicked()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const QString path = QFileDialog::getSaveFileName(this, tr("Export Path-Traced Image"),
		QString(), tr("PNG Image (*.png)"));
	if (path.isEmpty())
		return;

	// captureCleanPathTracedImage() reuses the viewport's own RtPresenter
	// tonemap/gamma pipeline (guaranteed pixel-identical to what's shown on
	// screen, not a second, possibly-diverging implementation) but with the
	// axis triad/view cube/mesh-count HUD overlays suppressed - a plain
	// grabFramebuffer() would capture those too, which isn't wanted in a
	// render-to-file export. Export is only enabled once the session has
	// converged (see onProgressTimer()).
	const QImage frame = viewport->captureCleanPathTracedImage();
	frame.save(path, "PNG");
}

void PathTracingDialog::onRestoreDefaultsClicked()
{
	// Matches CpuPathTracer::Settings/RtPathTracingSession's own struct
	// defaults exactly (also what the .ui's widgets are initialized to) -
	// worth having explicitly now that every value here persists via
	// QSettings across app restarts (see loadSettings()/saveSettings()) and
	// can otherwise drift somewhere odd with no easy way back.
	ui->spinBoxMaxSamples->setValue(128);
	ui->spinBoxMaxBounces->setValue(6);
	ui->checkBoxDenoiser->setChecked(true);
	ui->doubleSpinBoxFireflyClamp->setValue(3.0);
	ui->spinBoxMaxTransmissionBounces->setValue(32);
	ui->spinBoxRussianRouletteDepth->setValue(3);
	ui->checkBoxEnvImportanceSampling->setChecked(true);
	// Each setValue()/setChecked() above already emitted its usual
	// valueChanged/toggled signal (unchanged from any other edit), pushing
	// the reset value into the viewport via this dialog's existing slots -
	// no separate push-to-viewport step needed here.
}

void PathTracingDialog::onProgressTimer()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	uint32_t current = 0, target = 0;
	bool running = false;
	viewport->pathTracingProgress(current, target, running);

	if (running)
		_stoppedByUser = false; // real progress again - via this dialog's Render, or the camera settling/restarting on its own
	if (_stoppedByUser)
		current = 0;

	ui->progressBarSamples->setMaximum(static_cast<int>(std::max<uint32_t>(target, 1)));
	ui->progressBarSamples->setValue(static_cast<int>(current));
	ui->labelStatus->setText(running
		? tr("Rendering... %1 / %2 samples").arg(current).arg(target)
		: (current > 0 ? tr("Converged: %1 / %2 samples").arg(current).arg(target) : tr("Idle")));

	updateButtonsForState(running, current);
}

void PathTracingDialog::updateButtonsForState(bool running, uint32_t currentSamples)
{
	ui->pushButtonRender->setEnabled(!running);
	ui->pushButtonStop->setEnabled(running);
	ui->pushButtonExport->setEnabled(!running && currentSamples > 0);
}
