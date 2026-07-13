#include "PathTracingDialog.h"
#include "ui_PathTracingDialog.h"

#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "RtTonemap.h"

#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QCloseEvent>
#include <QSettings>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QApplication>
#include <QEventLoop>
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

	// The OptiX item is a placeholder for a future NVIDIA OptiX denoiser
	// backend (distinct from the OIDN CPU/GPU-CUDA options above it, which
	// are both already implemented) - reserving its place in the dropdown
	// now, but disabled, so it can't actually be selected (and silently
	// cast to an enum value nothing implements yet) until that backend
	// exists. DenoiserDevicePreference doesn't have a 4th enumerator to
	// match, so leave this index unreachable via the combo itself.
	if (auto* model = qobject_cast<QStandardItemModel*>(ui->comboBoxDenoiserDevice->model()))
	{
		constexpr int kOptixPlaceholderIndex = 3;
		if (QStandardItem* item = model->item(kOptixPlaceholderIndex))
		{
			item->setEnabled(false);
			item->setToolTip(tr("Not yet implemented - reserved for a future NVIDIA OptiX denoiser backend."));
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
		ui->checkBoxEnvImportanceSampling->setChecked(viewport->pathTracingEnvImportanceSamplingEnabled());
		ui->comboBoxDenoiserDevice->setCurrentIndex(static_cast<int>(viewport->pathTracingDenoiserDevicePreference()));
		ui->comboBoxRenderEngine->setCurrentIndex(static_cast<int>(viewport->pathTracingEnginePreference()));

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
	connect(ui->checkBoxEnvImportanceSampling, &QCheckBox::toggled, this, &PathTracingDialog::onEnvImportanceSamplingToggled);
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
		viewport->requestPathTracedRenderNow(); // start immediately, don't wait for the idle-settle countdown - also (re)starts ViewportWidget's own elapsed-time clock
	_stoppedByUser = false;
	_frozenElapsedMs = -1;
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
			   "while and will block the application until it finishes.\n\nContinue?")
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
	if (_modelViewer && !_modelViewer->currentFile().isEmpty())
		docName = QFileInfo(_modelViewer->currentFile()).completeBaseName();
	const QString defaultName = QString("%1_%2spp_%3x%4")
		.arg(docName).arg(sampleCountForFilename).arg(targetWidth).arg(targetHeight);

	QString selectedFilter;
	const QString path = QFileDialog::getSaveFileName(this, tr("Export Path-Traced Image"), defaultName,
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
		// user explicitly confirmed that tradeoff above. Disable this
		// dialog's own controls and stop the interactive progress poll for
		// the duration, since QApplication::processEvents() below would
		// otherwise let onProgressTimer() fire mid-loop and clobber the
		// offline-progress display with the (unrelated, possibly stale)
		// interactive session's own state, or let a stray click re-enter
		// one of these slots.
		_progressTimer->stop();
		setEnabled(false);
		QApplication::setOverrideCursor(Qt::WaitCursor);

		std::vector<glm::vec3> linearRgb;
		const bool renderedOk = viewport->renderPathTracedOffline(targetWidth, targetHeight,
			[this](uint32_t currentSample, uint32_t maxSamples)
			{
				ui->progressBarSamples->setMaximum(static_cast<int>(std::max<uint32_t>(maxSamples, 1)));
				ui->progressBarSamples->setValue(static_cast<int>(currentSample));
				ui->labelStatus->setText(tr("Offline rendering... %1 / %2 samples").arg(currentSample).arg(maxSamples));
				QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			},
			linearRgb);

		if (!renderedOk)
		{
			QApplication::restoreOverrideCursor();
			setEnabled(true);
			_progressTimer->start();
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

		QApplication::restoreOverrideCursor();
		setEnabled(true);
		_progressTimer->start();
	}
	else if (exportExr)
	{
		// Fast path, EXR: reuse the raw linear buffer already converged in
		// the live viewport - RtPathTracingSession::latestFrame(), read
		// BEFORE RtPresenter's tonemap/gamma pass ever touches it. Downscaled
		// if the requested resolution is smaller than what's already there
		// (never upscaled - that goes through the offline branch above).
		int liveWidth = 0, liveHeight = 0;
		std::vector<glm::vec3> linearRgb = viewport->pathTracingRawFrame(liveWidth, liveHeight);
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
		QMessageBox::warning(this, tr("Export Failed"), tr("Could not write the image file. See the log for details."));
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
	ui->comboBoxDenoiserDevice->setCurrentIndex(static_cast<int>(DenoiserDevicePreference::Auto));
	ui->comboBoxRenderEngine->setCurrentIndex(static_cast<int>(RtPathTracingEnginePreference::CPU));
	// Each setValue()/setChecked() above already emitted its usual
	// valueChanged/toggled signal (unchanged from any other edit), pushing
	// the reset value into the viewport via this dialog's existing slots -
	// no separate push-to-viewport step needed here.

	onMatchViewportClicked(); // export resolution has no fixed "default" - viewport size is the sane reset target
}

void PathTracingDialog::onExportResolutionChanged()
{
	updateResolutionWarning();
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
	// ViewportWidget's own session clock) while running, freezes at the last
	// read once the session stops being active rather than continuing to
	// advance after the render is actually done. Hidden entirely before the
	// first render this dialog has ever observed (running was never true and
	// nothing is frozen yet).
	if (running || _frozenElapsedMs >= 0)
	{
		qint64 elapsedMs;
		if (running)
		{
			_frozenElapsedMs = -1;
			elapsedMs = viewport->pathTracingElapsedMs();
		}
		else
		{
			if (_frozenElapsedMs < 0)
				_frozenElapsedMs = viewport->pathTracingElapsedMs();
			elapsedMs = _frozenElapsedMs;
		}

		const qint64 totalSeconds = elapsedMs / 1000;
		ui->labelElapsedTime->setText(tr("Elapsed: %1:%2")
			.arg(totalSeconds / 60, 2, 10, QChar('0'))
			.arg(totalSeconds % 60, 2, 10, QChar('0')));
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

	// Re-checked every poll (not just on spinbox edits) since resizing the
	// viewport window itself can flip whether the currently-entered export
	// resolution now exceeds it, with no direct signal for that resize.
	updateResolutionWarning();

	updateButtonsForState(running, current);
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

void PathTracingDialog::updateButtonsForState(bool running, uint32_t currentSamples)
{
	ui->pushButtonRender->setEnabled(!running);
	ui->pushButtonStop->setEnabled(running);
	ui->pushButtonExport->setEnabled(!running && currentSamples > 0);
}
