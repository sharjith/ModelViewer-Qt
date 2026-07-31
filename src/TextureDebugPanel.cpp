#include "TextureDebugPanel.h"
#include "ViewportWidget.h"
#include "ModelViewer.h"
#include "LanguageManager.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>
#include <QSettings>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr int ThumbSize    = 80;   // thumbnail width/height in pixels
static constexpr int ThumbColumns = 3;    // swatches per row in the grid

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

// Grey checkerboard placeholder for inactive texture slots.
QPixmap inactivePlaceholder()
{
	QPixmap pm(ThumbSize, ThumbSize);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	const QColor c1(80, 80, 80);
	const QColor c2(55, 55, 55);
	const int sq = ThumbSize / 8;
	for (int row = 0; row < 8; ++row)
	{
		for (int col = 0; col < 8; ++col)
		{
			p.fillRect(col * sq, row * sq, sq, sq,
			           ((row + col) % 2 == 0) ? c1 : c2);
		}
	}
	return pm;
}

// Small coloured dot used for extension flag indicators.
// active=false → grey outline (extension not in use)
// active=true, disabledByUser=false → green filled (active, toggleable)
// active=true, disabledByUser=true  → amber filled with X (active but suppressed)
QPixmap dotPixmap(bool active, bool disabledByUser = false)
{
	const int sz = 14;
	QPixmap pm(sz, sz);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);

	QColor col;
	bool filled = false;
	if (!active)
	{
		col    = QColor(100, 100, 100);
		filled = false;
	}
	else if (disabledByUser)
	{
		col    = QColor(232, 168, 56);  // amber
		filled = true;
	}
	else
	{
		col    = QColor(72, 199, 116);  // green
		filled = true;
	}

	p.setPen(QPen(col.darker(140), 1));
	p.setBrush(filled ? QBrush(col) : Qt::NoBrush);
	p.drawEllipse(1, 1, sz - 2, sz - 2);

	// Strikethrough X when disabled by user so the state is unmistakeable.
	if (disabledByUser)
	{
		p.setPen(QPen(col.darker(170), 1.5f));
		p.drawLine(4, 4, sz - 4, sz - 4);
		p.drawLine(sz - 4, 4, 4, sz - 4);
	}
	return pm;
}

// Helper: create a horizontal separator line.
QFrame* makeSeparator(QWidget* parent)
{
	auto* line = new QFrame(parent);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

// Helper: clear all widgets from a QGridLayout.
// Uses delete (not deleteLater) so widgets are removed from the parent's
// child list immediately — deleteLater leaves them as visible children of
// the container until the next event-loop iteration, which means the old
// swatches can repaint after clearDynamicContent() has returned.
void clearLayout(QGridLayout* layout)
{
	if (!layout) return;
	while (layout->count() > 0)
	{
		QLayoutItem* item = layout->takeAt(0);
		if (item->widget())
			delete item->widget();   // immediate destruction, not deferred
		delete item;
	}
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
TextureDebugPanel::TextureDebugPanel(QWidget* parent)
	: QDialog(parent,
	          Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
	          Qt::WindowMinMaxButtonsHint)
{
	setWindowTitle(tr("Texture Debugger"));
	setObjectName("TextureDebugPanel");
	buildUI();
	restoreWindowGeometry();

	// This panel builds its UI entirely in code (no .ui file, so no generated
	// retranslateUi() exists) - retranslateTexts() is buildUI()'s hand-written
	// equivalent. See its own doc comment for how it re-applies every
	// translated string without rebuilding the layout.
	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		retranslateTexts();
		});
}

void TextureDebugPanel::setViewportWidget(ViewportWidget* viewportWidget)
{
	_viewportWidget = viewportWidget;
	if (_viewportWidget)
		connect(_viewportWidget, &ViewportWidget::renderingModeChanged,
		        this, [this](int) { updatePBRWarning(); });
}

