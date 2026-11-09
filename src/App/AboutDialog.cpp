#include "AboutDialog.h"
#include "config.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <assimp/version.h>
#include <embree4/rtcore_config.h>
#include <Standard_Version.hxx>
#include <CGAL/version.h>

namespace
{
// Width is fixed (not just an initial size) so the logo pixmap below is
// scaled to this exact same value and always fills the dialog edge-to-edge -
// letting the dialog resize horizontally would leave stale letterboxing
// since the pixmap isn't re-scaled on resize.
constexpr int kDialogWidth = 560;
}

AboutDialog::AboutDialog(const QString& graphicsInfo, QWidget* parent)
	: QDialog(parent)
{
	setWindowTitle(tr("About 3D Model Viewer"));
	setupUI(graphicsInfo);

	// Fixed-ish size, not resizable to a screen fraction like QuickHelpDialog -
	// this is a compact info dialog, not a content-heavy reference the user
	// might want to enlarge.
	const QScreen* screen = QApplication::primaryScreen();
	const int maxHeight = screen ? static_cast<int>(screen->geometry().height() * 0.85) : 900;
	resize(kDialogWidth, qMin(maxHeight, 780));
	setFixedWidth(kDialogWidth);
}

void AboutDialog::setupUI(const QString& graphicsInfo)
{
	auto* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	// Smaller version of the splash image the user just replaced (see
	// QuickHelpDialog's Home tab for the same source used full-size as a
	// hero background) - scaled down here instead of shipping a second
	// image asset, so there's only one source to keep in sync.
	_logoLabel = new QLabel(this);
	_logoLabel->setAlignment(Qt::AlignCenter);
	_logoLabel->setScaledContents(false);
	const QPixmap splash(":/icons/res/Splashscreen.png");
	if (!splash.isNull())
	{
		_logoLabel->setPixmap(splash.scaledToWidth(kDialogWidth, Qt::SmoothTransformation));
	}
	mainLayout->addWidget(_logoLabel);

	_detailsBrowser = new QTextBrowser(this);
	_detailsBrowser->setOpenExternalLinks(true);
	_detailsBrowser->setFrameShape(QFrame::NoFrame);
	_detailsBrowser->setHtml(buildDetailsHtml(graphicsInfo));
	mainLayout->addWidget(_detailsBrowser, 1);

	auto* buttonLayout = new QHBoxLayout();
	buttonLayout->setContentsMargins(18, 12, 18, 18);
	auto* noticesButton = new QPushButton(tr("Third-Party Notices"), this);
	connect(noticesButton, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setWindowTitle(tr("Third-Party Notices"));
		auto* layout = new QVBoxLayout(&dialog);
		auto* browser = new QTextBrowser(&dialog);
		browser->setOpenExternalLinks(true);
		QFile notices(QStringLiteral(":/legal/THIRD_PARTY_NOTICES.md"));
		if (notices.open(QIODevice::ReadOnly))
			browser->setMarkdown(QString::fromUtf8(notices.readAll()));
		else
			browser->setPlainText(tr("See THIRD_PARTY_NOTICES.md in the installed data directory."));
		layout->addWidget(browser);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		dialog.resize(720, 520);
		dialog.exec();
	});
	buttonLayout->addWidget(noticesButton);
	buttonLayout->addStretch(1);

	_closeButton = new QPushButton(tr("Close"), this);
	_closeButton->setMinimumWidth(100);
	_closeButton->setDefault(true);
	connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);
	buttonLayout->addWidget(_closeButton);

	mainLayout->addLayout(buttonLayout);
}

