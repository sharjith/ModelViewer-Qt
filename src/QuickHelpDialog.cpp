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
	_tipsBrowser = new QTextBrowser();

	// Set open external links for all browsers
	_mouseControlsBrowser->setOpenExternalLinks(false);
	_keyboardBrowser->setOpenExternalLinks(false);
	_toolbarBrowser->setOpenExternalLinks(false);
	_menuBrowser->setOpenExternalLinks(false);
	_cameraBrowser->setOpenExternalLinks(false);
	_displayBrowser->setOpenExternalLinks(false);
	_advancedBrowser->setOpenExternalLinks(false);
	_tipsBrowser->setOpenExternalLinks(false);

	// Add tabs
	_tabWidget->addTab(_homeTab, tr("Home"));
	_tabWidget->addTab(_mouseControlsBrowser, tr("Mouse Controls"));
	_tabWidget->addTab(_keyboardBrowser, tr("Keyboard Shortcuts"));
	_tabWidget->addTab(_toolbarBrowser, tr("View Toolbar"));
	_tabWidget->addTab(_cameraBrowser, tr("Camera Modes"));
	_tabWidget->addTab(_displayBrowser, tr("Rendering && Display Modes"));
	_tabWidget->addTab(_advancedBrowser, tr("Advanced Features"));
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
		{tr("Shift"), tr("Hold while navigating to move faster in Fly/First Person modes")},
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
		 tr("Show or hide the 3D coordinate axis indicator")}
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

	_advancedBrowser->setHtml(createStyledHtml(tr("Advanced Features"), content));
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

	// Tools Menu
	QList<QStringList> toolsRows = {
		{tr("Tools → Texture Debugger"), tr(""), tr("Open the texture debugger panel")}
	};
	content += createSection(tr("Tools Menu"), "") + createTable(headers, toolsRows);

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
