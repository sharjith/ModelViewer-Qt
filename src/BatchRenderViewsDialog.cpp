#include "BatchRenderViewsDialog.h"
#include "ModelViewer.h"
#include "ViewportWidget.h"
#include "SceneGraph.h"
#include "RtImageExport.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMessageBox>
#include <QApplication>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace
{
	// Same small, self-contained resolution-preset shape RtRenderDialog.cpp
	// keeps local to itself - duplicated here rather than shared, matching
	// this codebase's own convention of re-declaring small stable constants
	// per file (see e.g. the vec3ToJson lambdas ModelViewer.cpp redefines in
	// each of its own save blocks).
	struct ResolutionPreset { const char* label; int width; int height; };
	const ResolutionPreset kResolutionPresets[] = {
		{ "Custom", 0, 0 },
		{ "HD (1280 x 720)", 1280, 720 },
		{ "Full HD (1920 x 1080)", 1920, 1080 },
		{ "QHD (2560 x 1440)", 2560, 1440 },
		{ "4K UHD (3840 x 2160)", 3840, 2160 },
		{ "8K UHD (7680 x 4320)", 7680, 4320 },
	};

	// Replaces anything outside [A-Za-z0-9 _-] with '_' - a captured view's
	// name is free-text (user-typed at capture time), so it can contain
	// characters that are illegal or awkward in a filename.
	QString sanitizeFileName(const QString& name)
	{
		QString result;
		result.reserve(name.size());
		for (const QChar& c : name)
			result.append((c.isLetterOrNumber() || c == QChar('_') || c == QChar('-') || c == QChar(' ')) ? c : QChar('_'));
		return result.trimmed();
	}
}