QString AboutDialog::buildDetailsHtml(const QString& graphicsInfo) const
{
	// GPU info is gathered once at startup (see MainWindow::graphicsInfo()'s
	// doc comment / main.cpp) as "Label: value\n" lines - reformat into the
	// same table style as the version info below instead of dumping it as
	// raw preformatted text like the old QMessageBox::about() call did.
	QString graphicsRows;
	const QStringList graphicsLines = graphicsInfo.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	for (const QString& line : graphicsLines)
	{
		const int sep = line.indexOf(QLatin1String(": "));
		if (sep < 0)
			continue;
		graphicsRows += QString("<tr><td class='label'>%1</td><td>%2</td></tr>")
			.arg(line.left(sep).toHtmlEscaped(), line.mid(sep + 2).toHtmlEscaped());
	}
	if (graphicsRows.isEmpty())
	{
		graphicsRows = QString("<tr><td colspan='2'>%1</td></tr>")
			.arg(tr("Not available yet - open a document to initialise the viewer."));
	}

	const unsigned int assimpMajor = aiGetVersionMajor();
	const unsigned int assimpMinor = aiGetVersionMinor();
	const unsigned int assimpPatch = aiGetVersionPatch();

	const QString versionRows = QString(
		"<tr><td class='label'>%1</td><td>%2</td></tr>"
		"<tr><td class='label'>%3</td><td>Qt %4</td></tr>"
		"<tr><td class='label'>%5</td><td>%6</td></tr>"
		"<tr><td class='label'>%7</td><td>%8.%9.%10</td></tr>"
		"<tr><td class='label'>%11</td><td>%12</td></tr>"
		"<tr><td class='label'>%13</td><td>%14</td></tr>"
		"<tr><td class='label'>%15</td><td>%16</td></tr>")
		.arg(tr("App Version"), QStringLiteral(APP_VERSION_STRING))
		.arg(tr("Qt Version"), QString::fromLatin1(qVersion()))
		.arg(tr("OpenCASCADE"), QStringLiteral(OCC_VERSION_COMPLETE))
		.arg(tr("Assimp")).arg(assimpMajor).arg(assimpMinor).arg(assimpPatch)
		.arg(tr("CGAL"), QStringLiteral(CGAL_VERSION_STR))
		.arg(tr("Embree"), QStringLiteral(RTC_VERSION_STRING))
		.arg(tr("GPU Path Tracing"),
#ifdef MODELVIEWER_HAVE_OPTIX
			tr("Enabled (NVIDIA OptiX)")
#else
			tr("Disabled (no CUDA/OptiX toolchain found at build time)")
#endif
		);

	return QString(
		"<html><head><style>"
		"body { font-family: Arial, sans-serif; font-size: 10pt; color: #d5dde3; margin: 18px; }"
		"h1 { font-size: 17pt; margin: 0 0 2px 0; }"
		"p.tagline { color: #9fb0bf; margin: 0 0 14px 0; }"
		"p.desc { line-height: 1.5; }"
		"h2 { font-size: 11pt; color: #7fb3d5; margin: 18px 0 6px 0; "
		"     border-bottom: 1px solid #3a4652; padding-bottom: 3px; }"
		"table { border-collapse: collapse; width: 100%; }"
		"td { padding: 3px 6px; vertical-align: top; }"
		"td.label { color: #9fb0bf; white-space: nowrap; width: 1%; padding-right: 14px; }"
		"tbody tr:nth-child(even) { background-color: #2a323a; }"
		"a { color: #6cb6f5; }"
		"p.copyright { color: #9fb0bf; margin-top: 18px; font-size: 9pt; }"
		"</style></head><body>"
		"<h1>%1</h1>"
		"<p class='tagline'>%2</p>"
		"<p class='desc'>%3</p>"
		"<h2>%4</h2>"
		"<table>%5</table>"
		"<h2>%6</h2>"
		"<table>%7</table>"
		"<h2>%12</h2>"
		"<p class='desc'>%13</p>"
		"<h2>%10</h2>"
		"<p class='desc'>%11</p>"
		"<p class='copyright'>%8<br/>"
		"%9 &middot; <a href='mailto:sharjith@gmail.com'>sharjith@gmail.com</a> &middot; "
		"<a href='https://github.com/sharjith/ModelViewer-Qt'>github.com/sharjith/ModelViewer-Qt</a></p>"
		"</body></html>")
		.arg(tr("3D Model Viewer"))
		.arg(tr("A desktop 3D model viewer for CAD and graphics-interchange formats"))
		.arg(tr("Visualizes OBJ and StereoLithography (STL) models via the Assimp library, and "
			"STEP, IGES, and BREP CAD files via the OpenCASCADE library, with real-time "
			"rasterized and offline path-traced rendering."))
		.arg(tr("Version Information"))
		.arg(versionRows)
		.arg(tr("Graphics"))
		.arg(graphicsRows)
		.arg(tr("Copyright © 2021–2026 Sharjith Naramparambath"))
		.arg(tr("Contact"))
		.arg(tr("Third-Party Credits"))
		.arg(tr("Environment maps in the LDRI set: Emil Persson (Humus), CC BY 3.0. "
			"Carbon Fiber material: &quot;Carbon Fibre Ball&quot; by Eric Chadwick, &copy; 2023 Wayfair, via the Khronos glTF Sample Assets, CC BY 4.0. "
			"Most other textures and HDRI maps are CC0 (ambientCG, Poly Haven, 3dtextures.me, TextureCan). "
			"3DXML XML-tessellation reference: cad-in-unity (Apache-2.0); no C# source bundled. "
			"JT reader subset: Open CASCADE JT-Assistant, &copy; OPEN CASCADE SAS, GPL-2.0-or-later (GPLv3 chosen). "
			"JT reference structures/operators: &copy; 2010, 2014 Siemens Product Lifecycle Management Software Inc.; &copy; 2026 Siemens. "
			"Use Third-Party Notices for importer acknowledgements and licence information. "
			"The full list is in CREDITS.md and textures/LICENSES.md in the installation folder."))
		.arg(tr("JT Import"))
		.arg(tr("Supported JT versions: 8.0-8.2, 9.0, 9.5, 10.0, 10.3, 10.5, 10.7, 10.8, 10.9 and 10.10."));
}