void TextureDebugPanel::setModelViewer(ModelViewer* mv)
{
	_modelViewer = mv;
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void TextureDebugPanel::buildUI()
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);

	// ---- Header row --------------------------------------------------------
	{
		auto* headerRow = new QHBoxLayout;
		_meshNameLabel = new QLabel(tr("No mesh selected"), this);
		QFont f = _meshNameLabel->font();
		f.setBold(true);
		_meshNameLabel->setFont(f);
		_meshNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

		_refreshButton = new QPushButton(tr("↻ Refresh"), this);
		_refreshButton->setFixedWidth(80);
		connect(_refreshButton, &QPushButton::clicked, this, &TextureDebugPanel::refresh);

		headerRow->addWidget(_meshNameLabel);
		headerRow->addWidget(_refreshButton);
		root->addLayout(headerRow);
	}

	root->addWidget(makeSeparator(this));

	// ---- Channel isolation row ---------------------------------------------
	// "All" = checkbox mode (multi-texture combined effect).
	// Specific channel = Khronos-style in-shader single-channel isolation.
	// IDs 1-8: geometry/vertex channels.  IDs 10-17: texture channels.
	{
		auto* row = new QHBoxLayout;
		_channelLabel = new QLabel(tr("Channel:"), this);
		_channelCombo = new QComboBox(this);
		populateChannelCombo();

		row->addWidget(_channelLabel);
		row->addWidget(_channelCombo, 1);
		root->addLayout(row);

		connect(_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
		        this, [this](int comboIndex)
		{
			// Separator items have invalid itemData — skip them.
			const QVariant data = _channelCombo->itemData(comboIndex);
			if (!data.isValid()) return;

			const int channelId = data.toInt();
			_activeChannelId = channelId;

			if (!_viewportWidget) return;

			if (channelId == 0)
			{
				// Restore to checkbox-mode: clear channel isolation on ALL meshes,
				// then re-apply per-mesh checkbox state for the selected mesh.
				_viewportWidget->setGlobalDebugChannel(0);
				if (_currentMeshId >= 0)
				{
					for (const QString& key : _disabledExtensions)
						_viewportWidget->setDebugExtensionEnabled(_currentMeshId, key, false);
					applyCurrentTextureState();
				}
				_thumbnailScroll->setEnabled(true);
				_extensionGroup->setEnabled(true);
			}
			else
			{
				// Single-channel isolation: applies to all meshes globally.
				// Checkboxes and extensions have no effect while this is active.
				if (_currentMeshId >= 0)
					_viewportWidget->clearDebugExtensionOverrides(_currentMeshId);
				_viewportWidget->setGlobalDebugChannel(channelId);
				_thumbnailScroll->setEnabled(false);
				_extensionGroup->setEnabled(false);
			}

			// Repopulate to update thumbnail border/style for active channel.
			if (!_lastSlots.isEmpty())
				populateThumbnails(_lastSlots);
		});
	}

	root->addWidget(makeSeparator(this));

	// ---- PBR mode warning strip --------------------------------------------
	// Shown in amber whenever the renderer is not in PBR mode.  Hidden for the
	// common case (PBR active); updated reactively via renderingModeChanged.
	{
		_pbrWarningLabel = new QLabel(this);
		_pbrWarningLabel->setText(
		    tr("⚠  Switch to PBR rendering mode for accurate channel display"));
		_pbrWarningLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		_pbrWarningLabel->setWordWrap(true);
		_pbrWarningLabel->setContentsMargins(4, 3, 4, 3);
		QFont wf = _pbrWarningLabel->font();
		wf.setPointSize(11);
		_pbrWarningLabel->setFont(wf);
		_pbrWarningLabel->setStyleSheet("color: #e8a838;");
		_pbrWarningLabel->hide();
		root->addWidget(_pbrWarningLabel);
	}

	// ---- Textures section --------------------------------------------------
	{
		_texturesSectionLabel = new QLabel(tr("TEXTURES"), this);
		QFont f = _texturesSectionLabel->font();
		f.setBold(true);
		_texturesSectionLabel->setFont(f);

		_showInactiveCheck = new QCheckBox(tr("Show inactive slots"), this);
		_showInactiveCheck->setChecked(false);
		connect(_showInactiveCheck, &QCheckBox::toggled, this, [this](bool) {
			if (!_lastSlots.isEmpty())
				populateThumbnails(_lastSlots);
		});

		auto* texHeaderRow = new QHBoxLayout;
		texHeaderRow->addWidget(_texturesSectionLabel);
		texHeaderRow->addStretch();
		texHeaderRow->addWidget(_showInactiveCheck);
		root->addLayout(texHeaderRow);

		// Scrollable thumbnail grid
		_thumbnailContainer = new QWidget(this);
		_thumbnailGrid = new QGridLayout(_thumbnailContainer);
		_thumbnailGrid->setSpacing(6);
		_thumbnailGrid->setContentsMargins(4, 4, 4, 4);

		_thumbnailScroll = new QScrollArea(this);
		_thumbnailScroll->setWidget(_thumbnailContainer);
		_thumbnailScroll->setWidgetResizable(true);
		_thumbnailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		_thumbnailScroll->setMinimumHeight(200);
		_thumbnailScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		root->addWidget(_thumbnailScroll, /*stretch=*/1);
	}

	root->addWidget(makeSeparator(this));

	// ---- Extensions section ------------------------------------------------
	{
		_extensionGroup  = new QGroupBox(tr("Extensions"), this);
		_extensionLayout = new QGridLayout(_extensionGroup);
		_extensionLayout->setSpacing(4);
		_extensionLayout->setContentsMargins(6, 4, 6, 4);
		root->addWidget(_extensionGroup);
	}

	// ---- Status strip -------------------------------------------------------
	// Shows warnings such as "multiple meshes selected — showing first only".
	// Hidden when there is nothing to report.
	root->addWidget(makeSeparator(this));
	{
		_statusLabel = new QLabel(this);
		_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
		_statusLabel->setWordWrap(true);
		QFont sf = _statusLabel->font();
		sf.setPointSize(11);
		_statusLabel->setFont(sf);
		_statusLabel->setContentsMargins(2, 2, 2, 2);
		_statusLabel->hide();  // hidden until there is something to say
		root->addWidget(_statusLabel);
	}

	setMinimumWidth(380);
	resize(420, 620);
}

