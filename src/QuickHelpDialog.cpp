#include "QuickHelpDialog.h"
#include "MainWindow.h"
#include "PathUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QSpacerItem>
#include <QFrame>
#include <QGridLayout>
#include <QPixmap>
#include <QStackedLayout>
#include <QPainter>
#include <QPainterPath>
#include <QAbstractButton>
#include <QEvent>

namespace
{
class ScaledBackdropWidget : public QWidget
{
public:
	explicit ScaledBackdropWidget(const QPixmap& pixmap, QWidget* parent = nullptr)
		: QWidget(parent), _source(pixmap) {}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		QWidget::paintEvent(event);
		QPainter painter(this);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		if (!_source.isNull())
		{
			QPixmap scaled = _source.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const int x = (width() - scaled.width()) / 2;
			const int y = (height() - scaled.height()) / 2;
			painter.drawPixmap(x, y, scaled);
		}
		else
		{
			painter.fillRect(rect(), QColor("#dfe7ef"));
		}
	}

private:
	QPixmap _source;
};

// Square, filleted (rounded-corner) button whose face is filled edge-to-edge
// with a thumbnail image, with its label drawn as a translucent overlay
// banner along the bottom edge instead of living below the icon.
class SampleModelButton : public QAbstractButton
{
public:
	SampleModelButton(const QPixmap& pixmap, const QString& label, QWidget* parent = nullptr)
		: QAbstractButton(parent), _pixmap(pixmap)
	{
		setText(label);
		setCursor(Qt::PointingHandCursor);
		setFixedSize(150, 150);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

		const QRectF bounds = rect().adjusted(0, 0, -1, -1);
		QPainterPath clipPath;
		clipPath.addRoundedRect(bounds, 16, 16);
		painter.setClipPath(clipPath);

		if (!_pixmap.isNull())
		{
			QPixmap scaled = _pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			const int x = (width() - scaled.width()) / 2;
			const int y = (height() - scaled.height()) / 2;
			painter.drawPixmap(x, y, scaled);
		}
		else
		{
			painter.fillRect(rect(), QColor("#dfe7ef"));
		}

		if (underMouse())
			painter.fillRect(rect(), QColor(43, 108, 176, 60));

		const qreal barHeight = 34;
		const QRectF barRect(0, height() - barHeight, width(), barHeight);
		QLinearGradient gradient(0, barRect.top(), 0, barRect.bottom());
		gradient.setColorAt(0, QColor(15, 26, 38, 0));
		gradient.setColorAt(1, QColor(15, 26, 38, 190));
		painter.fillRect(barRect, gradient);

		painter.setClipping(false);

		QFont labelFont = painter.font();
		labelFont.setPointSizeF(10);
		labelFont.setBold(true);
		painter.setFont(labelFont);
		painter.setPen(Qt::white);
		painter.drawText(barRect.adjusted(8, 0, -8, -7), Qt::AlignBottom | Qt::AlignHCenter, text());

		QPen borderPen(underMouse() ? QColor("#2b6cb0") : QColor("#c5d0db"));
		borderPen.setWidth(1);
		painter.setPen(borderPen);
		painter.drawRoundedRect(bounds, 16, 16);
	}

	void enterEvent(QEnterEvent*) override { update(); }
	void leaveEvent(QEvent*) override { update(); }

private:
	QPixmap _pixmap;
};
}

QuickHelpDialog::QuickHelpDialog(QWidget* parent)
	: QDialog(parent)
{
	setupUI();
	setWindowTitle(tr("Quick Help - ModelViewer"));

	// Set dialog size to 70% of screen
	QScreen* screen = QApplication::primaryScreen();
	QRect screenGeometry = screen->geometry();
	int width = static_cast<int>(screenGeometry.width() * 0.7);
	int height = static_cast<int>(screenGeometry.height() * 0.7);
	resize(width, height);
}

void QuickHelpDialog::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	// Create tab widget
	_tabWidget = new QTabWidget(this);
	_homeTab = new QWidget(this);

	// Create browsers for each tab
	_mouseControlsBrowser = new QTextBrowser();
	_keyboardBrowser = new QTextBrowser();
	_toolbarBrowser = new QTextBrowser();
	_menuBrowser = new QTextBrowser();
	_cameraBrowser = new QTextBrowser();
	_displayBrowser = new QTextBrowser();
	_advancedBrowser = new QTextBrowser();
	_measurementBrowser = new QTextBrowser();
	_meshEditingBrowser = new QTextBrowser();
	_tipsBrowser = new QTextBrowser();

	// Set open external links for all browsers
	_mouseControlsBrowser->setOpenExternalLinks(false);
	_keyboardBrowser->setOpenExternalLinks(false);
	_toolbarBrowser->setOpenExternalLinks(false);
	_menuBrowser->setOpenExternalLinks(false);
	_cameraBrowser->setOpenExternalLinks(false);
	_displayBrowser->setOpenExternalLinks(false);
	_advancedBrowser->setOpenExternalLinks(false);
	_measurementBrowser->setOpenExternalLinks(false);
	_meshEditingBrowser->setOpenExternalLinks(false);
	_tipsBrowser->setOpenExternalLinks(false);

	// Add tabs
	_tabWidget->addTab(_homeTab, tr("Home"));
	_tabWidget->addTab(_mouseControlsBrowser, tr("Mouse Controls"));
	_tabWidget->addTab(_keyboardBrowser, tr("Keyboard Shortcuts"));
	_tabWidget->addTab(_toolbarBrowser, tr("View Toolbar"));
	_tabWidget->addTab(_cameraBrowser, tr("Camera Modes"));
	_tabWidget->addTab(_displayBrowser, tr("Rendering && Display Modes"));
	_tabWidget->addTab(_advancedBrowser, tr("Advanced Features"));
	_tabWidget->addTab(_measurementBrowser, tr("Measurement && Annotation"));
	_tabWidget->addTab(_meshEditingBrowser, tr("Mesh Editing"));
	_tabWidget->addTab(_menuBrowser, tr("Menu Shortcuts"));
	_tabWidget->addTab(_tipsBrowser, tr("Tips && Tricks"));

	// Setup content for each tab
	setupHomeTab();
	setupMouseControlsTab();
	setupKeyboardShortcutsTab();
	setupViewToolbarTab();
	setupCameraModesTab();
	setupDisplayModesTab();
	setupAdvancedFeaturesTab();
	setupMeasurementTab();
	setupMeshEditingTab();
	setupMenuShortcutsTab();
	setupTipsAndTricksTab();

	mainLayout->addWidget(_tabWidget);

	auto settings = std::make_shared<QSettings>(
		QCoreApplication::organizationName(),
		QCoreApplication::applicationName()
	);

	// Bottom row: checkbox (left) — stretch — Close (right)
	QHBoxLayout* buttonLayout = new QHBoxLayout();

	_showOnStartupCheckBox = new QCheckBox(tr("Show on startup"), this);
	
	//Read initial value once from the same settings instance
	_showOnStartupCheckBox->setChecked(settings->value("showQuickHelpOnStartup", true).toBool());

	// Capture shared_ptr to reuse the same instance in the writer
	connect(_showOnStartupCheckBox, &QCheckBox::toggled, this, [settings](bool checked) {		
		settings->setValue("showQuickHelpOnStartup", checked);
		});
	
	// Add checkbox first (left)
	buttonLayout->addWidget(_showOnStartupCheckBox);
	// Add a stretch in the middle to push the next widget (Close) to the right
	buttonLayout->addStretch(1);

	_closeButton = new QPushButton(tr("Close"), this);
	_closeButton->setMinimumWidth(100);
	connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);

	// Add Close button (right)
	buttonLayout->addWidget(_closeButton);
	mainLayout->addLayout(buttonLayout);
}

