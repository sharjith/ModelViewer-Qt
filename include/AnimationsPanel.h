#pragma once

#include <QPalette>
#include <QTreeWidget>
#include <QWidget>

class ViewportWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QSlider;
class SceneGraph;

class AnimationsPanel : public QWidget
{
	Q_OBJECT

public:
	enum ItemRole
	{
		SourceFileRole = Qt::UserRole,
		ClipIndexRole = Qt::UserRole + 1,
		IsFileItemRole = Qt::UserRole + 2,
		DurationRole = Qt::UserRole + 3,
	};

	explicit AnimationsPanel(QWidget* parent = nullptr);

	void setSceneGraph(SceneGraph* sg);
	void setViewportWidget(ViewportWidget* viewportWidget);
	void refresh();
	// Styling only - does NOT reparent. Unlike SceneTreeWidget's per-document
	// instances, this class is one of MainWindow's shared/singleton panels
	// (see MainWindow.h); it must stay parented under MainWindow for its
	// whole lifetime. Nothing currently calls this method or reparents this
	// panel into a document's ViewportWidget - if that's ever wired up, note
	// that Qt would then destroy this singleton along with whichever
	// document it got reparented into, breaking every other document that
	// still expects it to exist.
	void setDetachedOverlayMode(bool enabled);
	void refreshDetachedOverlayTheme();

signals:
	void clipActivated(const QString& sourceFile, int clipIndex);
	void clipDeleteRequested(const QString& sourceFile, int clipIndex);
	void playbackToggled(bool playing);
	void loopToggled(bool enabled);
	void seekRequested(double timeSeconds);
	void playbackSpeedChanged(double speed);

private slots:
	void onItemClicked(QTreeWidgetItem* item, int column);
	void onPlayPauseClicked();
	void onResetClicked();
	void onTreeContextMenuRequested(const QPoint& pos);
	void onLoopCheckChanged(bool checked);
	void onPlaybackSpeedChanged(int index);
	void onSliderPressed();
	void onSliderReleased();
	void onSliderValueChanged(int value);

private:
	void paintEvent(QPaintEvent* event) override;

	QTreeWidgetItem* makeFileItem(const QString& sourceFile, const QString& displayName) const;
	QTreeWidgetItem* makeClipItem(const QString& label, int clipIndex, double durationSeconds, bool active) const;
	void markActiveClip(const QString& sourceFile, int clipIndex);
	void updateControlsForSelection();
	void restoreSelection();
	QIcon activeIcon() const;
	QIcon inactiveIcon() const;
	void updateDetachedPlayButtonStyle();

	QTreeWidget* _tree = nullptr;
	QPushButton* _playPauseButton = nullptr;
	QPushButton* _resetButton = nullptr;
	QCheckBox* _loopCheck = nullptr;
	QLabel* _speedLabel = nullptr;
	QComboBox* _speedCombo = nullptr;
	QSlider* _timelineSlider = nullptr;
	QLabel* _timeLabel = nullptr;

	SceneGraph* _sceneGraph = nullptr;
	ViewportWidget* _viewportWidget = nullptr;
	bool _overlayMode = false;
	bool _scrubbing = false;
	bool _syncingControls = false;
	double _currentDurationSeconds = 0.0;
	QString _selectedSourceFile;
	int _selectedClipIndex = -1;

	QPalette _savedPalette;
	QPalette _savedViewportPalette;
	bool _savedAutoFill = false;
	bool _savedViewportAutoFill = false;
	QString _savedStyleSheet;
	QString _savedPlayPauseStyle;
	QString _savedResetStyle;
	QColor _detachedOverlayFillColor = QColor(255, 255, 255, 65);
};