// ---------------------------------------------------------------------------
// populateChannelCombo
// ---------------------------------------------------------------------------
void TextureDebugPanel::populateChannelCombo()
{
	// "All" — normal checkbox-driven rendering
	_channelCombo->addItem(tr("All"), 0);

	// Geometry / vertex group
	_channelCombo->insertSeparator(_channelCombo->count());
	_channelCombo->addItem(tr("Texture Coordinates 0"),  1);
	_channelCombo->addItem(tr("Texture Coordinates 1"),  2);
	_channelCombo->addItem(tr("Geometry Normal"),        3);
	_channelCombo->addItem(tr("Geometry Tangent"),       4);
	_channelCombo->addItem(tr("Geometry Bitangent"),     5);
	_channelCombo->addItem(tr("Geometry Tangent W"),     6);
	_channelCombo->addItem(tr("Shading Normal"),         7);
	_channelCombo->addItem(tr("Alpha"),                  8);
	_channelCombo->addItem(tr("Vertex Color"),           9);

	// Texture channels group
	_channelCombo->insertSeparator(_channelCombo->count());
	_channelCombo->addItem(tr("Albedo"),    10);
	_channelCombo->addItem(tr("Metallic"),  11);
	_channelCombo->addItem(tr("Roughness"), 16);
	_channelCombo->addItem(tr("AO"),        17);
	_channelCombo->addItem(tr("Emissive"),  12);
	_channelCombo->addItem(tr("Normal Map"), 13);
	_channelCombo->addItem(tr("Height"),    14);
	_channelCombo->addItem(tr("Opacity"),   15);

	// Extension texture channels group (PBR only)
	_channelCombo->insertSeparator(_channelCombo->count());
	_channelCombo->addItem(tr("Clearcoat Strength"),             18);
	_channelCombo->addItem(tr("Clearcoat Roughness"),            19);
	_channelCombo->addItem(tr("Clearcoat Normal"),               20);
	_channelCombo->addItem(tr("Specular Strength"),              21);
	_channelCombo->addItem(tr("Specular Color"),                 22);
	_channelCombo->addItem(tr("Anisotropic Strength"),           23);
	_channelCombo->addItem(tr("Anisotropic Direction"),          32);
	_channelCombo->addItem(tr("Iridescence Strength"),           24);
	_channelCombo->addItem(tr("Iridescence Thickness"),          25);
	_channelCombo->addItem(tr("Sheen Color"),                    26);
	_channelCombo->addItem(tr("Sheen Roughness"),                27);
	_channelCombo->addItem(tr("Transmission Strength"),          28);
	_channelCombo->addItem(tr("Volume Thickness"),               30);
	_channelCombo->addItem(tr("Diffuse Transmission Strength"),  34);
	_channelCombo->addItem(tr("Diffuse Transmission Color"),     35);
}

