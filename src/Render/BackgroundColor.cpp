#include "BackgroundColor.h"

#include "BackgroundPreview.h"
#include "ui_BackgroundColor.h"
#include "LanguageManager.h"
#include "ScreenColorPicker.h"
#include "ViewportWidget.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kRecentMax = 8;
const char* const kRecentKey = "Background/RecentColors";
const char* const kUserPresetsKey = "Background/UserPresets";
const char* const kLivePreviewKey = "Background/LivePreview";
const char* const kGeometryKey = "BackgroundColor/geometry";

struct BuiltInPreset
{
	const char* name;
	unsigned first;
	unsigned second;
	int style;
	bool gradient;
};

// The first one is what Default resets to. The names are translated when shown.
const BuiltInPreset kBuiltInPresets[] = {
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Default"),     0x737373, 0xE6E6E6, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Studio"),      0xEDEEF0, 0x9AA0A8, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Classic CAD"), 0x7FA8D8, 0xF4F7FB, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Sky"),         0x2F6FB3, 0xDCEBFA, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Warm"),        0xF6E7D4, 0xC9A982, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Dark"),        0x1B1D22, 0x3A3F4B, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Blueprint"),   0x0E2F5A, 0x1F5CA3, 0, true },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Mid grey"),    0x808080, 0x808080, 0, false },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Paper"),       0xFFFFFF, 0xFFFFFF, 0, false },
	{ QT_TRANSLATE_NOOP("BackgroundColor", "Black"),       0x000000, 0x000000, 0, false },
};

QSettings appSettings()
{
	return QSettings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
}

QString hexOf(const QColor& color)
{
	return color.name(QColor::HexRgb).toUpper();
}

bool sameColor(const QColor& a, const QColor& b)
{
	return a.rgb() == b.rgb();
}

QPixmap presetIcon(const QColor& first, const QColor& second, int style, bool gradient)
{
	const QSize size(56, 34);
	QPixmap pixmap = QPixmap::fromImage(BackgroundPreview::renderBackground(size, first, second, style, gradient));
	QPainter painter(&pixmap);
	painter.setPen(QColor(0, 0, 0, 140));
	painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
	return pixmap;
}
}

BackgroundColor::BackgroundColor(QWidget* parent) :
	QDialog(parent),
	ui(new Ui::BackgroundColor),
	_viewport(dynamic_cast<ViewportWidget*>(parent))
{
	loadState();
	loadPresets();
	buildUi();
	retranslateUi();
	rebuildPresetList();
	rebuildRecent();
	refreshUi();

	QSettings settings = appSettings();
	restoreGeometry(settings.value(kGeometryKey).toByteArray());

	// A live language switch in Settings re-labels the open dialog.
	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		retranslateUi();
		rebuildPresetList();
		refreshUi();
		});
}

BackgroundColor::~BackgroundColor() = default;

// ---------------------------------------------------------------------------------------------------------------------------------
// State

void BackgroundColor::loadState()
{
	QSettings settings = appSettings();
	_gradient = settings.value("Background/StyleIndex", 0).toInt() != 1; // 0 = Gradient, 1 = Solid

	if (_viewport)
	{
		_topColor = _viewport->getBgTopColor();
		_bottomColor = _viewport->getBgBotColor();
		_gradientStyle = std::clamp(_viewport->getBgGradientStyle(), 0, 3);
	}
	else
	{
		_topColor = QColor::fromRgbF(0.45f, 0.45f, 0.45f, 1.0f);
		_bottomColor = QColor::fromRgbF(0.9f, 0.9f, 0.9f, 1.0f);
		_gradientStyle = 0;
	}
	_viewportTop = _topColor;
	_viewportBottom = _bottomColor;
	_viewportStyle = _gradientStyle;

	// A solid background hands the viewport the top colour for both ends; the second colour the user chose is kept in the settings.
	if (!_gradient)
	{
		const QVariant stored = settings.value("Background/BottomColor");
		if (stored.isValid() && stored.canConvert<QColor>())
			_bottomColor = stored.value<QColor>();
	}
}

void BackgroundColor::saveSettings()
{
	QSettings settings = appSettings();
	settings.setValue("Background/TopColor", _topColor);
	settings.setValue("Background/BottomColor", _bottomColor);
	settings.setValue("Background/GradientStyle", _gradientStyle);
	// Style mode (0 = Gradient, 1 = Solid) stays in step with the Settings dialog.
	settings.setValue("Background/StyleIndex", _gradient ? 0 : 1);
}

