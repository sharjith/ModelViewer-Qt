#include "PathTracingDialog.h"
#include "ui_PathTracingDialog.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "VisualizationEnvironmentPanel.h"
#include "RtTonemap.h"

#include <QTimer>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QImage>
#include <QCloseEvent>
#include <QSettings>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QApplication>
#include <QEventLoop>
#include <QLocale>
#include <QStandardItemModel>

#include <ImfRgbaFile.h>
#include <ImfArray.h>

#include <algorithm>
#include <iterator>

namespace
{
	// Walks up the parent chain from a widget inside the MDI area to find
	// the QMdiArea itself - ModelViewer has no direct pointer to it, and
	// QMdiSubWindow doesn't expose one either, but the widget hierarchy
	// always leads there regardless of exactly how many internal viewport/
	// container widgets sit in between.
	QMdiArea* findMdiArea(QWidget* widget)
	{
		for (QWidget* w = widget; w; w = w->parentWidget())
		{
			if (auto* area = qobject_cast<QMdiArea*>(w))
				return area;
		}
		return nullptr;
	}
}

namespace
{
	// Common export resolution presets - index 0 ("Custom") is a real,
	// selectable no-op item rather than an implicit/hidden state, so the
	// combo box's displayed label always matches what the spinboxes
	// actually hold instead of silently going stale.
	struct ResolutionPreset { const char* label; int width; int height; };
	const ResolutionPreset kResolutionPresets[] = {
		{ "Custom", 0, 0 },
		{ "HD (1280 x 720)", 1280, 720 },
		{ "Full HD (1920 x 1080)", 1920, 1080 },
		{ "QHD (2560 x 1440)", 2560, 1440 },
		{ "4K UHD (3840 x 2160)", 3840, 2160 },
		{ "8K UHD (7680 x 4320)", 7680, 4320 },
	};
}