// ---------------------------------------------------------------------------
// retranslateTexts
// ---------------------------------------------------------------------------
void TextureDebugPanel::retranslateTexts()
{
	setWindowTitle(tr("Texture Debugger"));
	_refreshButton->setText(tr("↻ Refresh"));
	_channelLabel->setText(tr("Channel:"));
	_pbrWarningLabel->setText(tr("⚠  Switch to PBR rendering mode for accurate channel display"));
	_texturesSectionLabel->setText(tr("TEXTURES"));
	_showInactiveCheck->setText(tr("Show inactive slots"));
	_extensionGroup->setTitle(tr("Extensions"));

	// Rebuild the combo's items in place rather than hand-tracking each
	// item's index (fragile, given the interspersed separators) - remember
	// the previously active channel by its stored id, not its index, so the
	// user's current selection survives the rebuild.
	const int previousChannelId = _channelCombo->currentData().isValid()
		? _channelCombo->currentData().toInt() : 0;
	{
		const QSignalBlocker blocker(_channelCombo);
		_channelCombo->clear();
		populateChannelCombo();
		const int restoredIndex = _channelCombo->findData(previousChannelId);
		_channelCombo->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
	}

	// _meshNameLabel/_statusLabel and the thumbnail/extension grids are all
	// stateful (mesh name, multi-select count, per-slot tooltips) rather than
	// fixed strings - refresh() re-derives and re-applies all of that fresh
	// (via onTextureReadbackReady()/populateThumbnails()/populateExtensions(),
	// which already call tr() every time they run), so it's reused here
	// instead of duplicating that logic. One known gap: refresh() doesn't
	// re-run onSelectionChanged()'s multi-select branch, so _statusLabel's
	// "N meshes selected" warning (if currently showing) keeps its old
	// language until the next real selection change - a narrow, rarely-hit
	// edge case not worth the extra state tracking it would take to fix.
	refresh();
}

// ---------------------------------------------------------------------------
// Slot: onSelectionChanged
// ---------------------------------------------------------------------------
void TextureDebugPanel::onSelectionChanged(const QList<int>& selectedIds)
{
	if (selectedIds.isEmpty())
	{
		// Clear per-mesh texture and extension overrides for the deselected mesh.
		// The global channel (combo) is intentionally left unchanged — it continues
		// to apply to all scene meshes even without a panel selection.
		if (_currentMeshId >= 0 && _viewportWidget)
		{
			_viewportWidget->clearDebugTextureOverrides(_currentMeshId);
			_viewportWidget->clearDebugExtensionOverrides(_currentMeshId);
		}
		_disabledUnits.clear();
		_disabledExtensions.clear();
		// Swatches/extensions re-enabled only if in All mode.
		if (_activeChannelId == 0)
		{
			if (_thumbnailScroll) _thumbnailScroll->setEnabled(true);
			if (_extensionGroup)  _extensionGroup->setEnabled(true);
		}

		_currentMeshId = -1;
		_lastSlots.clear();
		clearDynamicContent();
		_meshNameLabel->setText(tr("No mesh selected"));
		_statusLabel->hide();
		_statusLabel->clear();
		return;
	}

	const int meshId = selectedIds.first();

	// Multi-select warning — always update the status strip.
	if (selectedIds.count() > 1)
	{
		_statusLabel->setText(
		    tr("⚠  %1 meshes selected — showing textures for the mesh in the title bar.")
		    .arg(selectedIds.count()));
		_statusLabel->setStyleSheet("color: #e8a838;");
		_statusLabel->show();
	}
	else
	{
		_statusLabel->hide();
		_statusLabel->clear();
	}

	if (meshId == _currentMeshId && !_lastSlots.isEmpty())
		return; // same mesh — no need to re-read

	// Switching to a different mesh: clear per-mesh overrides for the old one.
	// The global channel is not touched — it already applies to all meshes.
	if (_currentMeshId >= 0 && meshId != _currentMeshId && _viewportWidget)
	{
		_viewportWidget->clearDebugTextureOverrides(_currentMeshId);
		_viewportWidget->clearDebugExtensionOverrides(_currentMeshId);
	}
	_disabledUnits.clear();
	_disabledExtensions.clear();
	// Keep the channel combo as-is so the user can browse meshes in the same mode.

	_currentMeshId = meshId;

	if (isVisible() && _viewportWidget)
		_viewportWidget->requestTextureReadback(_currentMeshId);
}

