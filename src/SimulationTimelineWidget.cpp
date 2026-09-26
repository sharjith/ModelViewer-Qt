#include "SimulationTimelineWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace
{
	constexpr int kMaxWidth = 640;
	constexpr int kMargin = 12;
	constexpr int kTopOffset = 12;
	constexpr int kLabelWidth = 190;
}

SimulationTimelineWidget::SimulationTimelineWidget(QWidget* viewport)
	: QWidget(viewport)
{
	setObjectName(QStringLiteral("simulationTimeline"));
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(QStringLiteral(
		"QWidget#simulationTimeline { background: rgba(30, 30, 30, 205); border: 1px solid rgba(255, 255, 255, 40); border-radius: 6px; }"
		"QLabel, QCheckBox { color: white; background: transparent; }"
		"QToolButton { background: transparent; border: none; padding: 3px; border-radius: 4px; }"
		"QToolButton:hover { background: rgba(255, 255, 255, 40); }"
		"QComboBox { color: white; background: rgba(70, 70, 70, 220); border: 1px solid rgba(255, 255, 255, 40); padding: 1px 6px; }"
		"QComboBox QAbstractItemView { color: white; background: rgb(50, 50, 50); selection-background-color: rgb(0, 120, 215); }"));

	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 4, 8, 4);
	layout->setSpacing(4);

	_grip = new QLabel(QStringLiteral("⋮⋮"), this); // two vertical ellipses
	_grip->setAlignment(Qt::AlignCenter);
	_grip->setFixedWidth(16);
	_grip->setStyleSheet(QStringLiteral("QLabel { color: rgba(255, 255, 255, 150); font-weight: bold; }"));
	_grip->installEventFilter(this);
	_pinButton = new QToolButton(this);
	_pinButton->setCheckable(true);

	_prevButton = new QToolButton(this);
	_prevButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
	_prevButton->setToolTip(tr("Previous step"));
	_playButton = new QToolButton(this);
	_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	_playButton->setToolTip(tr("Play"));
	_stopButton = new QToolButton(this);
	_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
	_stopButton->setToolTip(tr("Stop and rewind to the first step"));
	_nextButton = new QToolButton(this);
	_nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
	_nextButton->setToolTip(tr("Next step"));

	_slider = new QSlider(Qt::Horizontal, this);
	_slider->setMinimumWidth(120);
	_slider->setTracking(true);

	_label = new QLabel(this);
	_label->setMinimumWidth(kLabelWidth);
	_label->setMaximumWidth(kLabelWidth);

	_loopCheck = new QCheckBox(tr("Loop"), this);
	_loopCheck->setChecked(true);
	_speedCombo = new QComboBox(this);
	for (double speed : { 0.5, 1.0, 2.0, 4.0 })
		_speedCombo->addItem(QStringLiteral("%1x").arg(speed), speed);
	_speedCombo->setCurrentIndex(1);
	_speedCombo->setToolTip(tr("Playback speed"));

	layout->addWidget(_grip);
	layout->addWidget(_prevButton);
	layout->addWidget(_playButton);
	layout->addWidget(_stopButton);
	layout->addWidget(_nextButton);
	layout->addWidget(_slider, 1);
	layout->addWidget(_label);
	layout->addWidget(_loopCheck);
	layout->addWidget(_speedCombo);
	layout->addWidget(_pinButton);

	connect(_prevButton, &QToolButton::clicked, this, [this]() { emit stepRequested(std::max(0, _slider->value() - 1)); });
	connect(_nextButton, &QToolButton::clicked, this, [this]() { emit stepRequested(std::min(_count - 1, _slider->value() + 1)); });
	connect(_playButton, &QToolButton::clicked, this, [this]() { emit playRequested(!_playing); });
	connect(_stopButton, &QToolButton::clicked, this, [this]() {
		emit playRequested(false); // stop the playback first, then go back to the start
		emit stepRequested(0);
	});
	connect(_slider, &QSlider::valueChanged, this, [this](int value) {
		updateText();
		emit stepRequested(value);
	});
	connect(_loopCheck, &QCheckBox::toggled, this, &SimulationTimelineWidget::loopChanged);
	connect(_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		emit speedChanged(_speedCombo->currentData().toDouble());
	});

	connect(_pinButton, &QToolButton::toggled, this, [this](bool pinned) {
		_pinned = pinned;
		updateGrip();
		savePlacement();
	});
	loadPlacement();
	updateGrip();

	if (viewport)
		viewport->installEventFilter(this);
	hide();
}

void SimulationTimelineWidget::setSteps(int count, const std::function<QString(int)>& describe)
{
	_count = std::max(0, count);
	_texts.clear();
	for (int i = 0; i < _count; ++i)
		_texts << (describe ? describe(i) : QString());
	const QSignalBlocker block(_slider);
	_slider->setRange(0, std::max(0, _count - 1));
	_slider->setEnabled(_count > 1);
	_stopButton->setEnabled(_count > 1);
	updateText();
	reposition();
}

void SimulationTimelineWidget::setCurrentStep(int step)
{
	const QSignalBlocker block(_slider);
	_slider->setValue(std::clamp(step, 0, std::max(0, _count - 1)));
	updateText();
}

void SimulationTimelineWidget::setPlaying(bool playing)
{
	_playing = playing;
	_playButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	_playButton->setToolTip(playing ? tr("Pause") : tr("Play"));
}