namespace
{
	// Writes a linear HDR RGB buffer straight to an OpenEXR file - half-
	// float (Imf::Rgba) storage via the RgbaOutputFile convenience API
	// rather than the full 32-bit-float OutputFile/FrameBuffer API, since
	// half-float is the conventional storage precision for rendered output
	// (matches what Blender/most renderers write by default) and needs far
	// less boilerplate. Returns false (and leaves no partial file, since
	// OpenEXR only creates the file on first writePixels()) on any failure.
	bool writeExrFile(const QString& path, const std::vector<glm::vec3>& linearRgb, int width, int height)
	{
		if (width <= 0 || height <= 0 || linearRgb.size() != static_cast<size_t>(width) * height)
			return false;

		try
		{
			Imf::Array2D<Imf::Rgba> pixels(height, width);
			for (int y = 0; y < height; ++y)
			{
				for (int x = 0; x < width; ++x)
				{
					const glm::vec3& c = linearRgb[static_cast<size_t>(y) * width + x];
					pixels[y][x] = Imf::Rgba(c.r, c.g, c.b, 1.0f);
				}
			}

			Imf::RgbaOutputFile file(path.toUtf8().constData(), width, height, Imf::WRITE_RGBA);
			file.setFrameBuffer(&pixels[0][0], 1, width);
			file.writePixels(height);
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	// Box-filter (area-average) downscale of a linear HDR buffer - used by
	// the fast export path when the requested resolution is smaller than
	// what's already converged in the live viewport (see
	// PathTracingDialog::onExportClicked()). Only ever called with
	// dstW<=srcW/dstH<=srcH (upscaling goes through the offline render path
	// instead, which produces genuinely new samples at that resolution
	// rather than fabricating detail via interpolation).
	std::vector<glm::vec3> downscaleLinearBuffer(const std::vector<glm::vec3>& src, int srcW, int srcH, int dstW, int dstH)
	{
		std::vector<glm::vec3> dst(static_cast<size_t>(dstW) * dstH, glm::vec3(0.0f));
		for (int dy = 0; dy < dstH; ++dy)
		{
			const int sy0 = (dy * srcH) / dstH;
			const int sy1 = std::max(sy0 + 1, ((dy + 1) * srcH) / dstH);
			for (int dx = 0; dx < dstW; ++dx)
			{
				const int sx0 = (dx * srcW) / dstW;
				const int sx1 = std::max(sx0 + 1, ((dx + 1) * srcW) / dstW);

				glm::vec3 sum(0.0f);
				int count = 0;
				for (int sy = sy0; sy < sy1 && sy < srcH; ++sy)
				{
					for (int sx = sx0; sx < sx1 && sx < srcW; ++sx)
					{
						sum += src[static_cast<size_t>(sy) * srcW + sx];
						++count;
					}
				}
				dst[static_cast<size_t>(dy) * dstW + dx] = count > 0 ? sum / static_cast<float>(count) : glm::vec3(0.0f);
			}
		}
		return dst;
	}
}

PathTracingDialog::PathTracingDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
	, _progressTimer(new QTimer(this))
	, ui(std::make_unique<Ui::PathTracingDialog>())
{
	ui->setupUi(this);
	setModal(false); // watch the viewport update live while adjusting settings

	// The OptiX item uses NVIDIA's own AI denoiser (optixDenoiserInvoke()) -
	// a genuinely different model/API from the OIDN CPU/GPU-CUDA options
	// above it, not just another OIDN device type. Works regardless of which
	// render engine (Embree CPU or OptiX GPU) actually produced the frame,
	// since RtDenoiser owns its own standalone OptixDeviceContext rather than
	// reusing the path tracer's - it only needs an NVIDIA GPU with OptiX
	// support present. Falls back to the built-in bilateral filter (never
	// silently to OIDN) if that's not the case, or this build has no OptiX
	// SDK at all.
	if (auto* model = qobject_cast<QStandardItemModel*>(ui->comboBoxDenoiserDevice->model()))
	{
		constexpr int kOptixItemIndex = 3;
		if (QStandardItem* item = model->item(kOptixItemIndex))
		{
			item->setToolTip(tr("NVIDIA's own AI denoiser. Works with either render engine - falls back to "
				"the bilateral filter if no OptiX-capable NVIDIA GPU is available."));
		}
	}

	populateResolutionPresets();
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
		ui->spinBoxMaxShadowRayHits->setValue(viewport->pathTracingMaxShadowRayHits());
		ui->spinBoxMaxVolumeScatterBounces->setValue(viewport->pathTracingMaxVolumeScatterBounces());
		ui->checkBoxEnvImportanceSampling->setChecked(viewport->pathTracingEnvImportanceSamplingEnabled());
		// Shadows/Self Shadows are NOT owned by this dialog (or persisted via
		// its "pathtracing/*" QSettings keys) - they're the same viewport-wide
		// toggles the main Environment panel already controls. This is just a
		// convenience mirror, kept in sync with whichever panel changed them
		// last via onProgressTimer()'s poll (see its own comment).
		ui->checkBoxShadows->setChecked(viewport->areShadowsEnabled());
		ui->checkBoxSelfShadows->setChecked(viewport->areSelfShadowsEnabled());
		ui->comboBoxDenoiserDevice->setCurrentIndex(static_cast<int>(viewport->pathTracingDenoiserDevicePreference()));
		ui->comboBoxRenderEngine->setCurrentIndex(static_cast<int>(viewport->pathTracingEnginePreference()));
		updateMaxShadowRayHitsEnabled();

		// Export resolution defaults fresh to the CURRENT viewport size every
		// time the dialog opens (not persisted via QSettings like the other
		// controls) - a stale custom resolution left over from a differently-
		// sized document/session wouldn't be meaningful, and "Match Viewport"
		// is right there for whenever the user wants to reset to it anyway.
		int viewportWidth = 0, viewportHeight = 0;
		viewport->pathTracingViewportResolution(viewportWidth, viewportHeight);
		ui->spinBoxExportWidth->setValue(std::max(1, viewportWidth));
		ui->spinBoxExportHeight->setValue(std::max(1, viewportHeight));
		syncResolutionPresetFromSpinboxes();
	}

	connect(ui->spinBoxMaxSamples, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxSamplesChanged);
	connect(ui->spinBoxMaxBounces, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxBouncesChanged);
	connect(ui->checkBoxDenoiser, &QCheckBox::toggled, this, &PathTracingDialog::onDenoiserToggled);
	connect(ui->comboBoxDenoiserDevice, qOverload<int>(&QComboBox::currentIndexChanged), this, &PathTracingDialog::onDenoiserDeviceChanged);
	connect(ui->comboBoxRenderEngine, qOverload<int>(&QComboBox::currentIndexChanged), this, &PathTracingDialog::onRenderEngineChanged);
	connect(ui->doubleSpinBoxFireflyClamp, &QDoubleSpinBox::valueChanged, this, &PathTracingDialog::onFireflyClampChanged);
	connect(ui->spinBoxMaxTransmissionBounces, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxTransmissionBouncesChanged);
	connect(ui->spinBoxRussianRouletteDepth, &QSpinBox::valueChanged, this, &PathTracingDialog::onRussianRouletteDepthChanged);
	connect(ui->spinBoxMaxShadowRayHits, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxShadowRayHitsChanged);
	connect(ui->spinBoxMaxVolumeScatterBounces, &QSpinBox::valueChanged, this, &PathTracingDialog::onMaxVolumeScatterBouncesChanged);
	connect(ui->checkBoxEnvImportanceSampling, &QCheckBox::toggled, this, &PathTracingDialog::onEnvImportanceSamplingToggled);
	connect(ui->checkBoxShadows, &QCheckBox::toggled, this, &PathTracingDialog::onShadowsToggled);
	connect(ui->checkBoxSelfShadows, &QCheckBox::toggled, this, &PathTracingDialog::onSelfShadowsToggled);
	connect(ui->pushButtonRender, &QPushButton::clicked, this, &PathTracingDialog::onRenderClicked);
	connect(ui->pushButtonStop, &QPushButton::clicked, this, &PathTracingDialog::onStopClicked);
	connect(ui->pushButtonExport, &QPushButton::clicked, this, &PathTracingDialog::onExportClicked);
	connect(ui->pushButtonRestoreDefaults, &QPushButton::clicked, this, &PathTracingDialog::onRestoreDefaultsClicked);
	connect(ui->spinBoxExportWidth, &QSpinBox::valueChanged, this, &PathTracingDialog::onExportResolutionChanged);
	connect(ui->spinBoxExportHeight, &QSpinBox::valueChanged, this, &PathTracingDialog::onExportResolutionChanged);
	connect(ui->pushButtonMatchViewport, &QPushButton::clicked, this, &PathTracingDialog::onMatchViewportClicked);
	connect(ui->comboBoxResolutionPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, &PathTracingDialog::onResolutionPresetSelected);

	_progressTimer->setInterval(200);
	connect(_progressTimer, &QTimer::timeout, this, &PathTracingDialog::onProgressTimer);
	_progressTimer->start();

	onProgressTimer(); // reflect whatever's already running (dialog reopened mid-render) immediately
	updateResolutionWarning(); // reflect the freshly-initialized export resolution above

	// Hide/show this dialog as its OWN document's MDI subwindow loses/gains
	// focus - this app uses QMdiArea::SubWindowView (not tabbed), so
	// switching the active document does NOT hide/show widgets on its own
	// (all subwindows can be simultaneously visible, just not focused), and
	// without this a dialog opened for one document kept showing - and
	// still controlling - that original document even while a completely
	// different one was the active tab, which read as ambiguous/wrong.
	if (_modelViewer)
	{
		if (QMdiArea* mdiArea = findMdiArea(_modelViewer))
			connect(mdiArea, &QMdiArea::subWindowActivated, this, &PathTracingDialog::onActiveSubWindowChanged);
	}
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

	// The viewport already loaded these once unconditionally at construction
	// (see loadPathTracingSettingsFromDisk()'s doc comment - Path Tracing can
	// trigger via the idle timer without this dialog ever having been
	// opened, so that load can't depend on the dialog). Re-running it here
	// picks up any changes made to the settings file outside this session
	// (or a future settings-reset feature) before the UI widgets below read
	// the viewport's values.
	viewport->loadPathTracingSettingsFromDisk();
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
	settings.setValue("pathtracing/maxShadowRayHits", viewport->pathTracingMaxShadowRayHits());
	settings.setValue("pathtracing/maxVolumeScatterBounces", viewport->pathTracingMaxVolumeScatterBounces());
	settings.setValue("pathtracing/envImportanceSampling", viewport->pathTracingEnvImportanceSamplingEnabled());
	settings.setValue("pathtracing/denoiserDevicePreference", static_cast<int>(viewport->pathTracingDenoiserDevicePreference()));
	settings.setValue("pathtracing/enginePreference", static_cast<int>(viewport->pathTracingEnginePreference()));
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

void PathTracingDialog::onDenoiserDeviceChanged(int index)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingDenoiserDevicePreference(static_cast<DenoiserDevicePreference>(index));
}

void PathTracingDialog::onRenderEngineChanged(int index)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingEnginePreference(static_cast<RtPathTracingEnginePreference>(index));
	updateMaxShadowRayHitsEnabled();
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

void PathTracingDialog::onMaxShadowRayHitsChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingMaxShadowRayHits(value);
}