BatchRenderViewsDialog::BatchRenderViewsDialog(ModelViewer* modelViewer, QWidget* parent)
	: QDialog(parent)
	, _modelViewer(modelViewer)
{
	setWindowTitle(tr("Batch Render Views"));
	resize(460, 560);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(12);

	auto* introLabel = new QLabel(tr("Renders every checked captured view as a high-quality offline "
	                                  "path-traced image, one file per view."), this);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);

	SceneGraph* sceneGraph = _modelViewer ? _modelViewer->sceneGraph() : nullptr;
	const QVector<GltfCameraEntry> capturedViews = sceneGraph
		? sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras
		: QVector<GltfCameraEntry>();

	auto* viewsGroup = new QGroupBox(tr("Captured Views"), this);
	auto* viewsLayout = new QVBoxLayout(viewsGroup);

	// Same text ReportExportDialog already uses for the identical empty
	// state, for consistency between the two "act on captured views" tools.
	_noViewsLabel = new QLabel(tr("No captured views yet - use the Cameras tab's \"Capture View\" first."), viewsGroup);
	_noViewsLabel->setWordWrap(true);
	viewsLayout->addWidget(_noViewsLabel);

	_viewsList = new QListWidget(viewsGroup);
	for (const GltfCameraEntry& entry : capturedViews)
	{
		QListWidgetItem* item = new QListWidgetItem(entry.name, _viewsList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
	}
	viewsLayout->addWidget(_viewsList, 1);

	_noViewsLabel->setVisible(capturedViews.isEmpty());
	_viewsList->setVisible(!capturedViews.isEmpty());

	layout->addWidget(viewsGroup, 1);

	auto* settingsGroup = new QGroupBox(tr("Render Settings"), this);
	auto* settingsLayout = new QVBoxLayout(settingsGroup);

	auto* resRow = new QHBoxLayout();
	resRow->addWidget(new QLabel(tr("Resolution:"), settingsGroup));
	_resolutionPresetCombo = new QComboBox(settingsGroup);
	resRow->addWidget(_resolutionPresetCombo, 1);
	settingsLayout->addLayout(resRow);

	auto* dimsRow = new QHBoxLayout();
	dimsRow->addWidget(new QLabel(tr("Width:"), settingsGroup));
	_widthSpin = new QSpinBox(settingsGroup);
	_widthSpin->setRange(16, 16384);
	_widthSpin->setValue(1920);
	dimsRow->addWidget(_widthSpin);
	dimsRow->addWidget(new QLabel(tr("Height:"), settingsGroup));
	_heightSpin = new QSpinBox(settingsGroup);
	_heightSpin->setRange(16, 16384);
	_heightSpin->setValue(1080);
	dimsRow->addWidget(_heightSpin);
	settingsLayout->addLayout(dimsRow);

	populateResolutionPresets();
	syncResolutionPresetFromSpinboxes();

	auto* formatRow = new QHBoxLayout();
	formatRow->addWidget(new QLabel(tr("Format:"), settingsGroup));
	_formatCombo = new QComboBox(settingsGroup);
	_formatCombo->addItem(tr("PNG"), QStringLiteral("*.png"));
	_formatCombo->addItem(tr("JPEG"), QStringLiteral("*.jpg"));
	_formatCombo->addItem(tr("BMP"), QStringLiteral("*.bmp"));
	_formatCombo->addItem(tr("TIFF"), QStringLiteral("*.tif"));
	_formatCombo->addItem(tr("OpenEXR"), QStringLiteral("*.exr"));
	formatRow->addWidget(_formatCombo, 1);
	settingsLayout->addLayout(formatRow);

	auto* folderRow = new QHBoxLayout();
	folderRow->addWidget(new QLabel(tr("Output Folder:"), settingsGroup));
	_outputFolderEdit = new QLineEdit(settingsGroup);
	// Prefer the currently loaded model's own folder (same default-location
	// reasoning RtRenderDialog::onExportClicked() uses for its own save
	// dialog), falling back to the user's Documents folder.
	QString defaultFolder;
	if (_modelViewer && !_modelViewer->currentFile().isEmpty())
		defaultFolder = QFileInfo(_modelViewer->currentFile()).absolutePath();
	if (defaultFolder.isEmpty())
		defaultFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	_outputFolderEdit->setText(defaultFolder);
	folderRow->addWidget(_outputFolderEdit, 1);
	_browseFolderButton = new QPushButton(tr("Browse..."), settingsGroup);
	folderRow->addWidget(_browseFolderButton);
	settingsLayout->addLayout(folderRow);

	layout->addWidget(settingsGroup);

	_overallProgressBar = new QProgressBar(this);
	_overallProgressBar->setFormat(tr("Overall: %v / %m views"));
	layout->addWidget(_overallProgressBar);

	_currentViewProgressBar = new QProgressBar(this);
	_currentViewProgressBar->setFormat(tr("Current view: %p%"));
	layout->addWidget(_currentViewProgressBar);

	_statusLabel = new QLabel(this);
	_statusLabel->setWordWrap(true);
	layout->addWidget(_statusLabel);

	auto* buttonRow = new QHBoxLayout();
	buttonRow->addStretch();
	_startButton = new QPushButton(tr("Start"), this);
	_startButton->setEnabled(!capturedViews.isEmpty());
	_cancelButton = new QPushButton(tr("Cancel"), this);
	_cancelButton->setEnabled(false);
	buttonRow->addWidget(_startButton);
	buttonRow->addWidget(_cancelButton);
	layout->addLayout(buttonRow);

	connect(_resolutionPresetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
		this, &BatchRenderViewsDialog::onResolutionPresetSelected);
	connect(_widthSpin, qOverload<int>(&QSpinBox::valueChanged), this, &BatchRenderViewsDialog::onExportResolutionChanged);
	connect(_heightSpin, qOverload<int>(&QSpinBox::valueChanged), this, &BatchRenderViewsDialog::onExportResolutionChanged);
	connect(_browseFolderButton, &QPushButton::clicked, this, &BatchRenderViewsDialog::onBrowseFolderClicked);
	connect(_startButton, &QPushButton::clicked, this, &BatchRenderViewsDialog::onStartClicked);
	connect(_cancelButton, &QPushButton::clicked, this, &BatchRenderViewsDialog::onCancelClicked);
}