void SimulationTimelineWidget::setLoop(bool loop)
{
	const QSignalBlocker block(_loopCheck);
	_loopCheck->setChecked(loop);
}

void SimulationTimelineWidget::setSpeed(double speed)
{
	const QSignalBlocker block(_speedCombo);
	const int index = _speedCombo->findData(speed);
	if (index >= 0)
		_speedCombo->setCurrentIndex(index);
}

void SimulationTimelineWidget::updateText()
{
	const int step = _slider->value();
	QString text = tr("Step %1 / %2").arg(step + 1).arg(_count);
	if (step >= 0 && step < _texts.size() && !_texts[step].isEmpty())
		text += QStringLiteral(": ") + _texts[step];
	_label->setText(_label->fontMetrics().elidedText(text, Qt::ElideRight, kLabelWidth));
	_label->setToolTip(text);
}

void SimulationTimelineWidget::reposition()
{
	QWidget* viewport = parentWidget();
	if (!viewport)
		return;
	const int width = std::max(300, std::min(kMaxWidth, viewport->width() - 2 * kMargin));
	setFixedWidth(width);
	adjustSize();
	if (!_customPos)
	{
		move((viewport->width() - this->width()) / 2, kTopOffset);
		return;
	}
	// keep the whole control inside the viewport
	const int x = std::clamp(static_cast<int>(_fracX * viewport->width()) - this->width() / 2, 0, std::max(0, viewport->width() - this->width()));
	const int y = std::clamp(_topY, 0, std::max(0, viewport->height() - this->height()));
	move(x, y);
}

void SimulationTimelineWidget::resetPosition()
{
	_customPos = false;
	reposition();
	savePlacement();
}

void SimulationTimelineWidget::updateGrip()
{
	_grip->setCursor(_pinned ? Qt::ArrowCursor : Qt::SizeAllCursor);
	_grip->setEnabled(!_pinned);
	_grip->setToolTip(_pinned ? tr("Unpin to move the timeline") : tr("Drag to move the timeline (double-click to reset)"));
	// same icons as the navigation panel's pin: pin.png while pinned, unpin.png otherwise
	_pinButton->setIcon(QIcon(_pinned ? QStringLiteral(":/icons/res/pin.png") : QStringLiteral(":/icons/res/unpin.png")));
	_pinButton->setToolTip(_pinned ? tr("Pinned: position locked. Click to unlock.") : tr("Pin the timeline where it is"));
	{
		const QSignalBlocker block(_pinButton);
		_pinButton->setChecked(_pinned);
	}
}

void SimulationTimelineWidget::savePlacement() const
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("SimulationTimeline"));
	settings.setValue(QStringLiteral("pinned"), _pinned);
	settings.setValue(QStringLiteral("custom"), _customPos);
	settings.setValue(QStringLiteral("fracX"), _fracX);
	settings.setValue(QStringLiteral("topY"), _topY);
}

void SimulationTimelineWidget::loadPlacement()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("SimulationTimeline"));
	_pinned = settings.value(QStringLiteral("pinned"), false).toBool();
	_customPos = settings.value(QStringLiteral("custom"), false).toBool();
	_fracX = std::clamp(settings.value(QStringLiteral("fracX"), 0.5).toDouble(), 0.0, 1.0);
	_topY = std::max(0, settings.value(QStringLiteral("topY"), kTopOffset).toInt());
}

void SimulationTimelineWidget::refresh()
{
	const bool shouldShow = _count > 1 && _alive && _alive();
	if (shouldShow != isVisible())
		setVisible(shouldShow);
	if (shouldShow)
		raise();
}

bool SimulationTimelineWidget::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _grip)
	{
		auto* mouse = dynamic_cast<QMouseEvent*>(event);
		if (_pinned || !mouse)
			return false;
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
			if (mouse->button() == Qt::LeftButton)
			{
				_dragging = true;
				// Everything in the PARENT's coordinates (the position is this grip's own, mapped up), so the drag is
				// right wherever the viewport sits on the screen or the window moves.
				_dragOffset = _grip->mapTo(parentWidget(), mouse->position().toPoint()) - pos();
				return true;
			}
			break;
		case QEvent::MouseMove:
			if (_dragging && parentWidget())
			{
				QWidget* viewport = parentWidget();
				const QPoint target = _grip->mapTo(viewport, mouse->position().toPoint()) - _dragOffset;
				const int x = std::clamp(target.x(), 0, std::max(0, viewport->width() - width()));
				const int y = std::clamp(target.y(), 0, std::max(0, viewport->height() - height()));
				move(x, y);
				_customPos = true;
				_fracX = viewport->width() > 0 ? (x + width() / 2.0) / viewport->width() : 0.5;
				_topY = y;
				return true;
			}
			break;
		case QEvent::MouseButtonRelease:
			if (_dragging)
			{
				_dragging = false;
				savePlacement();
				return true;
			}
			break;
		case QEvent::MouseButtonDblClick:
			resetPosition();
			return true;
		default:
			break;
		}
		return false;
	}
	if (watched == parentWidget())
	{
		switch (event->type())
		{
		case QEvent::Resize:
			reposition();
			break;
		case QEvent::MouseMove:
		case QEvent::MouseButtonRelease:
		case QEvent::Wheel:
		case QEvent::Enter:
		case QEvent::Leave:
			refresh();
			break;
		default:
			break;
		}
	}
	return false; // never consume the viewport's events
}