void PathTracingDialog::onMaxVolumeScatterBouncesChanged(int value)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingMaxVolumeScatterBounces(value);
}

void PathTracingDialog::onEnvImportanceSamplingToggled(bool checked)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->setPathTracingEnvImportanceSamplingEnabled(checked);
}

void PathTracingDialog::onShadowsToggled(bool checked)
{
	// Not a "pathtracing/*" setting of this dialog's own - see the
	// constructor's identical comment on checkBoxShadows/checkBoxSelfShadows.
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->showShadows(checked);
	// Push the change back to the main Environment panel's own checkboxes too -
	// onProgressTimer()'s poll only pulls FROM the viewport into this dialog,
	// it doesn't know to also push into that other, unrelated panel.
	if (_modelViewer && _modelViewer->visualizationEnvironmentPanel)
		_modelViewer->visualizationEnvironmentPanel->syncShadowCheckboxes(checked, ui->checkBoxSelfShadows->isChecked());
}

void PathTracingDialog::onSelfShadowsToggled(bool checked)
{
	if (ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr)
		viewport->showSelfShadows(checked);
	if (_modelViewer && _modelViewer->visualizationEnvironmentPanel)
		_modelViewer->visualizationEnvironmentPanel->syncShadowCheckboxes(ui->checkBoxShadows->isChecked(), checked);
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
		viewport->requestPathTracedRenderNow(); // start immediately, don't wait for the idle-settle countdown - also (re)starts ViewportWidget's own elapsed-time clock
	_stoppedByUser = false;
	_frozenElapsedMs = -1;
}