void QuickHelpDialog::setupHomeTab()
{
	auto* homeLayout = new QVBoxLayout(_homeTab);
	homeLayout->setContentsMargins(18, 18, 18, 18);
	homeLayout->setSpacing(16);

	QPixmap banner(":/icons/res/Splashscreen.png");
	auto* heroFrame = new QFrame(_homeTab);
	heroFrame->setMinimumHeight(120);
	heroFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	heroFrame->setFrameShape(QFrame::NoFrame);
	heroFrame->setObjectName("homeHero");
	heroFrame->setStyleSheet(
		"QFrame#homeHero {"
		"  border: 1px solid #d8e1ea;"
		"  border-radius: 14px;"
		"}");

	auto* heroStack = new QStackedLayout(heroFrame);
	heroStack->setContentsMargins(0, 0, 0, 0);
	heroStack->setStackingMode(QStackedLayout::StackAll);

	auto* heroBackground = new ScaledBackdropWidget(banner, heroFrame);
	heroBackground->setMinimumHeight(100);
	heroBackground->setStyleSheet(
		"QWidget {"
		"  border: 1px solid #d8e1ea;"
		"  border-radius: 14px;"
		"  background: #dfe7ef;"
		"}");

	auto* heroOverlay = new QWidget(heroFrame);
	auto* heroLayout = new QVBoxLayout(heroOverlay);
	heroLayout->setContentsMargins(28, 24, 28, 24);
	heroLayout->addStretch(1);

	auto* heroCard = new QFrame(heroFrame);
	heroCard->setStyleSheet(
		"QFrame {"
		"  background: rgba(255, 255, 255, 215);"
		"  border-radius: 12px;"
		"  border: 1px solid rgba(216, 225, 234, 180);"
		"}");
	auto* heroCardLayout = new QVBoxLayout(heroCard);
	heroCardLayout->setContentsMargins(20, 16, 20, 16);
	heroCardLayout->setSpacing(8);

	auto* titleLabel = new QLabel(tr("Welcome to ModelViewer"), heroCard);
	titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	titleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #102a43; background: transparent;");

	auto* introLabel = new QLabel(
		tr("ModelViewer helps you inspect, render, and work with 3D models and CAD data. "
		   "Use the actions below to get started quickly, or explore the help tabs for detailed guidance."),
		heroCard);
	introLabel->setWordWrap(true);
	introLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	introLabel->setStyleSheet("font-size: 11pt; color: #334e68; background: transparent;");

	heroCardLayout->addWidget(titleLabel);
	heroCardLayout->addWidget(introLabel);
	heroLayout->addWidget(heroCard, 0, Qt::AlignLeft | Qt::AlignBottom);
	heroStack->addWidget(heroBackground);
	heroStack->addWidget(heroOverlay);

	homeLayout->addWidget(heroFrame, 1);

	auto* actionFrame = new QFrame(_homeTab);
	actionFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	actionFrame->setFrameShape(QFrame::StyledPanel);
	actionFrame->setStyleSheet(
		"QFrame { background: #f8fafc; border: 1px solid #d8e1ea; border-radius: 10px; }"
		"QPushButton { min-height: 36px; padding: 0 16px; font-size: 11pt; border-radius: 8px; }"
		"QPushButton#primaryAction { background: #2b6cb0; color: white; border: none; }"
		"QPushButton#primaryAction:hover { background: #245c98; }"
		"QPushButton#secondaryAction { background: white; color: #1f2933; border: 1px solid #c5d0db; }"
		"QPushButton#secondaryAction:hover { background: #f3f6f9; }");
	auto* actionLayout = new QHBoxLayout(actionFrame);
	actionLayout->setContentsMargins(16, 12, 16, 12);
	actionLayout->setSpacing(12);

	auto* openModelButton = new QPushButton(tr("Open Model"), actionFrame);
	openModelButton->setObjectName("primaryAction");
	auto* tutorialButton = new QPushButton(tr("Start Tutorial"), actionFrame);
	tutorialButton->setObjectName("secondaryAction");
	auto* shortcutsButton = new QPushButton(tr("View Shortcuts"), actionFrame);
	shortcutsButton->setObjectName("secondaryAction");
	auto* mouseHelpButton = new QPushButton(tr("Mouse Controls"), actionFrame);
	mouseHelpButton->setObjectName("secondaryAction");

	actionLayout->addWidget(openModelButton);
	actionLayout->addWidget(tutorialButton);
	actionLayout->addWidget(shortcutsButton);
	actionLayout->addWidget(mouseHelpButton);

	homeLayout->addWidget(actionFrame, 0);

	auto* sampleModelsFrame = new QFrame(_homeTab);
	sampleModelsFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	sampleModelsFrame->setFrameShape(QFrame::StyledPanel);
	sampleModelsFrame->setStyleSheet(
		"QFrame { background: #ffffff; border: 1px solid #d8e1ea; border-radius: 10px; }"
		"QLabel#sectionTitle { font-size: 13pt; font-weight: 600; color: #243b53; }");
	auto* sampleModelsLayout = new QVBoxLayout(sampleModelsFrame);
	sampleModelsLayout->setContentsMargins(16, 12, 16, 16);
	sampleModelsLayout->setSpacing(10);

	auto* sampleModelsTitle = new QLabel(tr("Sample Models"), sampleModelsFrame);
	sampleModelsTitle->setObjectName("sectionTitle");
	sampleModelsLayout->addWidget(sampleModelsTitle);

	auto* sampleModelsRow = new QHBoxLayout();
	sampleModelsRow->setSpacing(12);

	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/ANC101.png", tr("ANC101"), "ANC101.step"));
	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/CAD.png", tr("CAD"), "CAD.brep"));
	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/CAD-Model.png", tr("CAD Model"), "CAD Model v0.step"));
	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/Skateboard-Assy.png", tr("Skateboard Assy"), "Skate-Board-Assy/Skate-Board-Assy.stp"));
	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/Teapot.png", tr("Teapot"), "Tea-Pot/Teapot.obj"));
	sampleModelsRow->addWidget(createSampleModelButton(sampleModelsFrame,
		":/icons/res/sample-models/Camera.png", tr("Camera"), "Camera/glTF/camera.glb"));

	sampleModelsLayout->addLayout(sampleModelsRow);

	homeLayout->addWidget(sampleModelsFrame, 3);

	auto* firstStepsFrame = new QFrame(_homeTab);
	firstStepsFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	firstStepsFrame->setFrameShape(QFrame::StyledPanel);
	firstStepsFrame->setStyleSheet(
		"QFrame { background: #ffffff; border: 1px solid #d8e1ea; border-radius: 10px; }"
		"QLabel#sectionTitle { font-size: 13pt; font-weight: 600; color: #243b53; }"
		"QLabel#stepText { font-size: 10.5pt; color: #334e68; }");
	auto* firstStepsLayout = new QVBoxLayout(firstStepsFrame);
	firstStepsLayout->setContentsMargins(16, 16, 16, 16);
	firstStepsLayout->setSpacing(10);

	auto* stepsTitle = new QLabel(tr("Recommended First Steps"), firstStepsFrame);
	stepsTitle->setObjectName("sectionTitle");
	firstStepsLayout->addWidget(stepsTitle);

	const QStringList steps = {
		tr("1. Open a model and press <b>F</b> to fit the full scene."),
		tr("2. Use <b>1 / 2 / 3</b> to switch between Orbit, Fly, and First Person camera modes."),
		tr("3. Right-click in the viewport for common actions like visibility control, transformations, and visualization settings."),
		tr("4. Use the tabs in this dialog whenever you need shortcuts, view controls, or tips.")
	};

	for (const QString& step : steps)
	{
		auto* stepLabel = new QLabel(step, firstStepsFrame);
		stepLabel->setObjectName("stepText");
		stepLabel->setWordWrap(true);
		firstStepsLayout->addWidget(stepLabel);
	}

	homeLayout->addWidget(firstStepsFrame, 1);

	connect(openModelButton, &QPushButton::clicked, this, [this]() {
		if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
		{
			QMetaObject::invokeMethod(mw, "on_actionOpen_triggered");
			accept();
		}
	});

	connect(tutorialButton, &QPushButton::clicked, this, [this]() {
		if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
		{
			QMetaObject::invokeMethod(mw, "on_actionTutorial_triggered");
		}
	});

	connect(shortcutsButton, &QPushButton::clicked, this, [this]() {
		_tabWidget->setCurrentWidget(_keyboardBrowser);
	});

	connect(mouseHelpButton, &QPushButton::clicked, this, [this]() {
		_tabWidget->setCurrentWidget(_mouseControlsBrowser);
	});
}

QWidget* QuickHelpDialog::createSampleModelButton(QWidget* parent, const QString& iconResourcePath,
	const QString& label, const QString& relativeModelPath)
{
	auto* button = new SampleModelButton(QPixmap(iconResourcePath), label, parent);

	connect(button, &QAbstractButton::clicked, this, [this, relativeModelPath]() {
		openSampleModel(relativeModelPath);
	});

	return button;
}

void QuickHelpDialog::openSampleModel(const QString& relativeModelPath)
{
	if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
	{
		const QString fullPath = PathUtils::getDataDirectory() + QString("/sample-models/") + relativeModelPath;
		mw->openFile(fullPath);
		accept();
	}
}

void QuickHelpDialog::setupMouseControlsTab()
{
	QStringList headers = { tr("Action"), tr("Mouse Control"), tr("Alternative") };
	QList<QStringList> rows = {
		{tr("Rotate View"), tr("Ctrl + Left Button + Drag"), tr("Enable 'Rotate View' mode from toolbar")},
		{tr("Pan View"), tr("Ctrl + Right Button + Drag"), tr("Enable 'Pan View' mode from toolbar")},
		{tr("Zoom View"), tr("Mouse Wheel<br/>OR<br/>Ctrl + Middle Button + Drag"), tr("Enable 'Zoom View' mode from toolbar")},
		{tr("Center Pan"), tr("Middle Button Click (release at new position)"), tr("N/A")},
		{tr("Select Object"), tr("Left Button Click"), tr("N/A")},
		{tr("Multi-Select"), tr("Left Button + Drag (rubber band)"), tr("Hold Ctrl while clicking")},
		{tr("Window Zoom"), tr("Enable mode, then Left Button + Drag"), tr("Right-click menu")},
		{tr("Context Menu"), tr("Right Button Click"), tr("N/A")}
	};

	QString content = createSection(tr("View Manipulation"),
		tr("The mouse controls allow intuitive 3D view manipulation:")) +
		createTable(headers, rows);

	content += createSection(tr("Important Notes"),
		tr("<ul>"
			"<li><b>Inertia:</b> Mouse movements support inertial scrolling for smooth navigation</li>"
			"<li><b>Cursor Changes:</b> The cursor changes to indicate the active manipulation mode</li>"
			"<li><b>Mode Activation:</b> You can activate view modes from the toolbar or right-click menu, "
			"then use Left Button to perform the action</li>"
			"<li><b>Large Models:</b> For models larger than 50MB, a low-resolution preview is shown during manipulation</li>"
			"</ul>"));

	_mouseControlsBrowser->setHtml(createStyledHtml(tr("Mouse Controls"), content));
}

