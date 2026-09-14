#include "AnimationsPanel.h"

#include "ViewportWidget.h"
#include "SceneGraph.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace
{
QIcon makeCircleIcon(bool filled, const QColor& color)
{
	constexpr int size = 16;
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(color, 1.25));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(QRectF(2.0, 2.0, size - 4.0, size - 4.0));
	if (filled)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		painter.drawEllipse(QRectF(5.1, 5.1, size - 10.2, size - 10.2));
	}
	return QIcon(pixmap);
}

QString formatTime(double seconds)
{
	const int totalMs = qMax(0, static_cast<int>(seconds * 1000.0));
	const int minutes = totalMs / 60000;
	const int secs = (totalMs / 1000) % 60;
	const int centiseconds = (totalMs / 10) % 100;
	return QStringLiteral("%1:%2.%3")
		.arg(minutes)
		.arg(secs, 2, 10, QLatin1Char('0'))
		.arg(centiseconds, 2, 10, QLatin1Char('0'));
}

QString formatSpeedLabel(double speed)
{
	if (std::abs(speed - std::round(speed)) < 0.0001)
		return QStringLiteral("%1x").arg(speed, 0, 'f', 1);
	if (std::abs(speed * 10.0 - std::round(speed * 10.0)) < 0.0001)
		return QStringLiteral("%1x").arg(speed, 0, 'f', 1);
	return QStringLiteral("%1x").arg(speed, 0, 'f', 2);
}
}

AnimationsPanel::AnimationsPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(8);

	_tree = new QTreeWidget(this);
	_tree->setHeaderHidden(true);
	_tree->setColumnCount(1);
	_tree->setRootIsDecorated(true);
	_tree->setIndentation(16);
	_tree->setAlternatingRowColors(true);
	_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	rootLayout->addWidget(_tree, 1);

	auto* controlsLayout = new QVBoxLayout();
	controlsLayout->setContentsMargins(8, 0, 8, 8);
	controlsLayout->setSpacing(6);

	_loopCheck = new QCheckBox(tr("Loop"), this);
	_speedLabel = new QLabel(tr("Speed"), this);
	_speedCombo = new QComboBox(this);
	_timeLabel = new QLabel(tr("0:00.00 / 0:00.00"), this);
	_playPauseButton = new QPushButton(tr("Play"), this);
	_resetButton = new QPushButton(tr("Reset"), this);
	_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	_timeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	const QList<double> speedOptions{ 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };
	for (double speed : speedOptions)
		_speedCombo->addItem(formatSpeedLabel(speed), speed);
	_speedCombo->setCurrentIndex(_speedCombo->findData(1.0));
	_speedCombo->setToolTip(tr("Playback speed"));

	auto* infoRow = new QHBoxLayout();
	infoRow->setContentsMargins(0, 0, 0, 0);
	infoRow->setSpacing(8);
	infoRow->addWidget(_loopCheck);
	infoRow->addSpacerItem(new QSpacerItem(8, 0, QSizePolicy::Fixed, QSizePolicy::Minimum));
	infoRow->addWidget(_speedLabel);
	infoRow->addWidget(_speedCombo);
	infoRow->addStretch(1);
	infoRow->addWidget(_timeLabel, 1);
	controlsLayout->addLayout(infoRow);

	_timelineSlider = new QSlider(Qt::Horizontal, this);
	_timelineSlider->setRange(0, 1000);
	_timelineSlider->setEnabled(false);
	controlsLayout->addWidget(_timelineSlider);

	auto* transportRow = new QHBoxLayout();
	transportRow->setContentsMargins(0, 0, 0, 0);
	transportRow->setSpacing(8);
	transportRow->addWidget(_playPauseButton);
	transportRow->addStretch(1);
	transportRow->addWidget(_resetButton);
	controlsLayout->addLayout(transportRow);

	rootLayout->addLayout(controlsLayout);

	connect(_tree, &QTreeWidget::itemClicked, this, &AnimationsPanel::onItemClicked);
	connect(_tree, &QWidget::customContextMenuRequested, this, &AnimationsPanel::onTreeContextMenuRequested);
	connect(_playPauseButton, &QPushButton::clicked, this, &AnimationsPanel::onPlayPauseClicked);
	connect(_resetButton, &QPushButton::clicked, this, &AnimationsPanel::onResetClicked);
	connect(_loopCheck, &QCheckBox::toggled, this, &AnimationsPanel::onLoopCheckChanged);
	connect(_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AnimationsPanel::onPlaybackSpeedChanged);
	connect(_timelineSlider, &QSlider::sliderPressed, this, &AnimationsPanel::onSliderPressed);
	connect(_timelineSlider, &QSlider::sliderReleased, this, &AnimationsPanel::onSliderReleased);
	connect(_timelineSlider, &QSlider::valueChanged, this, &AnimationsPanel::onSliderValueChanged);
}