// ---------------------------------------------------------------------------
// Slot: refresh
// ---------------------------------------------------------------------------
void TextureDebugPanel::refresh()
{
	if (_currentMeshId >= 0 && _viewportWidget)
		_viewportWidget->requestTextureReadback(_currentMeshId);
	else if (_currentMeshId < 0)
	{
		clearDynamicContent();
		_meshNameLabel->setText(tr("No mesh selected"));
	}
}

// ---------------------------------------------------------------------------
// Slot: onTextureReadbackReady
// ---------------------------------------------------------------------------
// NOTE: parameter is named 'slotInfos', not 'slots', because 'slots' is a
// Qt macro that expands to empty and would silently corrupt every use.
void TextureDebugPanel::onTextureReadbackReady(const QVector<TextureSlotInfo>& slotInfos,
                                               const QString& meshName)
{
	_lastSlots = slotInfos;

	_meshNameLabel->setText(
	    meshName.isEmpty() ? tr("Unknown mesh") : tr("Mesh: %1").arg(meshName));

	// Re-apply per-mesh texture state when in "All" mode.
	// In single-channel mode the global channel is already active on all meshes —
	// no per-mesh action needed here.
	if (_viewportWidget && _currentMeshId >= 0 && _activeChannelId == 0)
		applyCurrentTextureState();

	populateThumbnails(slotInfos);
	populateExtensions(slotInfos);
}