void QuickHelpDialog::setupKeyboardShortcutsTab()
{
	QString content;

	// View Navigation
	QStringList navHeaders = { tr("Key"), tr("Action") };
	QList<QStringList> navRows = {
		{tr("W, A, S, D / Arrow Keys"), tr("Navigate in current camera mode:<br/>"
							 "• <b>Orbit Mode:</b> Pan view (W=up, S=down, A=left, D=right)<br/>"
							 "• <b>Fly/First Person:</b> Move forward/backward/left/right")},
		{tr("Q, E"), tr("Move up/down (Fly mode only)")},
		{tr("Shift"), tr("Hold an Arrow key or I/K/J/L/N/X/Z while navigating to move 3x faster.<br/>"
							 "• <b>Not on W/A/S/D/Q/E/M:</b> those letters double as Shift shortcuts "
							 "for other things (Show All, display modes) - use the Arrow keys "
							 "instead for faster movement (fully equivalent to W/A/S/D in every "
							 "camera mode)")},
		{tr("I, K"), tr("Rotate view around X-axis (up/down)")},
		{tr("J, L"), tr("Rotate view around Y-axis (left/right)")},
		{tr("M, N"), tr("Rotate view around Z-axis (clockwise/counter-clockwise)")},
		{tr("X, Z"), tr("Zoom in/out (Orbit mode only)")},
		{tr("F"), tr("Fit All - frame entire scene in view")},
		{tr("Ctrl + P"), tr("Toggle between Orthographic and Perspective projection") },
		{tr("Ctrl + M"), tr("Toggle between multi-view and single view")},
		{tr("Ctrl + T"), tr("Top View")},
		{tr("Ctrl + B"), tr("Bottom View")},
		{tr("Ctrl + F"), tr("Front View")},
		{tr("Ctrl + R"), tr("Rear View")},
		{tr("Ctrl + L"), tr("Left View")},
		{tr("Ctrl + J"), tr("Right View")},
		{tr("Home"), tr("Axonometric View")},
		{tr("F"), tr("Fit All")}
	};
	content += createSection(tr("View Navigation"), "") + createTable(navHeaders, navRows);

	// Camera Modes
	QList<QStringList> camRows = {
		{tr("1"), tr("Switch to Orbit camera mode")},
		{tr("2"), tr("Switch to Fly camera mode")},
		{tr("3"), tr("Switch to First Person camera mode")}
	};
	content += createSection(tr("Camera Modes"), "") + createTable(navHeaders, camRows);

	// Selection and Visibility
	QList<QStringList> selRows = {
		{tr("Delete"), tr("Delete selected objects")},
		{tr("Space"), tr("Hide selected objects (or show if swapped)")},
		{tr("Shift + Space"), tr("Show only selected objects")},
		{tr("Alt + S"), tr("Swap visible/hidden objects")},
		{tr("Esc"), tr("Cancel current operation and deselect all")}
	};
	content += createSection(tr("Selection & Visibility"), "") + createTable(navHeaders, selRows);

	// File Operations
	QList<QStringList> fileRows = {
		{tr("Ctrl + I"), tr("Import model into current scene")},
		{tr("Ctrl + E"), tr("Export selected objects")}
	};
	content += createSection(tr("File Operations"), "") + createTable(navHeaders, fileRows);

	content += createSection(tr("Tips"),
		tr("<ul>"
			"<li>Hold keys continuously for smooth navigation</li>"
			"<li>W/A/S/D and the Arrow keys both support navigation</li>"
			"<li>Camera mode affects how movement keys behave</li>"
			"<li>Hold Shift in Fly or First Person mode to sprint</li>"
			"<li>In First Person mode, pitch is limited to ±60 degrees</li>"
			"<li>In Fly mode, pitch is limited to ±89 degrees</li>"
			"</ul>"));

	_keyboardBrowser->setHtml(createStyledHtml(tr("Keyboard Shortcuts"), content));
}

void QuickHelpDialog::setupViewToolbarTab()
{
	QString content;

	QStringList headers = { tr("Button"), tr("Function"), tr("Description") };
	QList<QStringList> rows = {
		{tr("Rotate View"), tr("Activate rotation mode"),
		 tr("Click to enable, then use Left Mouse to rotate the view")},
		{tr("Pan View"), tr("Activate pan mode"),
		 tr("Click to enable, then use Left Mouse to pan the view")},
		{tr("Zoom View"), tr("Activate zoom mode"),
		 tr("Click to enable, then drag Left Mouse vertically to zoom")},
		{tr("Fit All"), tr("Frame scene"),
		 tr("Fits entire scene in the viewport (Shortcut: F)")},
		{tr("Window Zoom"), tr("Zoom to area"),
		 tr("Drag a rectangle to zoom into that specific area")},
		{tr("Camera Modes"), tr("Switch camera type"),
		 tr("Choose between Orbit, Fly, or First Person camera modes<br/>"
			"Shortcuts: 1=Orbit, 2=Fly, 3=First Person")},
		{tr("Orthographic Views"), tr("Standard views"),
		 tr("Quick access to Top, Front, Left, Bottom, Rear, Right views")},
		{tr("Axonometric Views"), tr("3D standard views"),
		 tr("Switch to Isometric, Dimetric, or Trimetric projections")},
		{tr("Projection Toggle"), tr("Ortho ↔ Perspective"),
		 tr("Switch between orthographic and perspective projection")},
		{tr("Multi-View"), tr("Four viewport layout"),
		 tr("Show Top, Front, Right, and Isometric views simultaneously")},
		{tr("Realistic Rendering"), tr("Toggle full PBR look"),
		 tr("Standalone toggle (Shortcut: Shift+R) that layers full material/lighting "
			"detail on top of whichever Display Mode is active")},
		{tr("Display Modes"), tr("Base rendering style"),
		 tr("Choose Shaded (Shift+S), Hollow Mesh (Shift+H), Mesh Edges (Shift+M, shaded + every "
			"triangle edge), Wireframe (Shift+W, feature edges only, no fill), or Shaded with Edges "
			"(Shift+E, shaded + feature edges only)")},
		{tr("Rendering Mode"), tr("Shading model"),
		 tr("Choose ADS (Blinn-Phong) or PBR (Metallic-Roughness) as the underlying lighting model")},
		{tr("Shading Normal Mode"), tr("Normal interpolation"),
		 tr("Choose Flat Shaded (Shift+F) for faceted faces, or Smooth Shaded (Shift+G) for smoothed normals")},
		{tr("Section View"), tr("Clipping planes"),
		 tr("Enable interactive clipping planes for cross-sections")},
		{tr("Swap Visible"), tr("Invert visibility"),
		 tr("Show hidden objects and hide visible ones")},
		{tr("Show/Hide Axis"), tr("Toggle axis display"),
		 tr("Show or hide the 3D coordinate axis indicator")},
		{tr("Lasso Select"), tr("Freeform selection"),
		 tr("Click to arm, then drag a freeform outline around meshes to select them; stays armed "
			"across multiple drags until clicked again")},
		{tr("Turntable"), tr("Auto-rotate camera"),
		 tr("Toggles continuous camera rotation for presentation; stops automatically on any manual "
			"navigation input")}
	};

	content += createSection(tr("Toolbar Buttons"), "") + createTable(headers, rows);

	content += createSection(tr("Auto-Hide Behavior"),
		tr("<ul>"
			"<li>The toolbar automatically appears at the bottom of the viewport</li>"
			"<li>Move mouse to bottom edge to reveal the toolbar</li>"
			"<li>Toolbar hides after 2 seconds of inactivity</li>"
			"<li>Toolbar remains visible when mouse is over it or menus are open</li>"
			"<li>Scroll buttons appear if toolbar is wider than viewport</li>"
			"</ul>"));

	_toolbarBrowser->setHtml(createStyledHtml(tr("View Toolbar"), content));
}