void AnimationsPanel::setSceneGraph(SceneGraph* sg)
{
	_sceneGraph = sg;
}

void AnimationsPanel::setViewportWidget(ViewportWidget* viewportWidget)
{
	_viewportWidget = viewportWidget;
}

void AnimationsPanel::refresh()
{
	_tree->clear();
	_currentDurationSeconds = 0.0;

	if (!_sceneGraph)
	{
		updateControlsForSelection();
		return;
	}

	const QString activeFile = _viewportWidget ? _viewportWidget->activeAnimationFile() : QString();
	const int activeClip = _viewportWidget ? _viewportWidget->activeAnimationClip() : -1;

	const QStringList files = _sceneGraph->filesWithAnimations();
	for (const QString& sourceFile : files)
	{
		const GltfAnimationData data = _sceneGraph->animationDataForFile(sourceFile);
		QTreeWidgetItem* fileItem = makeFileItem(sourceFile, QFileInfo(sourceFile).fileName());
		_tree->addTopLevelItem(fileItem);

		const int sceneGraphActiveClip = _sceneGraph->activeAnimationClipForFile(sourceFile);
		const int effectiveActiveClip = (sourceFile == activeFile && activeClip >= 0) ? activeClip : sceneGraphActiveClip;

		for (int clipIndex = 0; clipIndex < data.clips.size(); ++clipIndex)
		{
			const GltfAnimationClip& clip = data.clips[clipIndex];
			QString label = clip.name.isEmpty() ? tr("Clip %1").arg(clipIndex + 1) : clip.name;
			fileItem->addChild(makeClipItem(label, clipIndex, clip.durationSeconds, effectiveActiveClip == clipIndex));
		}

		fileItem->setExpanded(true);
	}

	restoreSelection();
	updateControlsForSelection();
}

void AnimationsPanel::setDetachedOverlayMode(bool enabled)
{
	if (_overlayMode == enabled)
		return;

	if (enabled)
	{
		_savedStyleSheet = styleSheet();
		_savedPlayPauseStyle = _playPauseButton ? _playPauseButton->styleSheet() : QString();
		_savedResetStyle = _resetButton ? _resetButton->styleSheet() : QString();
		_savedPalette = _tree->palette();
		_savedViewportPalette = _tree->viewport()->palette();
		_savedAutoFill = autoFillBackground();
		_savedViewportAutoFill = _tree->viewport()->autoFillBackground();

		QPalette palette = _savedPalette;
		QColor base = palette.color(QPalette::Base);
		QColor alternate = palette.color(QPalette::AlternateBase);
		base.setAlpha(0);
		alternate.setAlpha(0);
		palette.setColor(QPalette::Base, base);
		palette.setColor(QPalette::AlternateBase, alternate);

		_tree->setPalette(palette);
		_tree->viewport()->setPalette(palette);
		_tree->setAutoFillBackground(false);
		_tree->viewport()->setAutoFillBackground(false);
		_tree->setAttribute(Qt::WA_NoSystemBackground, true);
		_tree->viewport()->setAttribute(Qt::WA_NoSystemBackground, true);
		_tree->viewport()->setAttribute(Qt::WA_StyledBackground, false);
		setAutoFillBackground(false);
		setAttribute(Qt::WA_NoSystemBackground, true);
		setProperty("detachedOverlayMode", true);
		_tree->setProperty("detachedOverlayMode", true);
		_tree->viewport()->setProperty("detachedOverlayMode", true);
		setStyleSheet(QStringLiteral(
			"QPushButton, QCheckBox, QLabel, QSlider { background: transparent; }"
		));
		updateDetachedPlayButtonStyle();
	}
	else
	{
		setStyleSheet(_savedStyleSheet);
		if (_playPauseButton)
			_playPauseButton->setStyleSheet(_savedPlayPauseStyle);
		if (_resetButton)
			_resetButton->setStyleSheet(_savedResetStyle);
		_tree->setPalette(_savedPalette);
		_tree->viewport()->setPalette(_savedViewportPalette);
		_tree->setAutoFillBackground(_savedAutoFill);
		_tree->viewport()->setAutoFillBackground(_savedViewportAutoFill);
		_tree->setAttribute(Qt::WA_NoSystemBackground, false);
		_tree->viewport()->setAttribute(Qt::WA_NoSystemBackground, false);
		_tree->viewport()->setAttribute(Qt::WA_StyledBackground, false);
		setAttribute(Qt::WA_NoSystemBackground, false);
		setProperty("detachedOverlayMode", false);
		_tree->setProperty("detachedOverlayMode", false);
		_tree->viewport()->setProperty("detachedOverlayMode", false);
		setAutoFillBackground(_savedAutoFill);
	}

	_overlayMode = enabled;
	refreshDetachedOverlayTheme();
	_tree->viewport()->update();
	_tree->update();
	update();
}