// ---------------------------------------------------------------------------
// populateThumbnails
// ---------------------------------------------------------------------------
void TextureDebugPanel::populateThumbnails(const QVector<TextureSlotInfo>& slotInfos)
{
	clearLayout(_thumbnailGrid);

	const bool showInactive = _showInactiveCheck->isChecked();
	const QPixmap placeholder = inactivePlaceholder();

	int col = 0, row = 0;
	for (const TextureSlotInfo& info : slotInfos)
	{
		if (info.isMarker)
			continue;  // scalar-activity markers have no texture to display
		if (!info.isActive && !showInactive)
			continue;

		const bool isDisabled = _disabledUnits.contains(info.unitIndex);

		// Cell widget
		auto* cell = new QWidget(_thumbnailContainer);
		auto* cellLayout = new QVBoxLayout(cell);
		cellLayout->setSpacing(2);
		cellLayout->setContentsMargins(0, 0, 0, 0);
		cellLayout->setAlignment(Qt::AlignHCenter);

		// ---- Thumbnail as a checkable QToolButton ----------------------------
		// Checked state = texture is DISABLED.  Clicking toggles the override.
		auto* thumbBtn = new QToolButton(cell);
		thumbBtn->setCheckable(true);
		thumbBtn->setChecked(isDisabled);
		thumbBtn->setFixedSize(ThumbSize, ThumbSize);
		thumbBtn->setAutoRaise(false);
		thumbBtn->setToolTip(isDisabled
		    ? tr("Click to re-enable this texture")
		    : tr("Click to disable this texture in the viewport"));

		// Show the real thumbnail when active and not disabled; checkerboard otherwise.
		const QPixmap& thumbPx = (info.isActive && !info.thumbnail.isNull() && !isDisabled)
		    ? info.thumbnail : placeholder;
		thumbBtn->setIcon(QIcon(thumbPx));
		thumbBtn->setIconSize(QSize(ThumbSize - 4, ThumbSize - 4));

		// Border: red when disabled, default otherwise.
		if (isDisabled)
			thumbBtn->setStyleSheet(
			    "QToolButton { border: 2px solid #e74c3c; background: transparent; }"
			    "QToolButton:checked { border: 2px solid #e74c3c; background: #2a1010; }");
		else
			thumbBtn->setStyleSheet(
			    "QToolButton { border: 1px solid #555; background: transparent; }");

		// Wire toggle: disable/enable the texture.
		// In single-channel mode the combo has disabled the scroll area so
		// these buttons are not interactive — no guard needed here.
		const int unitIdx = info.unitIndex;
		connect(thumbBtn, &QToolButton::toggled, this, [this, unitIdx](bool checked) {
			// checked == true  → user wants to DISABLE the texture
			// checked == false → user wants to RE-ENABLE the texture
			if (checked)
				_disabledUnits.insert(unitIdx);
			else
				_disabledUnits.remove(unitIdx);

			applyCurrentTextureState();

			// Repopulate so the thumbnail visual state updates immediately.
			if (!_lastSlots.isEmpty())
				populateThumbnails(_lastSlots);
		});

		// ---- Slot name -------------------------------------------------------
		auto* nameLabel = new QLabel(cell);
		nameLabel->setAlignment(Qt::AlignHCenter);
		QFont nf = nameLabel->font();
		nf.setPointSize(11);
		nameLabel->setFont(nf);
		nameLabel->setWordWrap(false);
		nameLabel->setFixedWidth(ThumbSize);
		nameLabel->setText(nameLabel->fontMetrics().elidedText(
		    info.slotName, Qt::ElideRight, ThumbSize));
		if (isDisabled)
			nameLabel->setStyleSheet("color: #e74c3c;");
		else if (!info.isActive)
			nameLabel->setStyleSheet("color: #777;");

		// ---- Unit badge ------------------------------------------------------
		auto* unitLabel = new QLabel(cell);
		unitLabel->setAlignment(Qt::AlignHCenter);
		QFont uf = unitLabel->font();
		uf.setPointSize(11);
		unitLabel->setFont(uf);
		unitLabel->setText(QString("u%1").arg(info.unitIndex));
		if (isDisabled)
			unitLabel->setStyleSheet("color: #e74c3c;");
		else if (!info.isActive)
			unitLabel->setStyleSheet("color: #555;");

		cellLayout->addWidget(thumbBtn);
		cellLayout->addWidget(nameLabel);
		cellLayout->addWidget(unitLabel);

		_thumbnailGrid->addWidget(cell, row, col);

		if (++col >= ThumbColumns)
		{
			col = 0;
			++row;
		}
	}

	// Pad the last row so the grid doesn't stretch oddly
	if (col > 0)
	{
		for (int c = col; c < ThumbColumns; ++c)
			_thumbnailGrid->addWidget(new QWidget(_thumbnailContainer), row, c);
	}

	_thumbnailContainer->adjustSize();
}