void QuickHelpDialog::setupCameraModesTab()
{
	QString content;

	content += createSection(tr("Orbit Camera Mode (Key: 1)"),
		tr("<p><b>Best for:</b> Examining objects from all angles, CAD-like viewing</p>"
			"<p><b>Behavior:</b></p>"
			"<ul>"
			"<li>Camera orbits around the model center point</li>"
			"<li>Rotation keeps the model in view</li>"
			"<li>Up direction is always maintained</li>"
			"<li><b>W/A/S/D:</b> Pan the view (up/down/left/right)</li>"
			"<li><b>X/Z:</b> Zoom in/out</li>"
			"<li><b>I/K:</b> Rotate around X-axis</li>"
			"<li><b>J/L:</b> Rotate around Y-axis</li>"
			"</ul>"));

	content += createSection(tr("Fly Camera Mode (Key: 2)"),
		tr("<p><b>Best for:</b> Free exploration of large scenes, architectural walkthroughs</p>"
			"<p><b>Behavior:</b></p>"
			"<ul>"
			"<li>Camera moves freely through 3D space</li>"
			"<li>Mouse controls look direction</li>"
			"<li>No restrictions on viewing angle</li>"
			"<li><b>W/S</b> or <b>Up/Down:</b> Move forward/backward in viewing direction</li>"
			"<li><b>A/D</b> or <b>Left/Right:</b> Strafe left/right</li>"
			"<li><b>Q/E:</b> Move down/up vertically</li>"
			"<li><b>Shift:</b> Move faster while navigating</li>"
			"<li><b>Mouse:</b> Look around (pitch limited to ±89°)</li>"
			"</ul>"));

	content += createSection(tr("First Person Camera Mode (Key: 3)"),
		tr("<p><b>Best for:</b> Ground-level exploration, character perspective</p>"
			"<p><b>Behavior:</b></p>"
			"<ul>"
			"<li>Similar to Fly mode but with constraints</li>"
			"<li>Pitch restricted to ±60° (more natural for ground movement)</li>"
			"<li>Typically used for walking simulations</li>"
			"<li><b>W/S</b> or <b>Up/Down:</b> Walk forward/backward on the ground plane</li>"
			"<li><b>A/D</b> or <b>Left/Right:</b> Strafe left/right on the ground plane</li>"
			"<li><b>Shift:</b> Move faster while navigating</li>"
			"<li><b>Mouse:</b> Look around (pitch limited to ±60°)</li>"
			"<li>Note: No vertical Q/E movement in this mode</li>"
			"</ul>"));

	content += createSection(tr("Switching Modes"),
		tr("<p>You can switch between camera modes in several ways:</p>"
			"<ul>"
			"<li>Press <b>1</b>, <b>2</b>, or <b>3</b> on keyboard</li>"
			"<li>Use the Camera Modes button on the View Toolbar</li>"
			"<li>The toolbar button updates to show current mode</li>"
			"</ul>"));

	_cameraBrowser->setHtml(createStyledHtml(tr("Camera Modes"), content));
}

void QuickHelpDialog::setupDisplayModesTab()
{
	QString content;

	QStringList headers = { tr("Display Mode"), tr("Shortcut Key"), tr("Description"), tr("Use Case")};
	QList<QStringList> rows = {
		{tr("Realistic"),
		 tr("Shift + R"),
		 tr("Standalone toggle that layers full PBR material properties, textures, lighting, shadows, "
			"and reflections on top of whichever display mode below is active"),
		 tr("Final presentation, material evaluation, photorealistic visualization")},

		{tr("Shaded"),
		 tr("Shift + S"),
		 tr("Solid colored surfaces with basic lighting (Ambient-Diffuse-Specular model)"),
		 tr("General modeling work, performance, shape evaluation")},

		{tr("Hollow Mesh"),
		 tr("Shift + H"),
		 tr("Shows faces as translucent/hollow shells without solid shading"),
		 tr("Seeing through outer surfaces to inspect internal structure")},

		{tr("Mesh Edges"),
		 tr("Shift + M"),
		 tr("Shows solid filled surfaces with every triangle edge overlaid, revealing the full mesh tessellation"),
		 tr("Inspecting tessellation density, triangle-level topology checking")},

		{tr("Wireframe"),
		 tr("Shift + W"),
		 tr("Shows only true feature edges (crease/boundary edges, or B-Rep edges for CAD formats), no filled surfaces"),
		 tr("Clean edge-only inspection, technical drawings")},

		{tr("Shaded with Edges"),
		 tr("Shift + E"),
		 tr("Combination of shaded surfaces with only true feature edges overlaid (not every triangle edge)"),
		 tr("Modeling work where you need to see both shape and clean topology")}
	};

	content += createSection(tr("Available Display Modes"), "") + createTable(headers, rows);

	content += createSection(tr("Rendering Features"),
		tr("<p>The Realistic mode includes advanced rendering features:</p>"
			"<ul>"
			"<li><b>PBR Materials:</b> Physically Based Rendering with metallic/roughness workflow</li>"
			"<li><b>Image-Based Lighting:</b> Environmental lighting from HDRI maps</li>"
			"<li><b>Shadows:</b> Real-time shadow mapping with adjustable quality</li>"
			"<li><b>Reflections:</b> Environment reflections on surfaces</li>"
			"<li><b>Advanced Materials:</b> Support for transmission, clearcoat, sheen, iridescence, anisotropy</li>"
			"<li><b>HDR & Tone Mapping:</b> High dynamic range with multiple tone mapping algorithms</li>"
			"<li><b>Gamma Correction:</b> Proper color space handling</li>"
			"</ul>"));

	content += createSection(tr("Performance Considerations"),
		tr("<ul>"
			"<li><b>Realistic mode</b> is most demanding - may be slower on complex scenes</li>"
			"<li><b>Shaded mode</b> offers good balance of appearance and performance</li>"
			"<li><b>Wireframe mode</b> is fastest but least visually informative</li>"
			"<li>For large models (>50MB), low-resolution preview is automatically enabled during manipulation</li>"
			"</ul>"));

	QStringList renderingModeHeaders = { tr("Rendering Mode"), tr("Description"), tr("Use Case") };
	QList<QStringList> renderingModeRows = {
		{tr("ADS (Blinn-Phong)"),
		 tr("Classic Ambient-Diffuse-Specular lighting model with a single specular highlight term"),
		 tr("Lightweight shading, non-physical stylized looks, quick previews")},
		{tr("PBR (Metallic-Roughness)"),
		 tr("Physically Based Rendering using the metallic/roughness workflow, driven by material "
			"metallic, roughness, and other PBR factors/textures"),
		 tr("Photorealistic materials, glTF-authored assets, IBL-driven lighting")}
	};
	content += createSection(tr("Rendering Mode"),
		tr("<p>Selected from the Rendering Mode flyout button on the View Toolbar. This chooses the "
			"underlying lighting/shading model used to light every mesh, independent of Display Mode "
			"and the Realistic toggle.</p>")) +
		createTable(renderingModeHeaders, renderingModeRows);

	QStringList shadingNormalHeaders = { tr("Shading Normal Mode"), tr("Shortcut Key"), tr("Description"), tr("Use Case") };
	QList<QStringList> shadingNormalRows = {
		{tr("Flat Shaded"),
		 tr("Shift + F"),
		 tr("Each triangle face uses a single face normal, producing a faceted look with visible edges between faces"),
		 tr("Inspecting actual mesh facets, low-poly/faceted stylistic looks")},
		{tr("Smooth Shaded"),
		 tr("Shift + G"),
		 tr("Interpolates vertex normals across each face, producing a smooth, continuous-looking surface"),
		 tr("Most everyday viewing of organic or curved surfaces")}
	};
	content += createSection(tr("Shading Normal Mode"),
		tr("<p>Selected from the Shading Normal Mode flyout button on the View Toolbar. This controls "
			"how face normals are interpolated for lighting, independent of Display Mode and Rendering Mode.</p>")) +
		createTable(shadingNormalHeaders, shadingNormalRows);

	_displayBrowser->setHtml(createStyledHtml(tr("Rendering & Display Modes"), content));
}