void AnimationsPanel::refreshDetachedOverlayTheme()
{
	if (!_overlayMode || !_tree)
		return;

	const bool lightText = property("overlayViewerLightText").toBool();
	const QColor textColor = lightText ? QColor(255, 255, 255) : QColor(0, 0, 0);
	_detachedOverlayFillColor = lightText ? QColor(255, 255, 255, 65) : QColor(0, 0, 0, 45);

	QPalette treePalette = _tree->palette();
	treePalette.setColor(QPalette::Text, textColor);
	treePalette.setColor(QPalette::WindowText, textColor);
	treePalette.setColor(QPalette::ButtonText, textColor);
	treePalette.setColor(QPalette::HighlightedText, textColor);
	_tree->setPalette(treePalette);

	QPalette viewportPalette = _tree->viewport()->palette();
	viewportPalette.setColor(QPalette::Text, textColor);
	viewportPalette.setColor(QPalette::WindowText, textColor);
	viewportPalette.setColor(QPalette::ButtonText, textColor);
	viewportPalette.setColor(QPalette::HighlightedText, textColor);
	_tree->viewport()->setPalette(viewportPalette);

	updateDetachedPlayButtonStyle();
	if (_sceneGraph)
		refresh();
	else
		updateControlsForSelection();
}

void AnimationsPanel::paintEvent(QPaintEvent* event)
{
	if (_overlayMode)
	{
		QPainter painter(this);
		painter.setCompositionMode(QPainter::CompositionMode_Source);
		painter.fillRect(event->rect(), _detachedOverlayFillColor);
	}

	QWidget::paintEvent(event);
}

void AnimationsPanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
	if (!item || item->data(0, IsFileItemRole).toBool())
		return;

	QTreeWidgetItem* parentItem = item->parent();
	if (!parentItem)
		return;

	const QString sourceFile = parentItem->data(0, SourceFileRole).toString();
	const int clipIndex = item->data(0, ClipIndexRole).toInt();
	if (sourceFile.isEmpty() || clipIndex < 0)
		return;

	_selectedSourceFile = sourceFile;
	_selectedClipIndex = clipIndex;
	markActiveClip(sourceFile, clipIndex);
	updateControlsForSelection();
	emit clipActivated(sourceFile, clipIndex);
}

void AnimationsPanel::onPlayPauseClicked()
{
	const bool playing = _viewportWidget ? _viewportWidget->isAnimationPlaying() : false;
	emit playbackToggled(!playing);
}

void AnimationsPanel::onResetClicked()
{
	if (_currentDurationSeconds <= 0.0)
		return;

	emit playbackToggled(false);
	emit seekRequested(0.0);
}

void AnimationsPanel::onTreeContextMenuRequested(const QPoint& pos)
{
	if (!_tree)
		return;

	QTreeWidgetItem* item = _tree->itemAt(pos);
	if (!item)
		return;

	const bool isFileItem = item->data(0, IsFileItemRole).toBool();
	QString sourceFile;
	int clipIndex = -1;
	if (isFileItem)
	{
		sourceFile = item->data(0, SourceFileRole).toString();
	}
	else
	{
		QTreeWidgetItem* parentItem = item->parent();
		if (!parentItem)
			return;
		sourceFile = parentItem->data(0, SourceFileRole).toString();
		clipIndex = item->data(0, ClipIndexRole).toInt();
	}

	if (sourceFile.isEmpty())
		return;

	QMenu menu(this);
	QAction* deleteAction = menu.addAction(QIcon(":/icons/res/delete.png"), isFileItem ? tr("Delete All") : tr("Delete"));
	const bool playing = _viewportWidget ? _viewportWidget->isAnimationPlaying() : false;
	deleteAction->setEnabled(!playing);
	QAction* chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
	if (chosen == deleteAction)
		emit clipDeleteRequested(sourceFile, clipIndex);
}

