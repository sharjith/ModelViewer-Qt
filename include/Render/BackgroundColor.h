#pragma once

#include <QColor>
#include <QDialog>
#include <memory>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Ui {
	class BackgroundColor;
}

class BackgroundPreview;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPoint;
class QPushButton;
class QToolButton;
class ViewportWidget;

// Background Color dialog. A live preview square (the same gradient maths as the viewport, with sample wireframe lines), swatches
// on the preview to pick the two colours and drag the gradient direction, hex fields and an eyedropper, a strip of recent colours,
// a strip of presets (built in and saved by the user), and an optional live preview on the real viewport that Close takes back.
class BackgroundColor : public QDialog
{
	Q_OBJECT

public:
	explicit BackgroundColor(QWidget* parent = nullptr);
	~BackgroundColor() override;

	bool hasGradient() const { return _gradient; }
	QColor getTopColor() const { return _topColor; }
	QColor getBottomColor() const { return _bottomColor; }

	// Hands the colours to the viewport and stores them in the settings (what Apply and OK do).
	void applyBgColors();

protected:
	void reject() override; // takes a live preview back
	void done(int result) override;

private:
	struct Preset
	{
		QString name;
		QColor first;
		QColor second;
		int style = 0;
		bool gradient = true;
		bool user = false;
	};

	void buildUi();
	void retranslateUi();
	void loadState();
	void saveSettings();

	// State changes; each ends in refreshUi().
	void setColor(int which, const QColor& color);
	void setGradient(bool gradient);
	void setStyle(int style);
	void swapColors();
	void applyPreset(int index);
	void refreshUi();
	void updateSwatchButton(QPushButton* button, const QColor& color, bool active) const;

	// Colour entry.
	void editColor(int which);
	void pickFromScreen(int which);
	void commitHex(int which);

	// Recent colours and presets.
	QStringList recentColors() const;
	void addRecent(const QColor& color);
	void rebuildRecent();
	void loadPresets();
	void rebuildPresetList();
	void saveCurrentAsPreset();
	void presetContextMenu(const QPoint& pos);

	// The viewport.
	void liveApply();
	void restoreViewport();
	bool viewportDiffersFrom(const QColor& top, const QColor& bottom, int style) const;

	std::unique_ptr<Ui::BackgroundColor> ui; // needs the out-of-line destructor: Ui:: is incomplete here

	ViewportWidget* _viewport = nullptr;

	QColor _topColor;
	QColor _bottomColor;
	int _gradientStyle = 0;
	bool _gradient = true;
	int _active = 0; // the colour the hex field / recent strip / eyedropper act on

	// What the viewport showed when the dialog opened (and after each Apply): Close puts it back.
	QColor _viewportTop;
	QColor _viewportBottom;
	int _viewportStyle = 0;

	bool _refreshing = false;
	QVector<Preset> _presets;

	// The widgets of the .ui, kept under these names; the preview is the one custom widget (see buildUi()).
	BackgroundPreview* _preview = nullptr;
	QPushButton* _modeSolid = nullptr;
	QPushButton* _modeGradient = nullptr;
	QComboBox* _directionCombo = nullptr;
	QToolButton* _swapButton = nullptr;
	QLabel* _colorLabel[2] = { nullptr, nullptr };
	QPushButton* _swatchButton[2] = { nullptr, nullptr };
	QLineEdit* _hexEdit[2] = { nullptr, nullptr };
	QPushButton* _eyedropperButton[2] = { nullptr, nullptr };
	QLabel* _hintLabel = nullptr;
	QLabel* _recentLabel = nullptr;
	QHBoxLayout* _recentLayout = nullptr;
	QLabel* _presetLabel = nullptr;
	QPushButton* _savePresetButton = nullptr;
	QListWidget* _presetList = nullptr;
	QCheckBox* _livePreviewCheck = nullptr;
	QPushButton* _defaultButton = nullptr;
	QPushButton* _okButton = nullptr;
	QPushButton* _applyButton = nullptr;
	QPushButton* _closeButton = nullptr;
};