void QuickHelpDialog::setupAdvancedFeaturesTab()
{
	QString content;

	content += createSection(tr("Clipping Planes (Section View)"),
		tr("<p>Cut through a model with up to three axis-aligned clipping planes to see internal "
		   "structure, opened via the Section View button on the View Toolbar.</p>"
		   "<ul>"
		   "<li><b>XY / YZ / ZX:</b> Enable each plane independently; each has its own 'Flip' toggle "
		   "to reverse which side is cut away</li>"
		   "<li><b>Coefficient:</b> A numeric field per plane that positions it along its axis</li>"
		   "<li><b>Capping:</b> Fills the cut cross-section with a solid cap instead of leaving it hollow</li>"
		   "<li><b>Dynamic Capping:</b> Recomputes the cap as you move or animate the model, rather than "
		   "only when you release the plane</li>"
		   "<li><b>Hatch Pattern:</b> Choose Diagonal 45/135, Horizontal, Vertical, Grid, or Cross Hatch "
		   "for the capped cross-section, with adjustable tiling, color, and an optional texture</li>"
		   "<li><b>Reset Coefficients / Reset All:</b> Quickly return planes to their default position "
		   "or clear all clipping state</li>"
		   "</ul>"));

	content += createSection(tr("Exploded Views"),
		tr("<p>Pull an assembly's parts apart to inspect how components relate, without altering the "
		   "real model. Opened from the Exploded View panel.</p>"
		   "<ul>"
		   "<li><b>Assembly / Anchor:</b> Choose which parts explode and which part stays fixed as the anchor</li>"
		   "<li><b>Explosion Mode:</b> Auto (Radial), Axis X/Y/Z, or a Custom Vector direction</li>"
		   "<li><b>Distance Slider:</b> Controls how far apart the parts spread, as a percentage</li>"
		   "<li><b>Manual Placement:</b> Use the on-screen transform gizmo to hand-position specific parts "
		   "into a staged exploded pose, without changing their real transform</li>"
		   "<li><b>Capture Steps:</b> Record multiple exploded poses in sequence and reorder them to build "
		   "a staged, multi-part reveal</li>"
		   "<li><b>Presets:</b> Save a full exploded configuration by name and switch between layouts instantly</li>"
		   "<li><b>Animation:</b> Play captured steps in parallel, sequentially, or as separate animation clips; "
		   "exportable to glTF/GLB</li>"
		   "</ul>"));

	content += createSection(tr("Transform Gizmo"),
		tr("<p>An interactive on-screen handle for translating, rotating, and scaling a selection directly "
		   "in the viewport, shown via the right-click context menu or the Transformations panel.</p>"
		   "<ul>"
		   "<li><b>Translate:</b> Drag an axis arrow (X, Y, or Z) to move along that axis</li>"
		   "<li><b>Rotate:</b> Drag a rotation ring (XY, YZ, or ZX) to rotate around that plane</li>"
		   "<li><b>Scale:</b> Drag the center handle to resize uniformly</li>"
		   "<li>The gizmo scales itself relative to camera distance so its handles stay usable at any zoom level</li>"
		   "<li>The same gizmo is reused during Exploded View manual placement to stage poses non-destructively</li>"
		   "</ul>"));

	content += createSection(tr("Morph Target (Blend Shape) Animation"),
		tr("<p>Models imported from glTF/GLB that include morph targets (blend shapes) can smoothly "
		   "deform between vertex-position variants — commonly used for facial expressions or organic "
		   "deformation that rigid transforms and skeletal rigs alone can't produce.</p>"
		   "<ul>"
		   "<li>Morph weights are driven by animation clips, played back through the Animations panel "
		   "(Play/Pause, Loop, Speed) just like any other clip</li>"
		   "<li>Morph target data is fully preserved when saving to <b>.mvf</b>, and re-injected on export "
		   "back to glTF/GLB</li>"
		   "</ul>"));

	content += createSection(tr("Lasso Selection"),
		tr("<p>A freeform-polygon alternative to click/rubber-band selection, armed via the Lasso Select "
		   "button on the View Toolbar.</p>"
		   "<ul>"
		   "<li>Click the toolbar button to arm it, then drag a freeform outline in the viewport - every "
		   "mesh whose center falls inside the outline is selected when you release</li>"
		   "<li>Stays armed across multiple drags until you click the button again (or press Esc), unlike "
		   "Window Zoom's one-shot gesture</li>"
		   "<li>Hold Shift while dragging to add to the current selection instead of replacing it</li>"
		   "<li>Plain click and rubber-band selection still work normally whenever Lasso isn't armed</li>"
		   "</ul>"));

	content += createSection(tr("Filter by Material"),
		tr("<p>Opened via Selection → Filter by Material..., this lists every distinct material actually "
		   "in use in the scene and lets you select every mesh using it.</p>"
		   "<ul>"
		   "<li>Groups by each mesh's <b>current</b> material, not where it was imported from - a mesh "
		   "re-materialed with the Eyedropper below is grouped by what it looks like now</li>"
		   "<li>Multi-select rows with Ctrl/Shift-click - the live viewport selection updates as the union "
		   "of every checked material</li>"
		   "<li><b>Show Only</b> / <b>Hide</b> act immediately on whatever the list currently has selected, "
		   "so you can isolate or hide a material as soon as you find it</li>"
		   "</ul>"));

	content += createSection(tr("Filter by Color"),
		tr("<p>Opened via Selection → Filter by Color..., this builds a list of target colors and selects "
		   "every mesh whose color falls within a shared tolerance of any of them.</p>"
		   "<ul>"
		   "<li>Three ways to add a color: the <b>+ Add Color...</b> button opens the OS color picker; the "
		   "<b>eyedropper</b> button lets you click meshes directly in the viewport for an exact match (no "
		   "guessing - screen-sampling a rendered pixel rarely lands close enough to a mesh's true stored "
		   "color); <b>Auto-Detect Colors in Scene</b> seeds the list with every distinct color already in "
		   "the scene, so you can start from everything and prune what you don't want with each row's "
		   "own × button</li>"
		   "<li>Each listed color shows its own live match count, so you can see at a glance whether a "
		   "color you added is actually catching anything</li>"
		   "<li><b>Match Tolerance</b> is shared across every listed color - raise it if a picked/sampled "
		   "color isn't quite matching</li>"
		   "<li><b>Show Only</b> / <b>Hide</b> act on the combined result, same as Filter by Material</li>"
		   "</ul>"));

	content += createSection(tr("Material Eyedropper / Brush"),
		tr("<p>Copies one mesh's material onto others, armed from the eyedropper button in the Material "
		   "Properties panel.</p>"
		   "<ul>"
		   "<li>Click the button, then click a source mesh to sample its material - the cursor switches to "
		   "a brush icon</li>"
		   "<li>Click or drag across target meshes to apply the sampled material - every mesh touched "
		   "during one stroke is batched into a single undo step</li>"
		   "<li>Stays armed after a stroke finishes, so you can keep applying the same sampled material</li>"
		   "</ul>"));

	content += createSection(tr("Named Selection Sets"),
		tr("<p>Save the current selection under a name and recall it later, from the Selections panel or "
		   "Selection → Save Selection Set...</p>"
		   "<ul>"
		   "<li>Recalling a set reveals any of its members that are currently hidden, so the set always "
		   "shows what you saved even if visibility has changed since</li>"
		   "<li>A set that references a since-deleted mesh gracefully skips that entry on recall instead of "
		   "failing</li>"
		   "<li>Saved sets are persisted with the document</li>"
		   "</ul>"));

	content += createSection(tr("Turntable"),
		tr("<p>A standalone View Toolbar toggle for continuous camera auto-rotation, useful for "
		   "presentation or demo purposes.</p>"
		   "<ul>"
		   "<li>Works independently of whichever camera mode (Orbit/Fly/First Person) is active</li>"
		   "<li>Stops automatically the instant you rotate, pan, zoom, or otherwise navigate manually</li>"
		   "<li>Always off when a document is first opened - it's a transient presentation setting, not "
		   "saved with the document</li>"
		   "</ul>"));

	_advancedBrowser->setHtml(createStyledHtml(tr("Advanced Features"), content));
}