void AnimationsPanel::onLoopCheckChanged(bool checked)
{
	if (_syncingControls)
		return;

	emit loopToggled(checked);
}

void AnimationsPanel::onPlaybackSpeedChanged(int index)
{
	if (_syncingControls || !_speedCombo || index < 0)
		return;

	emit playbackSpeedChanged(_speedCombo->itemData(index).toDouble());
}

void AnimationsPanel::onSliderPressed()
{
	_scrubbing = true;
}

void AnimationsPanel::onSliderReleased()
{
	_scrubbing = false;
	onSliderValueChanged(_timelineSlider->value());
}

void AnimationsPanel::onSliderValueChanged(int value)
{
	if (_syncingControls || _currentDurationSeconds <= 0.0)
		return;

	const double timeSeconds = (static_cast<double>(value) / static_cast<double>(_timelineSlider->maximum())) * _currentDurationSeconds;
	_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(timeSeconds), formatTime(_currentDurationSeconds)));

	emit seekRequested(timeSeconds);
}

QTreeWidgetItem* AnimationsPanel::makeFileItem(const QString& sourceFile, const QString& displayName) const
{
	auto* item = new QTreeWidgetItem();
	item->setText(0, displayName);
	item->setToolTip(0, sourceFile);
	item->setData(0, SourceFileRole, sourceFile);
	item->setData(0, IsFileItemRole, true);
	item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
	QFont font = item->font(0);
	font.setBold(true);
	item->setFont(0, font);
	return item;
}

QTreeWidgetItem* AnimationsPanel::makeClipItem(const QString& label, int clipIndex, double durationSeconds, bool active) const
{
	auto* item = new QTreeWidgetItem();
	item->setText(0, QStringLiteral("%1 (%2)").arg(label, formatTime(durationSeconds)));
	item->setData(0, ClipIndexRole, clipIndex);
	item->setData(0, DurationRole, durationSeconds);
	item->setData(0, IsFileItemRole, false);
	item->setIcon(0, active ? activeIcon() : inactiveIcon());
	return item;
}

void AnimationsPanel::markActiveClip(const QString& sourceFile, int clipIndex)
{
	for (int topIndex = 0; topIndex < _tree->topLevelItemCount(); ++topIndex)
	{
		QTreeWidgetItem* fileItem = _tree->topLevelItem(topIndex);
		if (fileItem->data(0, SourceFileRole).toString() != sourceFile)
			continue;

		for (int childIndex = 0; childIndex < fileItem->childCount(); ++childIndex)
		{
			QTreeWidgetItem* child = fileItem->child(childIndex);
			const bool isActive = child->data(0, ClipIndexRole).toInt() == clipIndex;
			child->setIcon(0, isActive ? activeIcon() : inactiveIcon());
		}
		break;
	}
}

void AnimationsPanel::updateControlsForSelection()
{
	_syncingControls = true;

	QString activeFile = _viewportWidget ? _viewportWidget->activeAnimationFile() : QString();
	int activeClip = _viewportWidget ? _viewportWidget->activeAnimationClip() : -1;
	if (activeFile.isEmpty() && _sceneGraph)
	{
		const QStringList files = _sceneGraph->filesWithAnimations();
		if (!files.isEmpty())
		{
			activeFile = files.front();
			activeClip = _sceneGraph->activeAnimationClipForFile(activeFile);
		}
	}

	double currentTime = _viewportWidget ? _viewportWidget->currentAnimationTimeSeconds() : 0.0;
	const bool playing = _viewportWidget ? _viewportWidget->isAnimationPlaying() : false;
	const bool looping = _viewportWidget ? _viewportWidget->isAnimationLooping() : false;
	const double speed = _viewportWidget ? _viewportWidget->animationPlaybackSpeed() : 1.0;

	_currentDurationSeconds = 0.0;
	if (_sceneGraph && !activeFile.isEmpty())
	{
		const GltfAnimationData data = _sceneGraph->animationDataForFile(activeFile);
		if (activeClip >= 0 && activeClip < data.clips.size())
			_currentDurationSeconds = data.clips[activeClip].durationSeconds;
	}

	_playPauseButton->setEnabled(_currentDurationSeconds > 0.0);
	_playPauseButton->setText(playing ? tr("Pause") : tr("Play"));
	_resetButton->setEnabled(_currentDurationSeconds > 0.0 && (playing || currentTime > 0.0));
	_loopCheck->setEnabled(_currentDurationSeconds > 0.0);
	_loopCheck->setChecked(looping);
	_speedCombo->setEnabled(_currentDurationSeconds > 0.0);
	{
		QSignalBlocker blocker(_speedCombo);
		int speedIndex = _speedCombo->findData(speed);
		if (speedIndex < 0)
			speedIndex = _speedCombo->findData(1.0);
		_speedCombo->setCurrentIndex(speedIndex);
	}
	_timelineSlider->setEnabled(_currentDurationSeconds > 0.0);

	if (!_scrubbing)
	{
		const int sliderValue = (_currentDurationSeconds > 0.0)
			? qRound((currentTime / _currentDurationSeconds) * _timelineSlider->maximum())
			: 0;
		QSignalBlocker blocker(_timelineSlider);
		_timelineSlider->setValue(qBound(0, sliderValue, _timelineSlider->maximum()));
	}

	_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(currentTime), formatTime(_currentDurationSeconds)));
	_syncingControls = false;
}