// ---------------------------------------------------------------------------
// populateExtensions
// ---------------------------------------------------------------------------
void TextureDebugPanel::populateExtensions(const QVector<TextureSlotInfo>& slotInfos)
{
	clearLayout(_extensionLayout);

	// Build a quick lookup: unit → extensionEnabled
	// extensionEnabled reflects actual KHR extension activity (scalar factors,
	// not just whether a texture is bound), so extension dots light up even
	// when an extension is active via factor alone (no texture).
	QHash<int, bool> activeUnits;
	for (const TextureSlotInfo& info : slotInfos)
		activeUnits[info.unitIndex] = info.extensionEnabled;

	// Extension definitions: { internal key, display name, relevant unit(s) }
	// Key is the stable identifier used by ViewportWidget::setDebugExtensionEnabled.
	struct ExtDef { QString key; QString name; QVector<int> units; };
	const QVector<ExtDef> extensions = {
		{ "Sheen",                tr("Sheen"),                { 26, 27 } },
		{ "Clearcoat",            tr("Clearcoat"),            { 18, 19, 20 } },
		{ "Iridescence",          tr("Iridescence"),          { 24, 25 } },
		{ "Specular",             tr("Specular"),             { 21, 22 } },
		{ "Anisotropy",           tr("Anisotropy"),           { 23 } },
		{ "Transmission",         tr("Transmission"),         { 28 } },
		{ "Diffuse Transmission", tr("Diffuse Transmission"), { 34, 35 } },
		{ "Volume / SSS",         tr("Volume / SSS"),         { 30 } },
		{ "Volume Scattering",    tr("Volume Scattering"),    { 202 } },  // 202 = hasVolumeScattering scalar marker
		{ "IOR",                  tr("IOR"),                  { 29, 203 } },  // 203 = IOR scalar marker (ior != 1.5)
		{ "Emissive Strength",    tr("Emissive Strength"),    { 200 } },  // 200 = emissiveStrength scalar marker
		{ "Dispersion",           tr("Dispersion"),           { 201 } },  // 201 = dispersion scalar marker
	};

	const int cols = 3;
	int extRow = 0, extCol = 0;
	for (const ExtDef& ext : extensions)
	{
		bool isActive = false;
		for (int u : ext.units)
			if (activeUnits.value(u, false)) { isActive = true; break; }

		const bool isDisabled = _disabledExtensions.contains(ext.key);

		// Build a QToolButton that acts as the indicator and toggle.
		// Inactive extensions: non-interactive grey dot.
		// Active extensions: checkable; checked = disabled by user (amber X).
		auto* btn = new QToolButton(_extensionGroup);
		btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		btn->setIcon(QIcon(dotPixmap(isActive, isDisabled)));
		btn->setIconSize(QSize(14, 14));
		btn->setText(ext.name);
		btn->setAutoRaise(true);

		QFont f = btn->font();
		f.setPointSize(11);
		btn->setFont(f);

		if (!isActive)
		{
			// Extension not in use — show as greyed, no toggle.
			btn->setEnabled(false);
			btn->setStyleSheet("QToolButton { color: #666; }");
			btn->setToolTip(tr("Extension not active on this material"));
		}
		else if (isDisabled)
		{
			// Active but disabled by user.
			btn->setCheckable(true);
			btn->setChecked(true);
			btn->setStyleSheet("QToolButton { color: #e8a838; }");
			btn->setToolTip(tr("Click to re-enable this extension"));
		}
		else
		{
			// Active and running — click to suppress.
			btn->setCheckable(true);
			btn->setChecked(false);
			btn->setStyleSheet("QToolButton { color: #48c774; }");
			btn->setToolTip(tr("Click to disable this extension in the viewport"));
		}

		const QString extKey = ext.key;
		connect(btn, &QToolButton::toggled, this, [this, extKey](bool checked) {
			// checked == true  → disable extension; checked == false → re-enable
			if (checked)
				_disabledExtensions.insert(extKey);
			else
				_disabledExtensions.remove(extKey);

			if (_viewportWidget)
				_viewportWidget->setDebugExtensionEnabled(_currentMeshId, extKey, !checked);

			// Repopulate to update dot icons and styles immediately.
			if (!_lastSlots.isEmpty())
				populateExtensions(_lastSlots);
		});

		_extensionLayout->addWidget(btn, extRow, extCol);

		if (++extCol >= cols)
		{
			extCol = 0;
			++extRow;
		}
	}
}

// ---------------------------------------------------------------------------
// clearDynamicContent
// ---------------------------------------------------------------------------
void TextureDebugPanel::clearDynamicContent()
{
	clearLayout(_thumbnailGrid);
	clearLayout(_extensionLayout);
}