void QuickHelpDialog::setupMeasurementTab()
{
	QString content;

	content += createSection(tr("Measure & Annotate"),
		tr("<p>Opened via Tools → Measure... and Tools → Annotate... (no toolbar button or keyboard "
		   "shortcut for either) - the two tools are mutually exclusive, arming one disarms the other.</p>"
		   "<ul>"
		   "<li>Creating, deleting, and repositioning a measurement or annotation is undoable; the "
		   "per-item visibility checkbox in either dialog's list is not (same convention as mesh "
		   "visibility in the Scene Tree)</li>"
		   "<li>Both are saved only in this app's native <b>.mvf</b> session format, not exported to "
		   "glTF/GLB (neither format has a native concept of a measurement or annotation)</li>"
		   "<li>A measurement is resolved live against current mesh geometry, so it stays correct if you "
		   "move/transform a mesh afterward</li>"
		   "</ul>"));

	QStringList measureHeaders = { tr("Tool"), tr("What It Measures"), tr("Picks"), tr("Notes") };

	QList<QStringList> pointDistanceRows = {
		{tr("Point"), tr("The 3D coordinates of a single point"), tr("1 click"), tr("Works on any mesh")},
		{tr("Distance"), tr("Straight-line distance between two points"), tr("2 clicks"),
		 tr("Points may be on different meshes/files")},
		{tr("Geodesic Distance"), tr("Distance measured ALONG the surface between two points (e.g. "
		 "wrapping around a curved part), not straight-line"), tr("2 clicks"),
		 tr("Both points must land on the SAME mesh")},
		{tr("3-Point Angle"), tr("The angle (0-180°) at a picked vertex, between rays to two other "
		 "picked points"), tr("3 clicks"), tr("Works on any mesh")}
	};
	content += createSection(tr("Point & Distance"), "") + createTable(measureHeaders, pointDistanceRows);

	QList<QStringList> arcsCirclesRows = {
		{tr("3-Point Arc Radius"), tr("Radius/center of a circular arc, fit through three picked points "
		 "on its rim"), tr("3 clicks"), tr("Works on any mesh")},
		{tr("Center + 2-Point Arc Radius"), tr("Radius of an arc/hole from a picked center plus two "
		 "points on the rim"), tr("3 clicks"),
		 tr("STEP/IGES/BREP: center snaps to the exact analytic center (works for through-holes too). "
		 "glTF/OBJ: center must land on real geometry - won't work on a through-hole's center")},
		{tr("Edge Radius"), tr("Exact radius/center/axis of a circular edge (hole or boss rim)"),
		 tr("1 click"), tr("STEP/IGES/BREP only - not available on glTF/OBJ meshes")},
		{tr("Pitch Circle"), tr("Diameter of the best-fit circle through 3+ hole centers (a bolt-hole "
		 "pattern), plus the angular gap between adjacent holes"), tr("3+ clicks, then Enter/Finish"),
		 tr("Center-snapping works best on STEP/IGES/BREP; falls back to a plain surface pick on glTF/OBJ")},
		{tr("Concentricity"), tr("Whether two holes/bosses share the same axis - distance between centers "
		 "and angle between axes"), tr("2 clicks"),
		 tr("STEP/IGES/BREP only - not available on glTF/OBJ meshes")},
		{tr("Cylindrical/Conical Diameter"), tr("Diameter of a cylindrical or conical surface at the "
		 "picked point (varies along a cone's length)"), tr("1 click on the curved surface, not its rim"),
		 tr("STEP/IGES/BREP uses the exact surface axis; glTF/OBJ uses a validated local fit. Has its own "
		 "options panel - see below")}
	};
	content += createSection(tr("Arcs & Circles"), "") + createTable(measureHeaders, arcsCirclesRows);

	content += createSection(tr("Cylindrical Diameter Options"),
		tr("<p>Shown only while Cylindrical/Conical Diameter is the active tool - tunes the mesh-fit/"
		   "region-growing path used on non-CAD or fit-based cases (session-only, not saved with the "
		   "document):</p>"
		   "<ul>"
		   "<li><b>Angle tolerance:</b> max angle between a candidate point's normal and the fitted "
		   "cylinder's radial direction to join the region (app default 35°, CGAL's own default is 25°)</li>"
		   "<li><b>Min region size:</b> minimum accepted point count for a fitted region (app default 24, "
		   "CGAL's own default is 3)</li>"
		   "<li><b>Min/Max diameter (0 = no limit):</b> reject a fit smaller/larger than these bounds</li>"
		   "</ul>"));

	QList<QStringList> facesRows = {
		{tr("Face to Face"), tr("Perpendicular distance between two near-parallel faces, or the angle "
		 "(0-90°) between them otherwise"), tr("2 clicks"), tr("Works on any mesh")},
		{tr("Point to Face"), tr("Perpendicular distance from a picked point to a picked face's "
		 "(infinite) plane"), tr("2 clicks"), tr("Works on any mesh")},
		{tr("Face Area"), tr("Surface area of a face - the picked triangle plus every triangle connected "
		 "to it and coplanar with it"), tr("1 click"), tr("Works on any mesh")},
		{tr("Minimum Distance"), tr("True closest-point distance between two faces/surfaces - each pick "
		 "expands to its whole smooth region"), tr("2 clicks"),
		 tr("May be picked on the same mesh (e.g. a wall-thickness check) or two different ones; can take "
		 "a moment on a very large, finely-tessellated face")}
	};
	content += createSection(tr("Faces"), "") + createTable(measureHeaders, facesRows);

	QList<QStringList> edgesRows = {
		{tr("Edge Length"), tr("Length of a single edge (straight or curved)"), tr("1 click"),
		 tr("Works on any mesh")},
		{tr("Edge to Vertex"), tr("Perpendicular distance from a picked vertex/point to a picked edge's "
		 "(infinite) line"), tr("2 clicks"), tr("Works on any mesh")},
		{tr("Edge to Edge"), tr("Perpendicular distance between two near-parallel edges, or the angle "
		 "(0-90°) between them otherwise"), tr("2 clicks"),
		 tr("Also handles skew, non-intersecting edges (angle-only result)")},
		{tr("Edge to Face"), tr("Perpendicular distance from an edge to a face's plane, or the angle "
		 "(0-90°) between them otherwise"), tr("2 clicks"), tr("Works on any mesh")},
		{tr("Chain Length"), tr("Total length of a connected run of edges - an open chain (e.g. a weld "
		 "seam) or a closed perimeter/loop"), tr("2+ clicks, then Enter/Finish"),
		 tr("Each new pick must share an endpoint with the chain so far - a disconnected edge is rejected")}
	};
	content += createSection(tr("Edges"), "") + createTable(measureHeaders, edgesRows);

	content += createSection(tr("Annotation"),
		tr("<p>A free-text sticky note anchored to a point on a mesh surface, connected to a draggable "
		   "text label by a leader line.</p>"
		   "<ul>"
		   "<li>Opens unarmed (still useful for reviewing/editing existing notes) - click <b>Place "
		   "Note</b> to arm it</li>"
		   "<li>Click a point on the model to place a note there - it's auto-selected with default text "
		   "\"New Note\" so you can immediately type over it; stays armed for placing more notes in a "
		   "row</li>"
		   "<li>Select a note from the results list (or click it in the viewport) to edit its text, or "
		   "<b>Delete</b> it - multi-select delete batches into one undo step</li>"
		   "<li>Drag a note's text frame to reposition just the label/leader, independent of its anchor "
		   "point</li>"
		   "</ul>"));

	content += createSection(tr("Export Report"),
		tr("<p>Opened via Tools → Export Report..., produces a PDF built from captured camera views plus "
		   "an optional measurement/annotation summary table.</p>"
		   "<ul>"
		   "<li>The views list is pulled from the Cameras panel's own <b>Capture View</b> button - "
		   "capture a view there first, it then appears here automatically</li>"
		   "<li><b>Include measurement/annotation table</b> appends an HTML table listing every "
		   "measurement and annotation's text to the PDF</li>"
		   "<li>Double-click a captured view to override which specific measurements/annotations THAT "
		   "view's screenshot shows, independent of the document's real visibility - <b>Use Current "
		   "Visibility</b> clears the override</li>"
		   "<li>Export restores the document's real visibility and the viewport's camera exactly as they "
		   "were before, once finished - nothing is left toggled or parked on a captured view</li>"
		   "</ul>"));

	_measurementBrowser->setHtml(createStyledHtml(tr("Measurement & Annotation"), content));
}

void QuickHelpDialog::setupMeshEditingTab()
{
	QString content;

	content += createSection(tr("Shrink Wrap"),
		tr("<p>Opened via Tools → Shrink Wrap..., combines one or more selected meshes into a single new "
		   "watertight, 2-manifold shell using CGAL's alpha wrapping - the inputs don't need to share a "
		   "material or even be manifold themselves.</p>"
		   "<ul>"
		   "<li><b>Alpha</b> / <b>Offset:</b> the two numeric fields controlling how tightly the shell "
		   "wraps and how far it's offset from the input surface; <b>Reset to Suggested</b> computes "
		   "sensible starting values from the selection's bounding box</li>"
		   "<li>Always produces a brand-new mesh node - the original selection is left untouched</li>"
		   "</ul>"));

	content += createSection(tr("Reconstruct Surface"),
		tr("<p>Opened via Tools → Reconstruct Surface..., builds a new triangulated surface from the "
		   "point positions of one or more selected meshes/point clouds via CGAL's advancing-front "
		   "reconstruction - existing faces are ignored, only point positions matter.</p>"
		   "<ul>"
		   "<li><b>Sharpness:</b> lower is smoother/rounder, higher preserves sharper edges</li>"
		   "<li><b>Boundary Tolerance:</b> how large a gap the reconstruction may bridge</li>"
		   "<li><b>Simplify point cloud before reconstruction:</b> optional, reveals a <b>Target "
		   "Spacing</b> field that merges points closer than that distance first - speeds up large/noisy "
		   "scans at the cost of fine detail</li>"
		   "</ul>"));

	content += createSection(tr("Repair Mesh"),
		tr("<p>Opened via Tools → Repair Mesh..., runs each mesh in the working list independently "
		   "through CGAL's repair toolkit: duplicate/degenerate geometry, non-manifold vertices, "
		   "inconsistent winding, and self-intersections. It's defect cleanup only - it never fills holes "
		   "or forces closure, so an intentionally open panel stays open.</p>"
		   "<ul>"
		   "<li>A mesh already reported valid is skipped, no new node is created for it</li>"
		   "<li><b>Self-intersection resolution attempts:</b> how many smoothing/hole-refill rounds to "
		   "try (CGAL's own default is 7)</li>"
		   "<li><b>Try smoothing-based resolution too:</b> adds a slower smoothing-based strategy "
		   "alongside the default hole-filling-based one</li>"
		   "</ul>"));

	content += createSection(tr("Fill Holes"),
		tr("<p>Opened via Tools → Fill Holes..., detects every boundary loop (potential hole) across the "
		   "meshes in the working list and lets you interactively choose which loops are genuine defects "
		   "vs. an intentionally open edge, before patching only the checked ones via CGAL's "
		   "triangulate-and-refine-hole.</p>"
		   "<ul>"
		   "<li>Selecting a detected-hole row highlights that loop in orange in the viewport</li>"
		   "<li><b>Patch density:</b> how fine the new patch's triangulation is relative to the "
		   "surrounding mesh</li>"
		   "<li>Shares the same self-intersection resolution options as Repair Mesh above</li>"
		   "</ul>"));

	content += createSection(tr("Subdivide Surface"),
		tr("<p>Opened via Tools → Subdivide Surface..., this smooths one or more selected meshes using "
		   "CGAL's Loop or Catmull-Clark subdivision — each selected mesh is refined independently, added "
		   "as a new mesh alongside the original (which is left untouched), and becomes undoable once the "
		   "dialog closes.</p>"
		   "<ul>"
		   "<li><b>Method:</b> Loop stays triangle-based throughout; Catmull-Clark produces quads internally "
		   "and is triangulated back afterward — both usually look similarly smooth</li>"
		   "<li><b>Iterations:</b> each step roughly quadruples the triangle count, so higher values get "
		   "expensive fast; 1-2 is enough to see the effect</li>"
		   "<li><b>Preserve sharp edges</b> (on by default): keeps any edge with a 30-degree-or-greater bend "
		   "infinitely sharp instead of smoothing it away — a cylinder's flat end caps and a block's corners "
		   "stay crisp while the rest of the surface still smooths normally. Turn it off for the classic "
		   "fully-smooth subdivision-surface look (the same way subdividing a cube yields a rounded blob, "
		   "not a cube with a finer mesh) - useful on coarse/organic meshes where an all-over rounding "
		   "effect is what's actually wanted</li>"
		   "<li><b>Replace previous result:</b> when checked, each Generate click replaces the prior "
		   "preview; unchecked, results accumulate side by side</li>"
		   "</ul>"));

	content += createSection(tr("Shared \"Replace Previous Result\" Convention"),
		tr("<p>Shrink Wrap, Reconstruct Surface, Repair Mesh, Fill Holes, and Subdivide Surface all share "
		   "one checkbox, checked by default: when checked, each Generate click undoably deletes the "
		   "prior click's result before creating the new one, so at most one live result accumulates per "
		   "source mesh. Unchecked, results accumulate side by side instead.</p>"));

	content += createSection(tr("Mesh Operations (Right-Click Menu)"),
		tr("<p>Available from the tree/viewport right-click context menu when meshes are selected - all "
		   "add new mesh node(s) and remove the originals, and are undoable except Select Parent (a pure "
		   "navigation helper).</p>"));

	QStringList opsHeaders = { tr("Operation"), tr("What It Does"), tr("Selection Requirement") };
	QList<QStringList> opsRows = {
		{tr("Split by Connectivity"), tr("Splits each selected mesh into its disconnected pieces, one "
		 "new mesh per piece. A mesh already a single connected piece is left untouched"),
		 tr("One or more meshes, each evaluated independently")},
		{tr("Merge by Adjacency"), tr("Groups the selection into touching clusters (by shared vertex "
		 "position) and merges each touching cluster into one mesh; non-touching meshes are left alone"),
		 tr("2+ meshes; only touching subgroups are merged")},
		{tr("Merge Selected"), tr("Combines the whole selection into one new mesh by plain concatenation, "
		 "regardless of whether the meshes are touching"), tr("2+ meshes")},
		{tr("Mesh Union"), tr("Attempts a real CGAL boolean union across the selection's repaired "
		 "geometry; silently falls back to plain concatenation (same as Merge Selected) if repair or "
		 "corefinement fails"), tr("2+ meshes")},
		{tr("Group"), tr("Pure scene-graph reorganization - creates a new Group node and moves the "
		 "selected meshes into it. No geometry is touched"), tr("One or more meshes")},
		{tr("Select Parent"), tr("Selects the tree parent of the right-clicked item - navigation only, no "
		 "geometry change"), tr("The single right-clicked item")}
	};
	content += createTable(opsHeaders, opsRows);

	content += createSection(tr("Mixed-Material Merges"),
		tr("<p>Merge by Adjacency, Merge Selected, and Mesh Union all prompt when a touching group/"
		   "selection has more than one material: <b>Keep Materials Separate</b> splits that group into "
		   "one merge per material instead of combining everything into one with the first mesh's "
		   "material, or choose <b>Merge Anyway</b> to combine regardless.</p>"));

	_meshEditingBrowser->setHtml(createStyledHtml(tr("Mesh Editing"), content));
}