void AnimationsPanel::restoreSelection()
{
	if (_selectedSourceFile.isEmpty() || _selectedClipIndex < 0 || !_tree)
		return;

	for (int topIndex = 0; topIndex < _tree->topLevelItemCount(); ++topIndex)
	{
		QTreeWidgetItem* fileItem = _tree->topLevelItem(topIndex);
		if (!fileItem || fileItem->data(0, SourceFileRole).toString() != _selectedSourceFile)
			continue;

		for (int childIndex = 0; childIndex < fileItem->childCount(); ++childIndex)
		{
			QTreeWidgetItem* child = fileItem->child(childIndex);
			if (child && child->data(0, ClipIndexRole).toInt() == _selectedClipIndex)
			{
				_tree->setCurrentItem(child);
				return;
			}
		}
	}

	_selectedSourceFile.clear();
	_selectedClipIndex = -1;
}

QIcon AnimationsPanel::activeIcon() const
{
	const QColor color = _tree ? _tree->palette().color(QPalette::Text)
	                           : palette().color(QPalette::Text);
	return makeCircleIcon(true, color);
}

QIcon AnimationsPanel::inactiveIcon() const
{
	QColor color = _tree ? _tree->palette().color(QPalette::Text)
	                     : palette().color(QPalette::Text);
	color.setAlpha(160);
	return makeCircleIcon(false, color);
}

void AnimationsPanel::updateDetachedPlayButtonStyle()
{
	if (!_playPauseButton && !_resetButton)
		return;

	if (!_overlayMode)
	{
		if (_playPauseButton)
			_playPauseButton->setStyleSheet(_savedPlayPauseStyle);
		if (_resetButton)
			_resetButton->setStyleSheet(_savedResetStyle);
		return;
	}

	const bool lightText = property("overlayPanelLightText").toBool()
		|| palette().color(QPalette::Text).lightnessF() > 0.5;
	const QColor buttonText = lightText ? QColor(255, 255, 255) : QColor(0, 0, 0);
	const QColor buttonFill = lightText ? QColor(24, 24, 24, 210) : QColor(255, 255, 255, 215);
	const QColor buttonBorder = lightText ? QColor(255, 255, 255, 110) : QColor(0, 0, 0, 90);
	const QColor buttonDisabledText = lightText ? QColor(255, 255, 255, 120) : QColor(0, 0, 0, 120);

	const QString buttonStyle = QStringLiteral(
		"QPushButton {"
		"  background-color: rgba(%1, %2, %3, %4);"
		"  color: rgb(%5, %6, %7);"
		"  border: 1px solid rgba(%8, %9, %10, %11);"
		"  border-radius: 4px;"
		"  padding: 3px 10px;"
		"}"
		"QPushButton:disabled {"
		"  color: rgba(%12, %13, %14, %15);"
		"}"
	)
		.arg(buttonFill.red()).arg(buttonFill.green()).arg(buttonFill.blue()).arg(buttonFill.alpha())
		.arg(buttonText.red()).arg(buttonText.green()).arg(buttonText.blue())
		.arg(buttonBorder.red()).arg(buttonBorder.green()).arg(buttonBorder.blue()).arg(buttonBorder.alpha())
		.arg(buttonDisabledText.red()).arg(buttonDisabledText.green()).arg(buttonDisabledText.blue()).arg(buttonDisabledText.alpha());

	if (_playPauseButton)
		_playPauseButton->setStyleSheet(buttonStyle);
	if (_resetButton)
		_resetButton->setStyleSheet(buttonStyle);
}