// ---------------------------------------------------------------------------
// showEvent / closeEvent — trigger refresh and save geometry
// ---------------------------------------------------------------------------
void TextureDebugPanel::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);

	// Warn and offer to switch if not in PBR mode.
	updatePBRWarning();
	if (_viewportWidget &&
	    _viewportWidget->getRenderingMode() != RenderingMode::PHYSICALLY_BASED_RENDERING)
	{
		const auto answer = QMessageBox::question(
		    this,
		    tr("Texture Debugger"),
		    tr("The Texture Debugger works best in PBR rendering mode.\n\n"
		       "Switch to PBR now?"),
		    QMessageBox::Yes | QMessageBox::No,
		    QMessageBox::Yes);

		if (answer == QMessageBox::Yes)
		{
			// Emit signal — ModelViewer is connected to onRenderingModeSelected("PBR")
			// so the full activation chain runs: setRenderingMode + setPBRLightingMode
			// + HDR skybox + Realistic display mode + toolbar button sync + updateControls.
			emit requestPBRMode();
			// Warning strip will hide itself via renderingModeChanged signal.
		}
	}

	// Trigger a readback if a mesh was already selected before the panel opened.
	if (_currentMeshId >= 0 && _lastSlots.isEmpty())
		refresh();
}

void TextureDebugPanel::reject()
{
	// Single cleanup point for ALL dismissal paths (Escape, X button, close()).
	// QDialog::closeEvent calls reject() internally, so putting cleanup here
	// avoids the circular close() → closeEvent() → reject() → close() loop.
	if (_viewportWidget)
	{
		_viewportWidget->setGlobalDebugChannel(0);
		if (_currentMeshId >= 0)
			_viewportWidget->clearAllDebugOverrides(_currentMeshId);
	}
	_disabledUnits.clear();
	_disabledExtensions.clear();
	_activeChannelId = 0;
	if (_channelCombo) { QSignalBlocker b(_channelCombo); _channelCombo->setCurrentIndex(0); }
	if (_thumbnailScroll) _thumbnailScroll->setEnabled(true);
	if (_extensionGroup)  _extensionGroup->setEnabled(true);

	saveWindowGeometry();
	QDialog::reject();  // calls hide() — no QCloseEvent generated, no re-entry
}

void TextureDebugPanel::closeEvent(QCloseEvent* event)
{
	// Route through reject() so cleanup always runs regardless of dismissal path.
	// Do NOT call QDialog::closeEvent — it would call reject() again (re-entrant).
	reject();
	event->accept();
}

// ---------------------------------------------------------------------------
// updatePBRWarning
// ---------------------------------------------------------------------------
void TextureDebugPanel::updatePBRWarning()
{
	if (!_pbrWarningLabel || !_viewportWidget) return;
	const bool isPBR =
	    (_viewportWidget->getRenderingMode() == RenderingMode::PHYSICALLY_BASED_RENDERING);
	_pbrWarningLabel->setVisible(!isPBR);
}

// ---------------------------------------------------------------------------
// Geometry persistence
// ---------------------------------------------------------------------------
void TextureDebugPanel::saveWindowGeometry()
{
	QSettings s(QCoreApplication::organizationName(),
	            QCoreApplication::applicationName());
	s.setValue("TextureDebugPanel/geometry", saveGeometry());
}

void TextureDebugPanel::restoreWindowGeometry()
{
	QSettings s(QCoreApplication::organizationName(),
	            QCoreApplication::applicationName());
	const QByteArray geo = s.value("TextureDebugPanel/geometry").toByteArray();
	if (!geo.isEmpty())
		restoreGeometry(geo);
}

// ---------------------------------------------------------------------------
// activeUnits / applyCurrentTextureState
// ---------------------------------------------------------------------------
QSet<int> TextureDebugPanel::activeUnits() const
{
	QSet<int> result;
	for (const TextureSlotInfo& info : _lastSlots)
		if (info.isActive)
			result.insert(info.unitIndex);
	return result;
}

void TextureDebugPanel::applyCurrentTextureState()
{
	if (!_viewportWidget || _currentMeshId < 0) return;

	const QSet<int> all     = activeUnits();
	const QSet<int> enabled = all - _disabledUnits;
	_viewportWidget->applyDebugTextureState(_currentMeshId, enabled, all);
}
