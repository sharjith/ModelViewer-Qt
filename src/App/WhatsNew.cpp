#include "WhatsNewDialog.h"

#include "config.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
	constexpr int kDialogWidth = 780;
	constexpr const char* kLastShownKey = "whatsNew/lastShownVersion";
	constexpr const char* kShowOnUpdateKey = "whatsNew/showOnUpdate";
}

bool WhatsNewDialog::dueOnStartup(const QSettings& settings)
{
	if (!settings.value(kShowOnUpdateKey, true).toBool())
		return false;
	return settings.value(kLastShownKey).toString() != QStringLiteral(APP_VERSION_STRING);
}

void WhatsNewDialog::markShown()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	settings.setValue(kLastShownKey, QStringLiteral(APP_VERSION_STRING));
}

WhatsNewDialog::WhatsNewDialog(QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("What's New"));
	setModal(false);
	setMinimumWidth(kDialogWidth);
	resize(kDialogWidth, 760);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// The same artwork as the splash screen and the About dialog, so there is one image to keep in step.
	auto* banner = new QLabel(this);
	banner->setAlignment(Qt::AlignCenter);
	const QPixmap artwork(QStringLiteral(":/icons/res/Splashscreen.png"));
	if (!artwork.isNull())
		banner->setPixmap(artwork.scaledToWidth(kDialogWidth, Qt::SmoothTransformation));
	layout->addWidget(banner);

	_browser = new QTextBrowser(this);
	_browser->setFrameShape(QFrame::NoFrame);
	_browser->setOpenLinks(false);
	_browser->setHtml(buildHtml());
	layout->addWidget(_browser, 1);

	auto* footer = new QHBoxLayout();
	footer->setContentsMargins(18, 10, 18, 16);
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	_showOnUpdate = new QCheckBox(tr("Show this window after an update"), this);
	_showOnUpdate->setChecked(settings.value(kShowOnUpdateKey, true).toBool());
	_showOnUpdate->setToolTip(tr("Open What's New once when a new version starts for the first time.\nIt is always available under Help > What's New."));
	footer->addWidget(_showOnUpdate);
	footer->addStretch(1);
	_tutorialButton = new QPushButton(tr("Open Tutorial"), this);
	_helpButton = new QPushButton(tr("Quick Help"), this);
	_closeButton = new QPushButton(tr("Close"), this);
	_closeButton->setDefault(true);
	footer->addWidget(_tutorialButton);
	footer->addWidget(_helpButton);
	footer->addWidget(_closeButton);
	layout->addLayout(footer);

	connect(_showOnUpdate, &QCheckBox::toggled, this, [](bool on) {
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		s.setValue(kShowOnUpdateKey, on);
	});
	connect(_tutorialButton, &QPushButton::clicked, this, [this]() { emit openTutorialRequested(0); });
	connect(_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
		if (url.scheme() == QLatin1String("lesson"))
			emit openTutorialRequested(url.path().toInt());
	});
	connect(_helpButton, &QPushButton::clicked, this, &WhatsNewDialog::openQuickHelpRequested);
	connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

QString WhatsNewDialog::buildHtml() const
{
	const QPalette pal = palette();
	const QString text = pal.color(QPalette::Text).name();
	const QString accent = pal.color(QPalette::Highlight).lighter(130).name();
	const QString muted = pal.color(QPalette::PlaceholderText).name();

	struct Item
	{
		QString title, body, where;
		int lesson; // the tutorial lesson that covers it (0 = none)
	};
	const QList<Item> items = {
		{ tr("Ray Tracing"),
		  tr("A CPU and a GPU (NVIDIA OptiX) ray tracer draws the scene with physically based materials, environment lighting and soft shadows, with denoising and offline export at any size."),
		  tr("Visualization > Ray Tracing..."), 29 },
		{ tr("Simulation results"),
		  tr("Open FEM and CFD results (VTK, CalculiX, Exodus, CGNS, MED, OpenFOAM, VTKHDF), colour them by any field, play the time steps, probe, cut and chart them, and compare two results side by side. ModelViewer displays the results your solver has written; it does not run simulations."),
		  tr("File > Import, Visualization > Simulation Results, and the Simulation Results tab"), 19 },
		{ tr("3D data plotting"),
		  tr("Plot CSV data or formulas as surfaces, contours, lines, scatter, bars, voxels, vector fields and pathlines, on shared linear, log or symlog axes, with text notes and fills."),
		  tr("Visualization > Plot 3D... and the 3D Plot tab"), 23 },
		{ tr("Measure and annotate"),
		  tr("Measure distances, angles, radii, diameters, areas and geodesic distances directly on the model, add annotations and export a PDF report."),
		  tr("Tools > Measure... and Annotate..."), 27 },
		{ tr("Mesh tools"),
		  tr("Union, shrink-wrap, subdivide, reconstruct from points, repair and fill holes, split, merge and group meshes, and generate UVs."),
		  tr("The Tools menu and the Tools toolbar"), 26 },
		{ tr("Analysis"),
		  tr("Check draft angle, zebra stripes, curvature, wall thickness and deviation, and get volume, mass and centre of gravity per mesh and material."),
		  tr("Tools > Surface Analysis... and Mass Properties..."), 28 },
		{ tr("Selection and scenes"),
		  tr("Select with a lasso or by material, colour or box, use the material eyedropper, keep named selection sets and scene states, and render several views in one batch."),
		  tr("The Selection menu and Tools > Batch Render Views..."), 28 },
		{ tr("Interface"),
		  tr("Tabbed toolbars, a seamless navigation panel, clipping planes with a draggable gizmo and a box mode, oblique projections, and German, Spanish, French and Italian translations."),
		  tr("The View and Tools menus"), 0 },
	};

	QString html = QStringLiteral("<html><body style='color:%1; font-size:13px; margin:14px 22px;'>").arg(text);
	html += QStringLiteral("<h2 style='margin-bottom:2px;'>%1</h2>").arg(tr("What's new in ModelViewer %1").arg(QStringLiteral(APP_VERSION_STRING)));
	html += QStringLiteral("<p style='color:%1;'>%2</p>").arg(muted, tr("This is the biggest release so far. The tutorial has a lesson for each of the new areas."));
	for (const Item& item : items)
	{
		html += QStringLiteral("<h3 style='color:%1; margin-bottom:0px;'>%2</h3>").arg(accent, item.title.toHtmlEscaped());
		QString lessonLink;
		if (item.lesson > 0)
			lessonLink = QStringLiteral(" &nbsp;<a href='lesson:%1' style='color:%2;'>%3</a>").arg(item.lesson).arg(accent, tr("Tutorial: lesson %1").arg(item.lesson).toHtmlEscaped());
		html += QStringLiteral("<p style='margin-top:2px;'>%1<br/><span style='color:%2;'>%3</span>%4</p>")
			.arg(item.body.toHtmlEscaped(), muted, item.where.toHtmlEscaped(), lessonLink);
	}
	html += QStringLiteral("</body></html>");
	return html;
}