void BackgroundColor::applyBgColors()
{
	if (_viewport)
	{
		_viewport->setBgTopColor(_topColor);
		_viewport->setBgBotColor(_gradient ? _bottomColor : _topColor);
		_viewport->setBgGradientStyle(_gradientStyle);
		saveSettings();
		_viewport->loadBgColorSettings(); // sync bgStyleIndex on the render controller
	}
	else
		saveSettings();

	// What Close puts back from now on.
	_viewportTop = _topColor;
	_viewportBottom = _gradient ? _bottomColor : _topColor;
	_viewportStyle = _gradientStyle;

	addRecent(_topColor);
	if (_gradient)
		addRecent(_bottomColor);
}

void BackgroundColor::setColor(int which, const QColor& color)
{
	if (!color.isValid())
		return;
	(which == 0 ? _topColor : _bottomColor) = QColor(color.rgb());
	refreshUi();
}

void BackgroundColor::setGradient(bool gradient)
{
	_gradient = gradient;
	if (!_gradient)
		_active = 0;
	refreshUi();
}

void BackgroundColor::setStyle(int style)
{
	style = std::clamp(style, 0, 3);
	if (style == _gradientStyle)
		return;
	_gradientStyle = style;
	refreshUi();
}

void BackgroundColor::swapColors()
{
	std::swap(_topColor, _bottomColor);
	refreshUi();
}

void BackgroundColor::applyPreset(int index)
{
	if (index < 0 || index >= _presets.size())
		return;
	const Preset preset = _presets[index];
	_topColor = preset.first;
	_bottomColor = preset.second;
	_gradientStyle = preset.style;
	_gradient = preset.gradient;
	if (!_gradient)
		_active = 0;
	refreshUi();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Building the dialog

void BackgroundColor::buildUi()
{
	ui->setupUi(this);

	// The preview is a custom widget: the .ui holds a plain placeholder at its place, swapped for the real one here.
	_preview = new BackgroundPreview(this);
	ui->previewColumn->replaceWidget(ui->previewPlaceholder, _preview);
	delete ui->previewPlaceholder;
	ui->previewPlaceholder = _preview;

	_modeSolid = ui->modeSolid;
	_modeGradient = ui->modeGradient;
	_directionCombo = ui->directionCombo;
	_swapButton = ui->swapButton;
	_colorLabel[0] = ui->labelColor0;
	_colorLabel[1] = ui->labelColor1;
	_swatchButton[0] = ui->swatchButton0;
	_swatchButton[1] = ui->swatchButton1;
	_hexEdit[0] = ui->hexEdit0;
	_hexEdit[1] = ui->hexEdit1;
	_eyedropperButton[0] = ui->eyedropperButton0;
	_eyedropperButton[1] = ui->eyedropperButton1;
	_hintLabel = ui->hintLabel;
	_recentLabel = ui->labelRecent;
	_recentLayout = ui->recentLayout;
	_presetLabel = ui->labelPresets;
	_savePresetButton = ui->savePresetButton;
	_presetList = ui->presetList;
	_livePreviewCheck = ui->livePreviewCheck;
	_defaultButton = ui->defaultButton;
	_okButton = ui->okButton;
	_applyButton = ui->applyButton;
	_closeButton = ui->cancelButton;

	// Solid | Gradient act as one choice.
	auto* modeGroup = new QButtonGroup(this);
	modeGroup->setExclusive(true);
	modeGroup->addButton(_modeSolid);
	modeGroup->addButton(_modeGradient);
	connect(_modeSolid, &QPushButton::clicked, this, [this]() { setGradient(false); });
	connect(_modeGradient, &QPushButton::clicked, this, [this]() { setGradient(true); });
	connect(_directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (!_refreshing)
			setStyle(index);
		});
	connect(_swapButton, &QToolButton::clicked, this, [this]() { swapColors(); });

	connect(_preview, &BackgroundPreview::colorClicked, this, [this](int which) {
		_active = which;
		refreshUi();
		editColor(which);
		});
	connect(_preview, &BackgroundPreview::gradientStyleDragged, this, [this](int style) { setStyle(style); });
	QFont hintFont = _hintLabel->font();
	hintFont.setItalic(true);
	_hintLabel->setFont(hintFont);

	const QRegularExpression hexPattern(QStringLiteral("^#?[0-9A-Fa-f]{0,6}$"));
	for (int i = 0; i < 2; ++i)
	{
		connect(_swatchButton[i], &QPushButton::clicked, this, [this, i]() {
			_active = i;
			refreshUi();
			editColor(i);
			});
		_hexEdit[i]->setValidator(new QRegularExpressionValidator(hexPattern, _hexEdit[i]));
		connect(_hexEdit[i], &QLineEdit::textEdited, this, [this, i](const QString&) { commitHex(i); });
		connect(_hexEdit[i], &QLineEdit::editingFinished, this, [this, i]() {
			commitHex(i);
			// Whatever was left half typed goes back to the colour in use.
			const QString current = hexOf(i == 0 ? _topColor : _bottomColor);
			if (_hexEdit[i]->text().compare(current, Qt::CaseInsensitive) != 0)
				_hexEdit[i]->setText(current);
			});
		connect(_eyedropperButton[i], &QPushButton::clicked, this, [this, i]() { pickFromScreen(i); });
	}

	connect(_savePresetButton, &QPushButton::clicked, this, [this]() { saveCurrentAsPreset(); });
	connect(_presetList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
		applyPreset(_presetList->row(item));
		});
	connect(_presetList, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { presetContextMenu(pos); });

	{
		QSettings settings = appSettings();
		_livePreviewCheck->setChecked(settings.value(kLivePreviewKey, true).toBool());
	}
	connect(_livePreviewCheck, &QCheckBox::toggled, this, [this](bool on) {
		QSettings settings = appSettings();
		settings.setValue(kLivePreviewKey, on);
		if (on)
			liveApply();
		else
			restoreViewport();
		});
	connect(_defaultButton, &QPushButton::clicked, this, [this]() { applyPreset(0); });
	connect(_okButton, &QPushButton::clicked, this, [this]() {
		applyBgColors();
		accept();
		});
	connect(_applyButton, &QPushButton::clicked, this, [this]() { applyBgColors(); });
	connect(_closeButton, &QPushButton::clicked, this, [this]() { reject(); });
}