void BatchRenderViewsDialog::reject()
{
	if (_renderInProgress)
	{
		onCancelClicked();
		return;
	}
	QDialog::reject();
}

void BatchRenderViewsDialog::populateResolutionPresets()
{
	_resolutionPresetCombo->clear();
	for (const ResolutionPreset& preset : kResolutionPresets)
		_resolutionPresetCombo->addItem(QString::fromLatin1(preset.label));
}

void BatchRenderViewsDialog::onResolutionPresetSelected(int index)
{
	if (index <= 0 || index >= static_cast<int>(std::size(kResolutionPresets)))
		return; // "Custom" (index 0), or invalid - nothing to apply

	_updatingResolutionFromPreset = true;
	_widthSpin->setValue(kResolutionPresets[index].width);
	_heightSpin->setValue(kResolutionPresets[index].height);
	_updatingResolutionFromPreset = false;
}

void BatchRenderViewsDialog::onExportResolutionChanged()
{
	if (_updatingResolutionFromPreset)
		return;
	syncResolutionPresetFromSpinboxes();
}

void BatchRenderViewsDialog::syncResolutionPresetFromSpinboxes()
{
	const int w = _widthSpin->value();
	const int h = _heightSpin->value();

	int matchIndex = 0; // "Custom"
	for (int i = 1; i < static_cast<int>(std::size(kResolutionPresets)); ++i)
	{
		if (kResolutionPresets[i].width == w && kResolutionPresets[i].height == h)
		{
			matchIndex = i;
			break;
		}
	}

	_resolutionPresetCombo->blockSignals(true);
	_resolutionPresetCombo->setCurrentIndex(matchIndex);
	_resolutionPresetCombo->blockSignals(false);
}

void BatchRenderViewsDialog::onBrowseFolderClicked()
{
	const QString startDir = _outputFolderEdit->text().trimmed().isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
		: _outputFolderEdit->text().trimmed();
	const QString chosen = QFileDialog::getExistingDirectory(this, tr("Output Folder"), startDir);
	if (!chosen.isEmpty())
		_outputFolderEdit->setText(chosen);
}

std::vector<BatchRenderViewsDialog::Job> BatchRenderViewsDialog::buildJobList(const QVector<GltfCameraEntry>& capturedViews) const
{
	std::vector<Job> jobs;
	const QString filter = _formatCombo->currentData().toString();
	const QString ext = filter.mid(2); // "*.png" -> "png"
	const QDir outputDir(_outputFolderEdit->text().trimmed());

	// Case-FOLDED, not the raw candidate - two views named "Front"/"front"
	// sanitize to distinct strings that a plain QSet<QString> (case-sensitive)
	// happily accepts as both unique, but they address the SAME file on the
	// default case-insensitive Windows/macOS filesystem, so the second render
	// silently overwrote the first with nothing in this dialog ever
	// indicating a collision occurred.
	QSet<QString> usedNames;
	for (int i = 0; i < _viewsList->count() && i < capturedViews.size(); ++i)
	{
		if (_viewsList->item(i)->checkState() != Qt::Checked)
			continue;

		QString baseName = sanitizeFileName(capturedViews.at(i).name);
		if (baseName.isEmpty())
			baseName = tr("View %1").arg(i + 1);

		// Resolved once, up front, in a single pre-flight pass - not mid-loop -
		// so two views that sanitize/default to the same name still each get
		// a distinct output file. Also bumps past any file already sitting in
		// the output folder from an earlier run - QFileInfo::exists() uses
		// the OS's own (case-insensitive on Windows/default macOS) filename
		// lookup, so this catches a pre-existing "front.png" the same way the
		// in-batch check catches a same-batch "Front"/"front" collision,
		// without ever silently overwriting either.
		QString candidate = baseName;
		int suffix = 2;
		while (usedNames.contains(candidate.toCaseFolded())
			|| QFileInfo::exists(outputDir.filePath(candidate + QLatin1Char('.') + ext)))
		{
			candidate = QStringLiteral("%1_%2").arg(baseName).arg(suffix);
			++suffix;
		}
		usedNames.insert(candidate.toCaseFolded());

		jobs.push_back({ i, outputDir.filePath(candidate + QLatin1Char('.') + ext) });
	}
	return jobs;
}