void QuickHelpDialog::setupMenuShortcutsTab()
{
	QString content;

	QStringList headers = { tr("Menu"), tr("Shortcut"), tr("Action") };

	// File Menu
	QList<QStringList> fileRows = {
		{tr("File → New"), tr("Ctrl+N"), tr("Create new viewer session")},
		{tr("File → Open"), tr("Ctrl+O"), tr("Open a 3D model file")},
		{tr("File → Import"), tr("Ctrl+I"), tr("Import model into current scene")},
		{tr("File → Export"), tr("Ctrl+E"), tr("Export selected objects")},
		{tr("File → Save"), tr("Ctrl+S"), tr("Save current scene")},
		{tr("File → Save As"), tr("Ctrl+Shift+S"), tr("Save scene with new name")},
		{tr("File → Close"), tr(""), tr("Close current document")},
		{tr("File → Exit"), tr(""), tr("Exit application")}
	};
	content += createSection(tr("File Menu"), "") + createTable(headers, fileRows);

	// Edit Menu
	QList<QStringList> editRows = {
		{tr("Edit → Undo"), tr("Ctrl+Z"), tr("Undo last operation")},
		{tr("Edit → Redo"), tr("Ctrl+Y"), tr("Redo previously undone operation")},
		{tr("Edit → Settings"), tr(""), tr("Open the settings dialog")}
	};
	content += createSection(tr("Edit Menu"), "") + createTable(headers, editRows);

	// Selection Menu
	QList<QStringList> selectionRows = {
		{tr("Selection → Filter by Material..."), tr(""), tr("Select every mesh in the scene using a chosen material")},
		{tr("Selection → Filter by Color..."), tr(""), tr("Select every mesh whose color matches a chosen target, within a tolerance")},
		{tr("Selection → Save Selection Set..."), tr(""), tr("Save the current selection under a name, for quick recall later")}
	};
	content += createSection(tr("Selection Menu"), "") + createTable(headers, selectionRows);

	// Tools Menu
	QList<QStringList> toolsRows = {
		{tr("Tools → Measure..."), tr(""), tr("Open the measurement tool - point, distance, and arc-radius tools among others")},
		{tr("Tools → Annotate..."), tr(""), tr("Open the annotation tool - place text notes anchored to points on the model")},
		{tr("Tools → Export Report..."), tr(""), tr("Export captured views and the measurement/annotation list as a PDF report")},
		{tr("Tools → Shrink Wrap..."), tr(""), tr("Combine the selected meshes into one new watertight shell")},
		{tr("Tools → Subdivide Surface..."), tr(""), tr("Smooth the selected meshes via CGAL subdivision")},
		{tr("Tools → Reconstruct Surface..."), tr(""), tr("Reconstruct a triangulated surface from the selected point cloud(s)")},
		{tr("Tools → Repair Mesh..."), tr(""), tr("Fix defects (non-manifold vertices, self-intersections, etc.) on the selected meshes")},
		{tr("Tools → Fill Holes..."), tr(""), tr("Detect and interactively patch boundary-loop holes in the selected meshes")},
		{tr("Tools → Generate UVs..."), tr(""), tr("Generate UV coordinates for meshes using a chosen projection method")}
	};
	content += createSection(tr("Tools Menu"), "") + createTable(headers, toolsRows);

	// Visualization Menu
	QList<QStringList> visualizationRows = {
		{tr("Visualization → Texture Debugger"), tr(""), tr("Open the texture debugger panel")}
	};
	content += createSection(tr("Visualization Menu"), "") + createTable(headers, visualizationRows);

	// Window Menu
	QList<QStringList> windowRows = {
		{tr("Window → Next"), tr(""), tr("Switch to next document window")},
		{tr("Window → Previous"), tr(""), tr("Switch to previous document window")}
	};
	content += createSection(tr("Window Menu"), "") + createTable(headers, windowRows);

	// Context Menu (Right-Click)
	content += createSection(tr("Right-Click Context Menu"),
		tr("<p>Right-clicking in the viewport provides quick access to common operations:</p>"));

	QList<QStringList> contextRows = {
		{tr("When object selected:"), "", tr("")},
		{tr("  Center Screen"), tr(""), tr("Center view on selected object")},
		{tr("  Center Object List"), tr(""), tr("Scroll object list to selected item")},
		{tr("  Hide/Show"), tr("Space"), tr("Toggle visibility of selected objects")},
		{tr("  Show Only"), tr("Shift+Space"), tr("Show only selected, hide all others")},
		{tr("  Visualization Settings"), tr(""), tr("Open material/appearance settings")},
		{tr("  Transformations"), tr(""), tr("Open transformation panel (move/rotate/scale)")},
		{tr("  Generate UVs"), tr(""), tr("Auto-generate texture coordinates")},
		{tr("  Duplicate"), tr(""), tr("Create copy of selected objects")},
		{tr("  Delete"), tr("Delete"), tr("Remove selected objects")},
		{tr("  Mesh Info"), tr(""), tr("Display detailed mesh information")},
		{tr("  Select Parent"), tr(""), tr("Select the tree parent of the right-clicked item")},
		{tr("  Split by Connectivity"), tr(""), tr("Split each selected mesh into its disconnected pieces")},
		{tr("  Merge by Adjacency"), tr(""), tr("Merge only the touching clusters within the selection")},
		{tr("  Merge Selected"), tr(""), tr("Combine the whole selection into one mesh")},
		{tr("  Mesh Union"), tr(""), tr("Combine the selection via a real CGAL boolean union")},
		{tr("  Group"), tr(""), tr("Move the selected meshes into a new group node")},
		{tr(""), "", tr("")},
		{tr("When no selection:"), "", tr("")},
		{tr("  Fit All"), tr("F"), tr("Frame entire scene")},
		{tr("  Zoom Area"), tr(""), tr("Enable window zoom mode")},
		{tr("  Select/Zoom/Pan/Rotate"), tr(""), tr("Activate view manipulation modes")},
		{tr("  Show All"), tr("Shift+A"), tr("Make all objects visible")},
		{tr("  Hide All"), tr("Alt+A"), tr("Hide all objects")},
		{tr("  Swap Visible"), tr("Alt+S"), tr("Invert visibility of all objects")},
		{tr("  Background Color"), tr(""), tr("Change viewport background color")}
	};
	content += createTable({ tr("Action"), tr("Shortcut"), tr("Description") }, contextRows);

	_menuBrowser->setHtml(createStyledHtml(tr("Menu Shortcuts"), content));
}