void PathTracingDialog::onStopClicked()
{
	if (!_modelViewer)
		return;

	// Repurposed as Cancel for a blocking offline export - see
	// _offlineRenderInProgress's doc comment. There is no interactive
	// session to stop in that case (renderPathTracedOffline() doesn't
	// start one), so the normal fall-back-to-raster behavior below doesn't
	// apply at all here.
	if (_offlineRenderInProgress)
	{
		ViewportWidget* viewport = _modelViewer->getViewportWidget();
		if (viewport)
			viewport->cancelPathTracedOfflineRender();
		return;
	}

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

	const int targetWidth  = ui->spinBoxExportWidth->value();
	const int targetHeight = ui->spinBoxExportHeight->value();

	int viewportWidth = 0, viewportHeight = 0;
	viewport->pathTracingViewportResolution(viewportWidth, viewportHeight);

	// Only an offline render can produce genuinely NEW detail at a higher
	// resolution - anything at or below what's already converged can just
	// be downscaled from it instead (see updateResolutionWarning(), which
	// surfaces this same condition live as the user edits the spinboxes).
	const bool needsOfflineRender = targetWidth > viewportWidth || targetHeight > viewportHeight;

	if (needsOfflineRender)
	{
		const auto reply = QMessageBox::question(this, tr("Offline Render Required"),
			tr("The requested export resolution (%1x%2) exceeds the current viewport size (%3x%4).\n\n"
			   "This will run a fresh offline render at the requested resolution, which may take a "
			   "while - you can cancel it at any time using the Cancel button that replaces Stop "
			   "for the duration.\n\nContinue?")
				.arg(targetWidth).arg(targetHeight).arg(viewportWidth).arg(viewportHeight),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (reply != QMessageBox::Yes)
			return;
	}

	// Auto-computed default filename: docname_NNNspp_WxH.ext - the sample
	// count reflects whichever render will actually produce the exported
	// pixels (the already-converged live one for the fast path, or the
	// target the offline render is about to run to).
	uint32_t currentSamples = 0, targetSamples = 0;
	bool running = false;
	viewport->pathTracingProgress(currentSamples, targetSamples, running);
	const uint32_t sampleCountForFilename = needsOfflineRender ? viewport->pathTracingMaxSamples() : currentSamples;

	QString docName = tr("render");
	QString docFolder;
	if (_modelViewer && !_modelViewer->currentFile().isEmpty())
	{
		const QFileInfo currentFileInfo(_modelViewer->currentFile());
		docName = currentFileInfo.completeBaseName();
		docFolder = currentFileInfo.absolutePath();
	}
	const QString defaultName = QString("%1_%2spp_%3x%4")
		.arg(docName).arg(sampleCountForFilename).arg(targetWidth).arg(targetHeight);

	// getSaveFileName() with a bare filename (no directory component) falls
	// back to whatever the platform/Qt itself considers "current" - often
	// the app's working directory, not anywhere a user would expect to find
	// their export. Prefer the currently loaded model's own folder (most
	// export renders belong right next to their source model); fall back to
	// the user's Documents folder when no model is loaded yet.
	const QString defaultFolder = !docFolder.isEmpty()
		? docFolder
		: QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString defaultPath = QDir(defaultFolder).filePath(defaultName);

	QString selectedFilter;
	const QString path = QFileDialog::getSaveFileName(this, tr("Export Path-Traced Image"), defaultPath,
		tr("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;BMP Image (*.bmp);;TIFF Image (*.tif *.tiff);;OpenEXR Image (*.exr)"),
		&selectedFilter);
	if (path.isEmpty())
		return;

	// Derived from the selected FILTER rather than relying on QImage::save()
	// to infer format from the path's extension - getSaveFileName() doesn't
	// reliably append one if the user typed a filename with no/unexpected
	// extension, which would otherwise make save() fail silently.
	const bool exportExr = selectedFilter.contains("*.exr");
	QString ldrFormat = "PNG";
	if (selectedFilter.contains("*.jpg"))
		ldrFormat = "JPG";
	else if (selectedFilter.contains("*.bmp"))
		ldrFormat = "BMP";
	else if (selectedFilter.contains("*.tif"))
		ldrFormat = "TIFF";

	bool saveOk = false;

	if (needsOfflineRender)
	{
		// Genuinely blocks this thread for the whole render (see
		// ViewportWidget::renderPathTracedOffline()'s doc comment) - the
		// user explicitly confirmed that tradeoff above. Stop the
		// interactive progress poll for the duration, since
		// QApplication::processEvents() below would otherwise let
		// onProgressTimer() fire mid-loop and clobber the offline-progress
		// display with the (unrelated, possibly stale) interactive
		// session's own state.
		//
		// Deliberately does NOT setEnabled(false) the whole dialog the way
		// an earlier version did - that left no way to cancel a long
		// export short of force-closing the app. Instead, every control
		// EXCEPT pushButtonStop is disabled individually (Qt's effective
		// enabled state considers the whole ancestor chain, so a child
		// can't be selectively re-enabled once its parent dialog itself is
		// disabled - this dialog has to stay enabled for Stop to be
		// clickable at all), and that one button is repurposed as Cancel
		// for the duration (see onStopClicked()'s _offlineRenderInProgress
		// branch). processEvents() below also drops
		// ExcludeUserInputEvents - that flag was specifically what made a
		// Cancel click impossible to ever process while blocked here; now
		// that everything else is disabled and therefore inert to input
		// regardless, allowing user-input events through only lets the one
		// enabled button (Stop/Cancel) actually do anything.
		_progressTimer->stop();
		ui->tabWidgetSettings->setEnabled(false);
		ui->comboBoxResolutionPreset->setEnabled(false);
		ui->spinBoxExportWidth->setEnabled(false);
		ui->spinBoxExportHeight->setEnabled(false);
		ui->pushButtonMatchViewport->setEnabled(false);
		ui->pushButtonRender->setEnabled(false);
		ui->pushButtonExport->setEnabled(false);
		ui->pushButtonRestoreDefaults->setEnabled(false);
		ui->pushButtonStop->setEnabled(true);
		ui->pushButtonStop->setText(tr("Cancel"));
		_offlineRenderInProgress = true;
		QApplication::setOverrideCursor(Qt::WaitCursor);

		// See ViewportWidget::renderPathTracedOffline()'s own doc comment -
		// it (re)starts the session elapsed clock for the export itself.
		// Reset here too so the very first progress tick below (before the
		// clock has ticked meaningfully) doesn't briefly show a leftover
		// value from whatever ran before, and so onProgressTimer()'s own
		// freeze logic re-reads fresh once it resumes after this render
		// (see that logic's own doc comment) instead of keeping whatever
		// was frozen from a PREVIOUS interactive session.
		_frozenElapsedMs = -1;

		std::vector<glm::vec3> linearRgb;
		bool cancelled = false;
		const bool renderedOk = viewport->renderPathTracedOffline(targetWidth, targetHeight,
			[this, viewport](uint32_t currentSample, uint32_t maxSamples)
			{
				ui->progressBarSamples->setMaximum(static_cast<int>(std::max<uint32_t>(maxSamples, 1)));
				ui->progressBarSamples->setValue(static_cast<int>(currentSample));
				ui->labelStatus->setText(tr("Offline rendering... %1 / %2 samples (Cancel to stop)").arg(currentSample).arg(maxSamples));
				ui->labelElapsedTime->setText(formatElapsedTime(viewport->pathTracingElapsedMs()));
				QApplication::processEvents();
			},
			linearRgb, &cancelled);

		_offlineRenderInProgress = false;
		ui->pushButtonStop->setText(tr("Stop"));
		ui->tabWidgetSettings->setEnabled(true);
		ui->comboBoxResolutionPreset->setEnabled(true);
		ui->spinBoxExportWidth->setEnabled(true);
		ui->spinBoxExportHeight->setEnabled(true);
		ui->pushButtonMatchViewport->setEnabled(true);
		ui->pushButtonRestoreDefaults->setEnabled(true);
		QApplication::restoreOverrideCursor();
		// Freeze the display at the export's own final elapsed time, matching
		// onProgressTimer()'s "freeze once the session stops being active"
		// convention exactly, rather than leaving _frozenElapsedMs at -1 and
		// letting the next poll tick re-derive it implicitly.
		_frozenElapsedMs = viewport->pathTracingElapsedMs();
		_progressTimer->start();
		// Render/Export/Stop deliberately NOT hardcoded enabled/disabled here
		// like the controls above - this offline render ran entirely
		// independently of whatever the interactive PT session's own actual
		// running/converged state is (see ViewportWidget::
		// renderPathTracedOffline()'s doc comment), so the correct button
		// state to land on depends on that live state, not "an offline render
		// just finished" alone. updateButtonsForState() is the single source
		// of truth for that (also what every 200ms poll tick uses) - calling
		// it here immediately, rather than waiting up to 200ms for the next
		// tick, matches onExportResolutionChanged()'s identical "must reflect
		// reality instantly" precedent instead of leaving a brief stale-button
		// window right when the user is most likely to reach for Export next.
		updateButtonsForState();

		if (cancelled)
		{
			ui->labelStatus->setText(tr("Export cancelled."));
			return;
		}

		if (!renderedOk)
		{
			QMessageBox::warning(this, tr("Export Failed"), tr("Could not render at the requested resolution."));
			return;
		}

		// The offline path never touches the GPU/live framebuffer at all,
		// so for LDR formats the linear buffer has to be tonemapped here in
		// plain C++ (RtTonemap.h, a direct port of path_traced_present.frag)
		// using the SAME live settings the on-screen viewport uses - there's
		// no already-tonemapped framebuffer to grab like the fast path has.
		if (exportExr)
		{
			saveOk = writeExrFile(path, linearRgb, targetWidth, targetHeight);
		}
		else
		{
			bool hdrToneMapping = true, gammaCorrection = true;
			float screenGamma = 2.2f, iblExposure = 1.0f;
			int toneMapMode = 0;
			viewport->pathTracingToneMapSettings(hdrToneMapping, gammaCorrection, screenGamma, iblExposure, toneMapMode);

			QImage image(targetWidth, targetHeight, QImage::Format_RGB888);
			for (int y = 0; y < targetHeight; ++y)
			{
				uchar* line = image.scanLine(y);
				for (int x = 0; x < targetWidth; ++x)
				{
					const glm::vec3& c = linearRgb[static_cast<size_t>(y) * targetWidth + x];
					const glm::vec3 mapped = RtTonemap::apply(c, hdrToneMapping, gammaCorrection, screenGamma, iblExposure, toneMapMode);
					line[x * 3 + 0] = static_cast<uchar>(std::clamp(mapped.r, 0.0f, 1.0f) * 255.0f + 0.5f);
					line[x * 3 + 1] = static_cast<uchar>(std::clamp(mapped.g, 0.0f, 1.0f) * 255.0f + 0.5f);
					line[x * 3 + 2] = static_cast<uchar>(std::clamp(mapped.b, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}
			saveOk = image.save(path, ldrFormat.toUtf8().constData());
		}
	}
	else if (exportExr)
	{
		// Fast path, EXR: reuse the live viewport's raw linear buffer before
		// RtPresenter's tonemap/gamma pass ever touches it. Depending on what
		// is currently driving PT, that may come from the CPU session, the
		// settled GPU session, or the interactive GPU renderer's latest
		// completed device frame via a readback. Downscaled if the requested
		// resolution is smaller than what's already there (never upscaled -
		// that goes through the offline branch above).
		int liveWidth = 0, liveHeight = 0;
		std::vector<glm::vec3> linearRgb = viewport->pathTracingRawFrame(liveWidth, liveHeight);
		if (liveWidth <= 0 || liveHeight <= 0 || linearRgb.empty())
		{
			// Narrow but real gap: interactive GPU PT publishes its first
			// frame asynchronously (see RtInteractiveRenderer::pollCompletedFrame()),
			// so a frame can genuinely not exist yet for the first instant or
			// two after a drag/orbit starts. Without this check,
			// downscaleLinearBuffer()'s divide-by-zero guard would silently
			// hand back an all-zero buffer that writeExrFile() happily writes
			// as a valid (but blank) EXR - reporting "Export Complete" for an
			// image that isn't what the user actually sees on screen.
			QMessageBox::warning(this, tr("Export Failed"),
				tr("No live path-traced frame is available yet. Wait a moment for rendering to produce a frame, then try again."));
			return;
		}
		if (liveWidth != targetWidth || liveHeight != targetHeight)
			linearRgb = downscaleLinearBuffer(linearRgb, liveWidth, liveHeight, targetWidth, targetHeight);
		saveOk = writeExrFile(path, linearRgb, targetWidth, targetHeight);
	}
	else
	{
		// Fast path, LDR: captureCleanPathTracedImage() reuses the
		// viewport's own RtPresenter tonemap/gamma pipeline (guaranteed
		// pixel-identical to what's shown on screen, not a second,
		// possibly-diverging implementation) but with the axis triad/view
		// cube/mesh-count HUD overlays suppressed - a plain grabFramebuffer()
		// would capture those too, which isn't wanted in a render-to-file
		// export.
		QImage image = viewport->captureCleanPathTracedImage();
		if (image.width() != targetWidth || image.height() != targetHeight)
			image = image.scaled(targetWidth, targetHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		saveOk = image.save(path, ldrFormat.toUtf8().constData());
	}

	if (!saveOk)
	{
		QMessageBox::warning(this, tr("Export Failed"), tr("Could not write the image file. See the log for details."));
		return;
	}

	QMessageBox::information(this, tr("Export Complete"), tr("Image exported successfully to:\n%1").arg(path));
}

void PathTracingDialog::onRestoreDefaultsClicked()
{
	// Matches CpuPathTracer::Settings/RtPathTracingSession's own struct
	// defaults exactly (also what the .ui's widgets are initialized to) -
	// worth having explicitly now that every value here persists via
	// QSettings across app restarts (see loadSettings()/saveSettings()) and
	// can otherwise drift somewhere odd with no easy way back.
	ui->spinBoxMaxSamples->setValue(16);
	ui->spinBoxMaxBounces->setValue(6);
	ui->checkBoxDenoiser->setChecked(true);
	ui->doubleSpinBoxFireflyClamp->setValue(3.0);
	ui->spinBoxMaxTransmissionBounces->setValue(32);
	ui->spinBoxRussianRouletteDepth->setValue(3);
	ui->spinBoxMaxShadowRayHits->setValue(8);
	ui->spinBoxMaxVolumeScatterBounces->setValue(64);
	ui->checkBoxEnvImportanceSampling->setChecked(true);
	// Shadows/Self Shadows deliberately NOT reset here - not this dialog's
	// own setting (see checkBoxShadows/checkBoxSelfShadows' doc comment).
	ui->comboBoxDenoiserDevice->setCurrentIndex(static_cast<int>(DenoiserDevicePreference::Auto));
	ui->comboBoxRenderEngine->setCurrentIndex(static_cast<int>(RtPathTracingEnginePreference::Auto));
	// Each setValue()/setChecked() above already emitted its usual
	// valueChanged/toggled signal (unchanged from any other edit), pushing
	// the reset value into the viewport via this dialog's existing slots -
	// no separate push-to-viewport step needed here.

	onMatchViewportClicked(); // export resolution has no fixed "default" - viewport size is the sane reset target
}

void PathTracingDialog::onExportResolutionChanged()
{
	updateResolutionWarning();
	// Export must become enabled the instant a resolution larger than the
	// viewport is entered - see updateButtonsForState()'s own doc comment -
	// not wait for onProgressTimer()'s next poll tick.
	updateButtonsForState();
	if (!_updatingResolutionFromPreset)
		syncResolutionPresetFromSpinboxes();
}

void PathTracingDialog::onMatchViewportClicked()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	int viewportWidth = 0, viewportHeight = 0;
	viewport->pathTracingViewportResolution(viewportWidth, viewportHeight);
	ui->spinBoxExportWidth->setValue(std::max(1, viewportWidth));
	ui->spinBoxExportHeight->setValue(std::max(1, viewportHeight));
	syncResolutionPresetFromSpinboxes();

	// Explicit final check rather than relying solely on the two setValue()
	// calls' own valueChanged->onExportResolutionChanged() chain - if only
	// ONE dimension actually changes value (the other already equals the
	// viewport's size), QSpinBox::setValue() doesn't emit valueChanged for
	// the untouched one at all, so the warning label's disappearance (and
	// the dialog's resize-to-fit) can't be left depending on which
	// spinbox's signal happens to fire.
	updateResolutionWarning();
}

void PathTracingDialog::populateResolutionPresets()
{
	ui->comboBoxResolutionPreset->clear();
	for (const ResolutionPreset& preset : kResolutionPresets)
		ui->comboBoxResolutionPreset->addItem(QString::fromLatin1(preset.label));
}

void PathTracingDialog::onResolutionPresetSelected(int index)
{
	if (index <= 0 || index >= static_cast<int>(std::size(kResolutionPresets)))
		return; // "Custom" (index 0), or invalid - nothing to apply

	// The setValue() calls below each fire onExportResolutionChanged(),
	// which always updates the warning label regardless of this flag - it
	// only skips the combo-resync half (which would otherwise be a
	// redundant, if harmless, re-apply of the same preset just selected).
	_updatingResolutionFromPreset = true;
	ui->spinBoxExportWidth->setValue(kResolutionPresets[index].width);
	ui->spinBoxExportHeight->setValue(kResolutionPresets[index].height);
	_updatingResolutionFromPreset = false;

	// Same reasoning as onMatchViewportClicked()'s trailing call - can't
	// rely on both setValue() calls actually firing valueChanged.
	updateResolutionWarning();
}

void PathTracingDialog::syncResolutionPresetFromSpinboxes()
{
	const int w = ui->spinBoxExportWidth->value();
	const int h = ui->spinBoxExportHeight->value();

	int matchIndex = 0; // "Custom"
	for (int i = 1; i < static_cast<int>(std::size(kResolutionPresets)); ++i)
	{
		if (kResolutionPresets[i].width == w && kResolutionPresets[i].height == h)
		{
			matchIndex = i;
			break;
		}
	}

	// blockSignals rather than relying on _updatingResolutionFromPreset here
	// - this function is the one that sets the combo, not the spinboxes, so
	// it needs its own guard against onResolutionPresetSelected() firing
	// right back (which would be a harmless no-op re-apply in practice, but
	// blocking it outright is simpler than reasoning about that).
	ui->comboBoxResolutionPreset->blockSignals(true);
	ui->comboBoxResolutionPreset->setCurrentIndex(matchIndex);
	ui->comboBoxResolutionPreset->blockSignals(false);
}

void PathTracingDialog::updateMaxShadowRayHitsEnabled()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const bool cpuActive = viewport->effectivePathTracingEnginePreference() == RtPathTracingEnginePreference::CPU;
	ui->labelMaxShadowRayHits->setEnabled(cpuActive);
	ui->spinBoxMaxShadowRayHits->setEnabled(cpuActive);
}

void PathTracingDialog::updateResolutionWarning()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	int viewportWidth = 0, viewportHeight = 0;
	viewport->pathTracingViewportResolution(viewportWidth, viewportHeight);

	const bool exceedsViewport = ui->spinBoxExportWidth->value() > viewportWidth
		|| ui->spinBoxExportHeight->value() > viewportHeight;

	// adjustSize() only on an actual visibility CHANGE, mirroring
	// labelOrthoThinWallWarning's identical reasoning in onProgressTimer().
	if (exceedsViewport != ui->labelResolutionWarning->isVisible())
	{
		ui->labelResolutionWarning->setVisible(exceedsViewport);
		adjustSize();
	}
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

	// See _frozenElapsedMs's doc comment - ticks live (read straight from
	// ViewportWidget's own session clock) while running, continuously
	// updating _frozenElapsedMs to that same live value so it's always ready
	// to serve as "the render's final duration" the instant running stops -
	// no separate transition-detection needed, and a brand NEW session
	// naturally overwrites the old frozen value the moment it starts
	// publishing progress again (ViewportWidget's own session clock restarts
	// then too - see pathTracingElapsedMs()'s doc comment). Once stopped,
	// just re-displays that last captured value rather than re-reading the
	// live clock, so it does not keep advancing after the render is actually
	// done. Hidden entirely before the first render this dialog has ever
	// observed (running was never true and _frozenElapsedMs is still its
	// initial -1). effectiveElapsedMs (0 until the first render) is also
	// what refreshDiagnostics() uses below, so Render Time/Samples-per-sec/
	// MRays-per-sec freeze in step with this label instead of continuing to
	// climb off ViewportWidget's live, never-reset session clock after Stop
	// is pressed - or, as a previous version of this logic did, going stale
	// at 0 forever after the very first stopped tick (resetting
	// _frozenElapsedMs to -1 on every single running tick meant the one tick
	// where running first went false always found -1 and had nothing to
	// freeze).
	qint64 effectiveElapsedMs = 0;
	if (running)
	{
		effectiveElapsedMs = viewport->pathTracingElapsedMs();
		_frozenElapsedMs = effectiveElapsedMs;
		ui->labelElapsedTime->setText(formatElapsedTime(effectiveElapsedMs));
	}
	else if (_frozenElapsedMs >= 0)
	{
		effectiveElapsedMs = _frozenElapsedMs;
		ui->labelElapsedTime->setText(formatElapsedTime(effectiveElapsedMs));
	}

	// Set once per session start (see ViewportWidget::startPathTracedSession()'s
	// doc comment) - only meaningful once at least one render has actually
	// started, so this stays hidden before the very first Render click.
	// adjustSize() only on an actual visibility CHANGE (not every 200ms
	// poll tick) - the hidden label still reserves no layout space once
	// gone, but the dialog's own top-level size (persisted via QSettings/
	// restoreGeometry()) doesn't automatically shrink to match on its own,
	// leaving an empty gap unless told to re-fit.
	const bool orthoWarningActive = viewport->pathTracingOrthoThinWallWarningActive();
	if (orthoWarningActive != ui->labelOrthoThinWallWarning->isVisible())
	{
		ui->labelOrthoThinWallWarning->setVisible(orthoWarningActive);
		adjustSize();
	}

	// checkBoxShadows/checkBoxSelfShadows mirror viewport-wide state this
	// dialog doesn't own (see their doc comment) - the main Environment panel
	// can change it while this dialog is open, so re-sync every poll rather
	// than only reading it once at construction. blockSignals() avoids
	// re-entering onShadowsToggled()/onSelfShadowsToggled() and redundantly
	// re-applying the same value back to the viewport every 200ms.
	const bool shadowsEnabled = viewport->areShadowsEnabled();
	if (shadowsEnabled != ui->checkBoxShadows->isChecked())
	{
		ui->checkBoxShadows->blockSignals(true);
		ui->checkBoxShadows->setChecked(shadowsEnabled);
		ui->checkBoxShadows->blockSignals(false);
	}
	const bool selfShadowsEnabled = viewport->areSelfShadowsEnabled();
	if (selfShadowsEnabled != ui->checkBoxSelfShadows->isChecked())
	{
		ui->checkBoxSelfShadows->blockSignals(true);
		ui->checkBoxSelfShadows->setChecked(selfShadowsEnabled);
		ui->checkBoxSelfShadows->blockSignals(false);
	}

	// Re-checked every poll (not just on spinbox edits) since resizing the
	// viewport window itself can flip whether the currently-entered export
	// resolution now exceeds it, with no direct signal for that resize.
	updateResolutionWarning();

	// Gated on the Diagnostics tab actually being the visible one right now
	// (per this feature's own design goal) - every field read there is
	// already cheap/precomputed, but there's no reason to touch even that,
	// every 200ms, while nobody can see the result.
	if (ui->tabWidgetSettings->currentWidget() == ui->tabDiagnostics)
		refreshDiagnostics(effectiveElapsedMs);

	updateButtonsForState();
}

void PathTracingDialog::onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow)
{
	// activeSubWindow is null when the last document closes (nothing left
	// to activate) - isOwnDocumentActive correctly comes out false there,
	// hiding this dialog, which is fine since closeEvent()/WA_DeleteOnClose
	// already handle the "this dialog's own document closed" case
	// separately (this slot's job is purely visibility, not lifetime).
	const bool isOwnDocumentActive = _modelViewer
		&& activeSubWindow
		&& activeSubWindow->widget() == static_cast<QWidget*>(_modelViewer);

	// setVisible(), not show()/hide() with extra bookkeeping - Qt already
	// treats repeated identical calls as a no-op, and this does NOT stop or
	// otherwise affect the underlying render (that keeps running in the
	// background regardless of whether this dialog is currently shown) -
	// purely a "which document does this dialog currently belong to"
	// visibility cue.
	setVisible(isOwnDocumentActive);
}

QString PathTracingDialog::formatElapsedTime(qint64 elapsedMs)
{
	const qint64 totalSeconds = elapsedMs / 1000;
	return tr("Elapsed: %1:%2")
		.arg(totalSeconds / 60, 2, 10, QChar('0'))
		.arg(totalSeconds % 60, 2, 10, QChar('0'));
}

void PathTracingDialog::refreshDiagnostics(qint64 effectiveElapsedMs)
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	const ViewportWidget::PathTracingDiagnostics diag = viewport->pathTracingDiagnostics();

	ui->labelDiagRendererValue->setText(diag.rendererName);
	ui->labelDiagGpuValue->setText(diag.gpuDeviceName.isEmpty() ? tr("N/A") : diag.gpuDeviceName);
	ui->labelDiagTraversalValue->setText(!diag.traversalKnown
		? tr("N/A (CPU engine)")
		: (diag.hasHardwareRT ? tr("Hardware RT") : tr("Software (megakernel)")));
	ui->labelDiagDenoiserValue->setText(diag.denoiserName.isEmpty() ? tr("N/A") : diag.denoiserName);

	ui->labelDiagResolutionValue->setText(diag.width > 0 && diag.height > 0
		? tr("%1 x %2").arg(diag.width).arg(diag.height)
		: tr("N/A"));
	ui->labelDiagTrianglesValue->setText(diag.triangleCountKnown
		? QLocale::system().toString(static_cast<qulonglong>(diag.triangleCount))
		: tr("N/A (CPU engine)"));
	ui->labelDiagBlasValue->setText(diag.buildTimesKnown ? tr("%1 ms").arg(diag.gasBuildMs, 0, 'f', 1) : tr("N/A (CPU engine)"));
	ui->labelDiagTlasValue->setText(diag.buildTimesKnown ? tr("%1 ms").arg(diag.iasBuildMs, 0, 'f', 1) : tr("N/A (CPU engine)"));

	// Total (not per-pixel) samples/sec across the whole image - matches how
	// path-tracer throughput is usually quoted (e.g. this is what "10.8 M
	// samples/sec" on a GPU renderer's stats overlay means). Uses
	// effectiveElapsedMs (frozen once rendering stops - see this function's
	// own doc comment), NOT diag.elapsedMs, which is ViewportWidget's live,
	// never-reset session clock and would otherwise keep inflating the
	// denominator (shrinking these rates toward zero) forever after Stop.
	const double elapsedSeconds = effectiveElapsedMs / 1000.0;
	if (diag.currentSamples > 0 && elapsedSeconds > 0.0 && diag.width > 0 && diag.height > 0)
	{
		const double totalSamples = static_cast<double>(diag.width) * diag.height * diag.currentSamples;
		const double samplesPerSec = totalSamples / elapsedSeconds;
		ui->labelDiagSamplesPerSecValue->setText(tr("%1 M").arg(samplesPerSec / 1.0e6, 0, 'f', 1));

		// Rough estimate, not a true traced-ray counter: assumes one primary/
		// continuation ray plus one shadow ray per bounce, up to this
		// session's configured max bounce depth - good enough for a relative
		// throughput indicator, not a precise ray count.
		const double raysPerSample = 1.0 + 2.0 * std::max(1, viewport->pathTracingMaxBounces());
		const double mraysPerSec = samplesPerSec * raysPerSample / 1.0e6;
		ui->labelDiagMRaysPerSecValue->setText(tr("~%1").arg(mraysPerSec, 0, 'f', 0));
	}
	else
	{
		ui->labelDiagSamplesPerSecValue->setText(tr("N/A"));
		ui->labelDiagMRaysPerSecValue->setText(tr("N/A"));
	}

	ui->labelDiagRenderTimeValue->setText(effectiveElapsedMs > 0 ? tr("%1 s").arg(elapsedSeconds, 0, 'f', 1) : tr("N/A"));
}

void PathTracingDialog::updateButtonsForState()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	if (!viewport)
		return;

	uint32_t currentSamples = 0, targetSamples = 0;
	bool running = false;
	viewport->pathTracingProgress(currentSamples, targetSamples, running);

	// Export is also valid with zero converged viewport samples when the
	// requested export resolution exceeds the viewport - onExportClicked()'s
	// offline-render branch runs an entirely fresh, independent render at
	// that resolution and never touches the viewport's own accumulated
	// frame, so it has no business waiting on it. Mirrors
	// updateResolutionWarning()'s identical exceedsViewport computation.
	int viewportWidth = 0, viewportHeight = 0;
	viewport->pathTracingViewportResolution(viewportWidth, viewportHeight);
	const bool exceedsViewport = ui->spinBoxExportWidth->value() > viewportWidth
		|| ui->spinBoxExportHeight->value() > viewportHeight;

	ui->pushButtonRender->setEnabled(!running);
	ui->pushButtonStop->setEnabled(running);
	ui->pushButtonExport->setEnabled(!running && (currentSamples > 0 || exceedsViewport));
}