void BatchRenderViewsDialog::setRenderControlsEnabled(bool enabled)
{
	// Individually, not via this->setEnabled(false) - a disabled ancestor
	// makes an explicitly-enabled child inert too, and _cancelButton must
	// stay clickable for the whole duration. Same shape RtRenderDialog::
	// onExportClicked() uses for its own offline-render branch.
	_viewsList->setEnabled(enabled);
	_resolutionPresetCombo->setEnabled(enabled);
	_widthSpin->setEnabled(enabled);
	_heightSpin->setEnabled(enabled);
	_formatCombo->setEnabled(enabled);
	_outputFolderEdit->setEnabled(enabled);
	_browseFolderButton->setEnabled(enabled);
	_startButton->setEnabled(enabled);
	_cancelButton->setEnabled(!enabled);
}

void BatchRenderViewsDialog::onCancelClicked()
{
	_cancelRequested = true;
	if (_modelViewer && _modelViewer->getViewportWidget())
		_modelViewer->getViewportWidget()->cancelRayTracedOfflineRender();
}

void BatchRenderViewsDialog::onStartClicked()
{
	ViewportWidget* viewport = _modelViewer ? _modelViewer->getViewportWidget() : nullptr;
	SceneGraph* sceneGraph = _modelViewer ? _modelViewer->sceneGraph() : nullptr;
	if (!viewport || !sceneGraph)
		return;

	if (_outputFolderEdit->text().trimmed().isEmpty() || !QDir(_outputFolderEdit->text().trimmed()).exists())
	{
		QMessageBox::warning(this, tr("Batch Render Views"), tr("Choose an existing output folder first."));
		return;
	}

	const QVector<GltfCameraEntry> capturedViews = sceneGraph->gltfCameraDataForFile(capturedViewsSourceFileKey()).cameras;
	const std::vector<Job> jobs = buildJobList(capturedViews);
	if (jobs.empty())
	{
		QMessageBox::information(this, tr("Batch Render Views"), tr("No views are checked to render."));
		return;
	}

	const int targetWidth = _widthSpin->value();
	const int targetHeight = _heightSpin->value();
	const QString formatFilter = _formatCombo->currentData().toString();

	// Captured explicitly, separate from ViewportWidget's own "system camera"
	// backup (restored below instead of via resetToSystemCamera()) - that
	// backup is only saved the FIRST time any camera jump happens all
	// session (activateGltfCamera()/activateCameraEntry() both gate the save
	// on systemCameraStateSaved()), so if a captured view or scene state was
	// already active before this dialog opened, the backup is stale (from
	// whatever came before THAT jump), and resetToSystemCamera() would
	// silently replace the camera visible right before the batch ran with
	// that older pose instead. isGltfCameraActive() distinguishes a real
	// glTF-file camera (restored via activateGltfCamera(), which also
	// re-establishes its animation-driven association) from a free-nav/
	// captured-view/scene-state pose (activateCameraEntry() always clears
	// that association, so isGltfCameraActive() is false in both of those
	// cases - restoring the captured pose directly is correct either way).
	const bool preBatchHadGltfCamera = viewport->isGltfCameraActive();
	const QString preBatchGltfFile = viewport->activeGltfCameraFile();
	const int preBatchGltfIndex = viewport->activeGltfCameraIndex();
	const GltfCameraEntry preBatchCamera =
		viewport->captureCurrentCameraEntry(QStringLiteral("__batchRenderPreBatch__"));

	setRenderControlsEnabled(false);
	_renderInProgress = true;
	_cancelRequested = false;
	QApplication::setOverrideCursor(Qt::WaitCursor);

	_overallProgressBar->setMaximum(static_cast<int>(jobs.size()));
	_overallProgressBar->setValue(0);
	_currentViewProgressBar->setValue(0);

	QStringList failedViews;
	std::size_t completed = 0;
	for (std::size_t i = 0; i < jobs.size(); ++i)
	{
		if (_cancelRequested)
			break;

		const QString viewName = capturedViews.at(jobs[i].viewIndex).name;
		const std::size_t total = jobs.size();
		_statusLabel->setText(tr("View %1 of %2: %3").arg(i + 1).arg(total).arg(viewName));

		// Synchronous - repositions the camera and repaints immediately, no
		// extra event-loop spin needed before the render call below (same
		// as ReportExportDialog's own capture loop).
		viewport->activateGltfCamera(capturedViewsSourceFileKey(), jobs[i].viewIndex);

		std::vector<glm::vec3> linearRgb;
		bool viewCancelled = false;
		const bool renderedOk = viewport->renderRayTracedOffline(targetWidth, targetHeight,
			[this, i, total, viewName](uint32_t currentSample, uint32_t maxSamples)
			{
				_currentViewProgressBar->setMaximum(static_cast<int>(std::max<uint32_t>(maxSamples, 1)));
				_currentViewProgressBar->setValue(static_cast<int>(currentSample));
				_statusLabel->setText(tr("View %1 of %2: %3 - %4 / %5 samples")
					.arg(i + 1).arg(total).arg(viewName).arg(currentSample).arg(maxSamples));
				// Plain processEvents() - deliberately WITHOUT ExcludeUserInputEvents.
				// That flag is specifically what makes a Cancel button
				// unclickable during a blocking loop elsewhere in this app;
				// setRenderControlsEnabled(false) above already makes every
				// OTHER control inert, so letting input through here only
				// lets Cancel do anything.
				QApplication::processEvents();
			},
			linearRgb, &viewCancelled);

		if (viewCancelled || _cancelRequested)
		{
			// No file written for a cancelled view, and the outer loop
			// itself stops here too - cancelling doesn't just abort one
			// image and silently continue to the rest.
			_cancelRequested = true;
			break;
		}

		bool saveOk = false;
		if (renderedOk)
		{
			bool hdrToneMapping = true, gammaCorrection = true;
			float screenGamma = 2.2f, iblExposure = 1.0f;
			int toneMapMode = 0;
			viewport->rayTracingToneMapSettings(hdrToneMapping, gammaCorrection, screenGamma, iblExposure, toneMapMode);
			saveOk = RtImageExport::saveOfflineRender(linearRgb, targetWidth, targetHeight,
				jobs[i].path, formatFilter, hdrToneMapping, gammaCorrection, screenGamma, iblExposure, toneMapMode);
		}

		// Skip-and-continue on a single view's failure (confirmed
		// preference) - report which views failed at the end rather than
		// aborting the rest of an otherwise-long batch.
		if (!renderedOk || !saveOk)
			failedViews.append(viewName);

		++completed;
		_overallProgressBar->setValue(static_cast<int>(completed));
	}

	// Unconditional restore, whichever way the loop exited (finished,
	// cancelled, or a view failed) - mirrors ReportExportDialog's own
	// unconditional restore-after pattern for its capture loop. Restores the
	// exact camera captured before the loop started (see its capture above
	// for why that's NOT the same as resetToSystemCamera()'s own backup),
	// leaving that backup itself untouched either way.
	if (preBatchHadGltfCamera)
		viewport->activateGltfCamera(preBatchGltfFile, preBatchGltfIndex);
	else
		viewport->activateCameraEntry(preBatchCamera);

	setRenderControlsEnabled(true);
	_renderInProgress = false;
	QApplication::restoreOverrideCursor();

	if (_cancelRequested)
		_statusLabel->setText(tr("Cancelled after %1 of %2 view(s).").arg(completed).arg(jobs.size()));
	else if (!failedViews.isEmpty())
		_statusLabel->setText(tr("Done, but %1 of %2 view(s) failed: %3")
			.arg(failedViews.size()).arg(jobs.size()).arg(failedViews.join(QStringLiteral(", "))));
	else
		_statusLabel->setText(tr("Done - %1 view(s) rendered.").arg(jobs.size()));
}