void QuickHelpDialog::setupTipsAndTricksTab()
{
	QString content;

	content += createSection(tr("Getting Started"),
		tr("<ul>"
			"<li><b>Opening Files:</b> Drag and drop files directly onto the window, or use File → Open</li>"
			"<li><b>First View:</b> Press 'F' to frame your model perfectly in the viewport</li>"
			"<li><b>Quick Navigation:</b> Use Middle Mouse for rotation, Mouse Wheel for zoom, Right Mouse for pan</li>"
			"<li><b>Recent Files:</b> Access recently opened files from File → Recent menu</li>"
			"</ul>"));

	content += createSection(tr("Selection Techniques"),
		tr("<ul>"
			"<li><b>Single Select:</b> Left-click on an object in the viewport</li>"
			"<li><b>Multi-Select:</b> Drag a rubber band rectangle around multiple objects</li>"
			"<li><b>Toggle Selection:</b> Click on an already selected object to deselect it</li>"
			"<li><b>Select from List:</b> Use the object list panel on the left side</li>"
			"<li><b>Search Objects:</b> Use the search box above the object list to filter by name</li>"
			"<li><b>Lasso Select:</b> Arm it from the View Toolbar for a freeform selection outline</li>"
			"<li><b>Filter by Material/Color:</b> Use the Selection menu to select every mesh matching a "
			"material or color across the whole scene</li>"
			"<li><b>Named Selection Sets:</b> Save a selection under a name from the Selections panel to "
			"recall it instantly later</li>"
			"</ul>"));

	content += createSection(tr("Working with Visibility"),
		tr("<ul>"
			"<li><b>Hide Selected:</b> Press Space to temporarily hide objects you don't need</li>"
			"<li><b>Isolate:</b> Press Shift+Space to focus on selected objects only</li>"
			"<li><b>Swap Visible:</b> Press Alt+S to see what's hidden (and hide what's visible)</li>"
			"<li><b>Show All:</b> Press Shift+A to bring everything back</li>"
			"<li><b>Visual Indicator:</b> Hidden objects are grayed out in the object list</li>"
			"</ul>"));

	content += createSection(tr("View Organization"),
		tr("<ul>"
			"<li><b>Multiple Sessions:</b> File → New creates additional viewer windows</li>"
			"<li><b>Window Layouts:</b> Use Window menu to tile or cascade multiple documents</li>"
			"<li><b>Multi-View Mode:</b> Enable from toolbar to see four viewports simultaneously</li>"
			"<li><b>Standard Views:</b> Use toolbar buttons for instant Top/Front/Side views</li>"
			"<li><b>Axonometric Views:</b> Choose Isometric/Dimetric/Trimetric for technical drawings</li>"
			"</ul>"));

	content += createSection(tr("Performance Tips"),
		tr("<ul>"
			"<li><b>Large Models:</b> Automatic low-res preview during manipulation for models >50MB</li>"
			"<li><b>Display Mode:</b> Switch to Shaded or Wireframe for better performance</li>"
			"<li><b>Progressive Loading:</b> Large files load progressively with status updates</li>"
			"<li><b>Shadow Quality:</b> Adjust in Environment settings if shadows are slow</li>"
			"<li><b>Hidden Objects:</b> Hidden objects are still in memory but not rendered</li>"
			"</ul>"));

	content += createSection(tr("Materials and Appearance"),
		tr("<ul>"
			"<li><b>Visualization Settings:</b> Right-click object → Visualization Settings</li>"
			"<li><b>Material Editor:</b> Use the left panel to edit colors, roughness, metallic properties</li>"
			"<li><b>Texture Mapping:</b> Apply textures through the Texture Mapping panel</li>"
			"<li><b>Environment:</b> Enable SkyBox and IBL for realistic lighting</li>"
			"<li><b>Display Modes:</b> Switch to Realistic mode to see full PBR materials</li>"
			"<li><b>Eyedropper:</b> Sample one mesh's material and brush it onto others from the Material "
			"Properties panel</li>"
			"</ul>"));

	content += createSection(tr("Advanced Features"),
		tr("<ul>"
			"<li><b>Clipping Planes:</b> Use Section View to cut through models and see internals</li>"
			"<li><b>Transformations:</b> Move, rotate, scale objects individually or in groups</li>"
			"<li><b>Floor Plane:</b> Enable in Environment settings for shadow casting and reflections</li>"
			"<li><b>Shadows:</b> Toggle real-time shadows in Environment settings</li>"
			"<li><b>Window Zoom:</b> Zoom precisely into a specific region of interest</li>"
			"<li><b>UV Generation:</b> Auto-generate texture coordinates for objects without UVs</li>"
			"</ul>"));

	content += createSection(tr("Measuring & Documenting"),
		tr("<ul>"
			"<li><b>Measure:</b> Tools → Measure... for point, distance, arc-radius, and other precision "
			"CAD measurements</li>"
			"<li><b>Annotate:</b> Tools → Annotate... to pin text notes to specific points on the model</li>"
			"<li><b>Capture Views First:</b> Capture camera views on the Cameras tab before Export Report "
			"needs them - the report's view list is pulled from there</li>"
			"</ul>"));

	content += createSection(tr("Troubleshooting"),
		tr("<ul>"
			"<li><b>Lost Objects:</b> Press 'F' to fit all, or check if objects are hidden</li>"
			"<li><b>Stuck in Mode:</b> Press Esc to cancel any active operation</li>"
			"<li><b>Can't Select:</b> Make sure you're not in a view manipulation mode (check cursor)</li>"
			"<li><b>Black Screen:</b> Check display mode and lighting settings</li>"
			"<li><b>Slow Performance:</b> Try switching to Shaded mode or hiding some objects</li>"
			"</ul>"));

	content += createSection(tr("Customization"),
		tr("<ul>"
			"<li><b>Settings:</b> Edit → Settings to configure MSAA, anisotropic filtering, and theme</li>"
			"<li><b>Background:</b> Right-click → Background Color to customize viewport background</li>"
			"<li><b>Theme:</b> Choose between Light, Dark, or System theme in Settings</li>"
			"<li><b>Language:</b> Change interface language in Settings dialog</li>"
			"<li><b>Axis Position:</b> Configure corner axis triad position in Settings</li>"
			"</ul>"));

	_tipsBrowser->setHtml(createStyledHtml(tr("Tips & Tricks"), content));
}

QString QuickHelpDialog::createStyledHtml(const QString& title,
	const QString& content)
{
	QString html = QString(
		"<!DOCTYPE html>"
		"<html>"
		"<head>"
		"<style>"
		"body { font-family: Arial, sans-serif; font-size: 10pt; margin: 10px; }"

		"h1 { color: #2c3e50; font-size: 18pt; "
		"     border-bottom: 2px solid #3498db; padding-bottom: 5px; }"

		"h2 { color: #34495e; font-size: 14pt; "
		"     margin-top: 20px; margin-bottom: 10px; }"

		/* Table styling */
		"table { border-collapse: collapse; width: 100%%; margin: 10px 0; }"

		"th { background-color: #3498db; color: white; "
		"     border: 1px solid #ccc; "
		"     padding: 8px; text-align: left; font-weight: bold; }"

		"td { border: 1px solid #ddd; padding: 8px; vertical-align: top; }"

		"tbody tr:nth-child(even) { background-color: #f2f2f2; }"
		"tbody tr:hover { background-color: #e8f4f8; }"

		"ul { margin-left: 20px; }"
		"li { margin-bottom: 8px; line-height: 1.6; }"

		"code { background-color: #f4f4f4; padding: 2px 6px; "
		"       border-radius: 3px; font-family: 'Courier New', monospace; }"

		"kbd { background-color: #eef2f5; color: #2c3e50; "
		"      border: 1px solid #bfc7ce; "
		"      border-radius: 4px; "
		"      padding: 2px 6px; "
		"      font-family: 'Courier New', monospace; "
		"      font-size: 9pt; "
		"      white-space: nowrap; }"

		"p { line-height: 1.6; margin: 10px 0; }"
		".section { margin-bottom: 25px; }"
		"</style>"
		"</head>"
		"<body>"
		"<h1>%1</h1>"
		"%2"
		"</body>"
		"</html>"
	).arg(title, content);

	return html;
}


QString QuickHelpDialog::createSection(const QString& heading, const QString& content)
{
	if (content.isEmpty())
		return QString("<h2>%1</h2>").arg(heading);

	return QString("<div class='section'><h2>%1</h2>%2</div>").arg(heading, content);
}

QString QuickHelpDialog::createTable(const QStringList& headers,
	const QList<QStringList>& rows)
{
	QString table =
		"<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">";

	// Column widths
	table +=
		"<colgroup>"
		"  <col style=\"width:18%\">"
		"  <col style=\"width:14%\">"
		"  <col style=\"width:38%\">"
		"  <col style=\"width:30%\">"
		"</colgroup>";

	// Find shortcut column index
	int shortcutCol = -1;
	for (int i = 0; i < headers.size(); ++i)
	{
		if (headers[i] == tr("Shortcut Key") ||
			headers[i] == tr("Shortcut") ||
			headers[i] == tr("Key") ||
			headers[i] == tr("Button") ||
			headers[i] == tr("Mouse Control"))
		{
			shortcutCol = i;
			break;
		}
	}

	// Headers
	table += "<tr>";
	for (const QString& header : headers)
	{
		table += QString("<th>%1</th>").arg(header);
	}
	table += "</tr>";

	// Rows
	for (const QStringList& row : rows)
	{
		table += "<tr>";

		for (int i = 0; i < row.size(); ++i)
		{
			QString cell = row[i];

			if (i == shortcutCol && shortcutCol != -1)
				cell = QString("<kbd>%1</kbd>").arg(cell);

			table += QString("<td>%1</td>").arg(cell);
		}

		table += "</tr>";
	}

	table += "</table>";
	return table;
}