void BackgroundColor::retranslateUi()
{
	ui->retranslateUi(this); // everything the .ui holds; the preview and the texts that depend on the state are set here and in refreshUi()
	_preview->setToolTip(tr("Click a circle to choose that color.\n"
		"Drag the first circle to change the direction.\n"
		"The lines show how wireframes look on this background."));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Refreshing

void BackgroundColor::updateSwatchButton(QPushButton* button, const QColor& color, bool active) const
{
	const QString ring = active ? palette().color(QPalette::Highlight).name() : QStringLiteral("#808080");
	button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: %2px solid %3; border-radius: 3px; }")
		.arg(hexOf(color)).arg(active ? 3 : 1).arg(ring));
}

void BackgroundColor::refreshUi()
{
	if (_refreshing)
		return;
	_refreshing = true;

	if (!_gradient)
		_active = 0;

	_preview->setState(_topColor, _bottomColor, _gradientStyle, _gradient);
	_preview->setActiveColor(_active);

	_modeSolid->setChecked(!_gradient);
	_modeGradient->setChecked(_gradient);
	_directionCombo->setCurrentIndex(_gradientStyle);
	_directionCombo->setEnabled(_gradient);
	_swapButton->setEnabled(_gradient);

	const QColor colors[2] = { _topColor, _bottomColor };
	for (int i = 0; i < 2; ++i)
	{
		const bool shown = i == 0 || _gradient;
		_colorLabel[i]->setVisible(shown);
		_swatchButton[i]->setVisible(shown);
		_hexEdit[i]->setVisible(shown);
		_eyedropperButton[i]->setVisible(shown);
		updateSwatchButton(_swatchButton[i], colors[i], _active == i);
		const QString hex = hexOf(colors[i]);
		// A hex field being typed in keeps its own text while it is the same colour (case and the # do not matter).
		const QString typed = _hexEdit[i]->text();
		const bool sameText = typed.compare(hex, Qt::CaseInsensitive) == 0
			|| (!typed.startsWith(QLatin1Char('#')) && (QLatin1Char('#') + typed).compare(hex, Qt::CaseInsensitive) == 0);
		if (!(_hexEdit[i]->hasFocus() && sameText))
			_hexEdit[i]->setText(hex);
	}
	_colorLabel[0]->setText(_gradient ? tr("First color") : tr("Color"));
	_colorLabel[1]->setText(tr("Second color"));

	const bool strong = BackgroundPreview::isStrongGradient(_topColor, _bottomColor, _gradient);
	_hintLabel->setVisible(strong);
	_hintLabel->setText(tr("This gradient crosses mid-grey: wireframe lines get a light or dark outline here so they stay readable."));

	// Mark the preset that matches the current state, if any.
	{
		const QSignalBlocker blocker(_presetList);
		int match = -1;
		for (int i = 0; i < _presets.size(); ++i)
		{
			const Preset& p = _presets[i];
			if (p.gradient != _gradient || !sameColor(p.first, _topColor))
				continue;
			if (_gradient && (!sameColor(p.second, _bottomColor) || p.style != _gradientStyle))
				continue;
			match = i;
			break;
		}
		_presetList->setCurrentRow(match);
	}

	_refreshing = false;
	liveApply();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Colour entry

void BackgroundColor::editColor(int which)
{
	_active = which;
	const QColor original = which == 0 ? _topColor : _bottomColor;
	QColorDialog dialog(original, this);
	dialog.setWindowTitle(tr("Choose Color"));
	// The preview and the viewport follow the dialog while a colour is being chosen; Cancel puts the old colour back.
	connect(&dialog, &QColorDialog::currentColorChanged, this, [this, which](const QColor& color) { setColor(which, color); });
	if (dialog.exec() == QDialog::Accepted)
	{
		setColor(which, dialog.currentColor());
		addRecent(dialog.currentColor());
	}
	else
		setColor(which, original);
}

void BackgroundColor::commitHex(int which)
{
	QString text = _hexEdit[which]->text().trimmed();
	if (!text.startsWith(QLatin1Char('#')))
		text.prepend(QLatin1Char('#'));
	if (text.size() != 7)
		return; // still being typed
	const QColor color(text);
	if (!color.isValid())
		return;
	_active = which;
	setColor(which, color);
}

void BackgroundColor::pickFromScreen(int which)
{
	_active = which;
	const QColor original = which == 0 ? _topColor : _bottomColor;
	QScreen* targetScreen = screen();
	if (!targetScreen)
		targetScreen = QGuiApplication::primaryScreen();
	// The dialog must not be in the snapshot. It is made invisible, not hidden: hiding a dialog that is running exec() ends it.
	setWindowOpacity(0.0);
	QTimer::singleShot(250, this, [this, targetScreen, which, original]() {
		auto* picker = new ScreenColorPicker(targetScreen, this);
		connect(picker, &ScreenColorPicker::colorPicked, this, [this, which](const QColor& color) {
			setColor(which, color);
			addRecent(color);
			});
		connect(picker, &ScreenColorPicker::cancelled, this, [this, which, original]() { setColor(which, original); });
		connect(picker, &QObject::destroyed, this, [this]() {
			setWindowOpacity(1.0);
			raise();
			activateWindow();
			});
		picker->start();
		});
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Recent colours

QStringList BackgroundColor::recentColors() const
{
	QSettings settings = appSettings();
	QStringList list = settings.value(kRecentKey).toStringList();
	list.erase(std::remove_if(list.begin(), list.end(), [](const QString& s) { return !QColor(s).isValid(); }), list.end());
	while (list.size() > kRecentMax)
		list.removeLast();
	return list;
}

void BackgroundColor::addRecent(const QColor& color)
{
	if (!color.isValid())
		return;
	QStringList list = recentColors();
	const QString name = hexOf(color);
	list.removeAll(name);
	list.prepend(name);
	while (list.size() > kRecentMax)
		list.removeLast();
	QSettings settings = appSettings();
	settings.setValue(kRecentKey, list);
	rebuildRecent();
}

void BackgroundColor::rebuildRecent()
{
	while (QLayoutItem* item = _recentLayout->takeAt(0))
	{
		delete item->widget();
		delete item;
	}
	const QStringList list = recentColors();
	for (const QString& name : list)
	{
		auto* button = new QPushButton(this);
		button->setFixedSize(22, 22);
		button->setAutoDefault(false);
		button->setToolTip(name);
		button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid #808080; border-radius: 3px; }").arg(name));
		connect(button, &QPushButton::clicked, this, [this, name]() { setColor(_active, QColor(name)); });
		_recentLayout->addWidget(button);
	}
	_recentLabel->setVisible(!list.isEmpty());
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Presets

void BackgroundColor::loadPresets()
{
	_presets.clear();
	for (const BuiltInPreset& b : kBuiltInPresets)
	{
		Preset p;
		p.name = QCoreApplication::translate("BackgroundColor", b.name);
		p.first = QColor::fromRgb(b.first);
		p.second = QColor::fromRgb(b.second);
		p.style = b.style;
		p.gradient = b.gradient;
		p.user = false;
		_presets.push_back(p);
	}
	// User presets: "name|RRGGBB|RRGGBB|style|gradient".
	QSettings settings = appSettings();
	const QStringList stored = settings.value(kUserPresetsKey).toStringList();
	for (const QString& entry : stored)
	{
		const QStringList parts = entry.split(QLatin1Char('|'));
		if (parts.size() != 5)
			continue;
		const QColor first(parts[1]), second(parts[2]);
		if (parts[0].isEmpty() || !first.isValid() || !second.isValid())
			continue;
		Preset p;
		p.name = parts[0];
		p.first = first;
		p.second = second;
		p.style = std::clamp(parts[3].toInt(), 0, 3);
		p.gradient = parts[4].toInt() != 0;
		p.user = true;
		_presets.push_back(p);
	}
}

void BackgroundColor::rebuildPresetList()
{
	loadPresets(); // the built-in names follow the current language
	const QSignalBlocker blocker(_presetList);
	_presetList->clear();
	for (const Preset& p : _presets)
	{
		auto* item = new QListWidgetItem(QIcon(presetIcon(p.first, p.second, p.style, p.gradient)), p.name);
		item->setToolTip(p.user ? tr("%1 (your preset)").arg(p.name) : p.name);
		item->setTextAlignment(Qt::AlignHCenter);
		_presetList->addItem(item);
	}
}

void BackgroundColor::saveCurrentAsPreset()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"), QLineEdit::Normal, tr("My background"), &ok);
	name.remove(QLatin1Char('|'));
	name = name.trimmed();
	if (!ok || name.isEmpty())
		return;

	QSettings settings = appSettings();
	QStringList stored = settings.value(kUserPresetsKey).toStringList();
	stored.erase(std::remove_if(stored.begin(), stored.end(),
		[&name](const QString& s) { return s.section(QLatin1Char('|'), 0, 0) == name; }), stored.end());
	stored.push_back(QStringLiteral("%1|%2|%3|%4|%5").arg(name, hexOf(_topColor), hexOf(_bottomColor)).arg(_gradientStyle).arg(_gradient ? 1 : 0));
	settings.setValue(kUserPresetsKey, stored);

	rebuildPresetList();
	refreshUi();
}

void BackgroundColor::presetContextMenu(const QPoint& pos)
{
	QListWidgetItem* item = _presetList->itemAt(pos);
	if (!item)
		return;
	const int row = _presetList->row(item);
	if (row < 0 || row >= _presets.size() || !_presets[row].user)
		return;
	QMenu menu(this);
	QAction* remove = menu.addAction(tr("Delete preset"));
	if (menu.exec(_presetList->viewport()->mapToGlobal(pos)) != remove)
		return;

	const QString name = _presets[row].name;
	QSettings settings = appSettings();
	QStringList stored = settings.value(kUserPresetsKey).toStringList();
	stored.erase(std::remove_if(stored.begin(), stored.end(),
		[&name](const QString& s) { return s.section(QLatin1Char('|'), 0, 0) == name; }), stored.end());
	settings.setValue(kUserPresetsKey, stored);
	rebuildPresetList();
	refreshUi();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The viewport

bool BackgroundColor::viewportDiffersFrom(const QColor& top, const QColor& bottom, int style) const
{
	return !_viewport || !sameColor(_viewport->getBgTopColor(), top) || !sameColor(_viewport->getBgBotColor(), bottom)
		|| _viewport->getBgGradientStyle() != style;
}

void BackgroundColor::liveApply()
{
	if (!_viewport || !_livePreviewCheck || !_livePreviewCheck->isChecked())
		return;
	const QColor bottom = _gradient ? _bottomColor : _topColor;
	if (!viewportDiffersFrom(_topColor, bottom, _gradientStyle))
		return;
	_viewport->setBgTopColor(_topColor);
	_viewport->setBgBotColor(bottom);
	_viewport->setBgGradientStyle(_gradientStyle);
}

void BackgroundColor::restoreViewport()
{
	if (!_viewport || !viewportDiffersFrom(_viewportTop, _viewportBottom, _viewportStyle))
		return;
	_viewport->setBgTopColor(_viewportTop);
	_viewport->setBgBotColor(_viewportBottom);
	_viewport->setBgGradientStyle(_viewportStyle);
}

void BackgroundColor::reject()
{
	restoreViewport(); // Close (and Esc) take a live preview back; what Apply stored stays
	QDialog::reject();
}

void BackgroundColor::done(int result)
{
	QSettings settings = appSettings();
	settings.setValue(kGeometryKey, saveGeometry());
	QDialog::done(result);
}
