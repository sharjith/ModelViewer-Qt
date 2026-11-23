#include "StartPageWidget.h"

#include "MainWindow.h"
#include "PathUtils.h"
#include "WhatsNew.h"
#include "config.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QStackedLayout>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
	constexpr int kMaxRecentFilesShown = 6;
	constexpr int kTileSize = 150;
	constexpr int kVideoTileWidth = 240;
	constexpr int kVideoThumbHeight = 135; // 16:9, like the thumbnails
	constexpr int kVideoCaptionHeight = 64;
	constexpr int kVideoRowSpacing = 14;
	// Its own setting, on by default: the old Quick Help popup's "show on startup" box may well have been switched off, and
	// that must not hide the page that replaces it.
	constexpr const char* kShowOnStartupKey = "showStartPageOnStartup";

	// The splash artwork, scaled to cover the whole widget.
	class ScaledBackdropWidget : public QWidget
	{
	public:
		explicit ScaledBackdropWidget(const QPixmap& pixmap, QWidget* parent = nullptr)
			: QWidget(parent), _source(pixmap) {}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing, true);
			painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
			QPainterPath clip;
			clip.addRoundedRect(QRectF(rect()), 14, 14);
			painter.setClipPath(clip);
			if (_source.isNull())
			{
				painter.fillRect(rect(), palette().color(QPalette::AlternateBase));
				return;
			}
			// Cover the area but keep the top of the artwork (its headline lines); a short, wide banner crops the bottom.
			const QPixmap scaled = _source.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
			painter.drawPixmap((width() - scaled.width()) / 2, 0, scaled);
		}

	private:
		QPixmap _source;
	};

	// Square, rounded button filled edge to edge with a thumbnail and a translucent label banner along the bottom.
	class SampleTile : public QAbstractButton
	{
	public:
		SampleTile(const QPixmap& pixmap, const QString& label, QWidget* parent = nullptr)
			: QAbstractButton(parent), _pixmap(pixmap)
		{
			setText(label);
			setCursor(Qt::PointingHandCursor);
			setFixedSize(kTileSize, kTileSize);
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
				const QPixmap scaled = _pixmap.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
				painter.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
			}
			else
			{
				painter.fillRect(rect(), palette().color(QPalette::AlternateBase));
			}

			QColor accent = palette().color(QPalette::Highlight);
			if (underMouse() || hasFocus())
			{
				QColor wash = accent;
				wash.setAlpha(60);
				painter.fillRect(rect(), wash);
			}

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

			painter.setPen(QPen((underMouse() || hasFocus()) ? accent : palette().color(QPalette::Mid), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(bounds, 16, 16);
		}

		void enterEvent(QEnterEvent*) override { update(); }
		void leaveEvent(QEvent*) override { update(); }
		void focusInEvent(QFocusEvent* event) override { QAbstractButton::focusInEvent(event); update(); }
		void focusOutEvent(QFocusEvent* event) override { QAbstractButton::focusOutEvent(event); update(); }

	private:
		QPixmap _pixmap;
	};

	// Greedy word wrap into at most maxLines lines, the last one elided.
	QStringList wrapElide(const QString& text, const QFontMetrics& metrics, int width, int maxLines)
	{
		QStringList lines;
		QString current;
		const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (int i = 0; i < words.size(); ++i)
		{
			const QString trial = current.isEmpty() ? words[i] : current + QLatin1Char(' ') + words[i];
			if (metrics.horizontalAdvance(trial) <= width)
			{
				current = trial;
				continue;
			}
			if (!current.isEmpty())
				lines << current;
			current = words[i];
			if (lines.size() == maxLines - 1)
			{
				QString rest = current;
				for (int j = i + 1; j < words.size(); ++j)
					rest += QLatin1Char(' ') + words[j];
				lines << metrics.elidedText(rest, Qt::ElideRight, width);
				return lines;
			}
		}
		if (!current.isEmpty())
			lines << metrics.elidedText(current, Qt::ElideRight, width);
		return lines;
	}

	// A tutorial video: its thumbnail with a play badge, "Tutorial N" and the title under it.
	class VideoTile : public QAbstractButton
	{
	public:
		VideoTile(const QPixmap& thumbnail, int number, const QString& title, const QString& toolTip, QWidget* parent = nullptr)
			: QAbstractButton(parent), _thumbnail(thumbnail), _title(title)
		{
			_label = number == 0 ? QCoreApplication::translate("StartPageWidget", "Trailer")
			                     : QCoreApplication::translate("StartPageWidget", "Tutorial %1").arg(number);
			setCursor(Qt::PointingHandCursor);
			setToolTip(toolTip);
			setAccessibleName(_label + QStringLiteral(": ") + title);
			setFixedSize(kVideoTileWidth, kVideoThumbHeight + kVideoCaptionHeight);
		}

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing, true);
			painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
			const QPalette& pal = palette();
			const bool hot = underMouse() || hasFocus();

			const QRectF thumbRect(0, 0, width() - 1, kVideoThumbHeight - 1);
			painter.save();
			QPainterPath clip;
			clip.addRoundedRect(thumbRect, 10, 10);
			painter.setClipPath(clip);
			if (_thumbnail.isNull())
				painter.fillRect(thumbRect, pal.color(QPalette::AlternateBase));
			else
				painter.drawPixmap(thumbRect.toRect(), _thumbnail);
			if (hot)
			{
				QColor wash = pal.color(QPalette::Highlight);
				wash.setAlpha(50);
				painter.fillRect(thumbRect, wash);
			}
			// Play badge.
			const QPointF centre = thumbRect.center();
			const qreal radius = hot ? 24 : 21;
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(0, 0, 0, hot ? 185 : 140));
			painter.drawEllipse(centre, radius, radius);
			QPainterPath triangle;
			triangle.moveTo(centre.x() - 6, centre.y() - 10);
			triangle.lineTo(centre.x() - 6, centre.y() + 10);
			triangle.lineTo(centre.x() + 11, centre.y());
			triangle.closeSubpath();
			painter.setBrush(Qt::white);
			painter.drawPath(triangle);
			painter.restore();

			painter.setPen(QPen(hot ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(thumbRect, 10, 10);

			QFont labelFont = font();
			labelFont.setBold(true);
			labelFont.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.0));
			const QFontMetrics labelMetrics(labelFont);
			painter.setFont(labelFont);
			painter.setPen(pal.color(QPalette::Highlight));
			painter.drawText(QRect(2, kVideoThumbHeight + 4, width() - 4, labelMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, _label);

			const QFontMetrics titleMetrics(font());
			painter.setFont(font());
			painter.setPen(pal.color(QPalette::WindowText));
			int y = kVideoThumbHeight + 4 + labelMetrics.height() + 1;
			for (const QString& line : wrapElide(_title, titleMetrics, width() - 4, 2))
			{
				painter.drawText(QRect(2, y, width() - 4, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, line);
				y += titleMetrics.height();
			}
		}

		void enterEvent(QEnterEvent*) override { update(); }
		void leaveEvent(QEvent*) override { update(); }
		void focusInEvent(QFocusEvent* event) override { QAbstractButton::focusInEvent(event); update(); }
		void focusOutEvent(QFocusEvent* event) override { QAbstractButton::focusOutEvent(event); update(); }

	private:
		QPixmap _thumbnail;
		QString _title;
		QString _label;
	};

	// One row of the recent-files list: the file name over its folder, the full path as the tooltip.
	class RecentFileButton : public QAbstractButton
	{
	public:
		RecentFileButton(const QString& path, QWidget* parent = nullptr)
			: QAbstractButton(parent), _path(path)
		{
			setCursor(Qt::PointingHandCursor);
			setToolTip(QDir::toNativeSeparators(path));
			setAccessibleName(QFileInfo(path).fileName());
			setAccessibleDescription(QDir::toNativeSeparators(path));
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			setFixedHeight(46);
		}

		QSize sizeHint() const override { return QSize(200, 46); }

	protected:
		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing, true);
			const QPalette& pal = palette();

			if (underMouse() || hasFocus())
			{
				QColor wash = pal.color(QPalette::Highlight);
				wash.setAlpha(45);
				painter.setPen(Qt::NoPen);
				painter.setBrush(wash);
				painter.drawRoundedRect(rect().adjusted(0, 1, 0, -1), 8, 8);
			}

			QFont nameFont = font();
			nameFont.setBold(true);
			QFont folderFont = font();
			folderFont.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.0));
			const QFontMetrics nameMetrics(nameFont);
			const QFontMetrics folderMetrics(folderFont);

			const int left = 12;
			const int available = width() - left - 12;
			const QFileInfo info(_path);

			painter.setFont(nameFont);
			painter.setPen(pal.color(QPalette::WindowText));
			painter.drawText(QRect(left, 5, available, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
				nameMetrics.elidedText(info.fileName(), Qt::ElideMiddle, available));

			QColor dim = pal.color(QPalette::WindowText);
			dim.setAlpha(140);
			painter.setFont(folderFont);
			painter.setPen(dim);
			painter.drawText(QRect(left, 5 + nameMetrics.height() + 1, available, folderMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
				folderMetrics.elidedText(QDir::toNativeSeparators(info.absolutePath()), Qt::ElideMiddle, available));
		}

		void enterEvent(QEnterEvent*) override { update(); }
		void leaveEvent(QEvent*) override { update(); }
		void focusInEvent(QFocusEvent* event) override { QAbstractButton::focusInEvent(event); update(); }
		void focusOutEvent(QFocusEvent* event) override { QAbstractButton::focusOutEvent(event); update(); }

	private:
		QString _path;
	};

	void clearLayout(QLayout* layout)
	{
		while (QLayoutItem* item = layout->takeAt(0))
		{
			if (QWidget* widget = item->widget())
				widget->deleteLater();
			delete item;
		}
	}
}

StartPageWidget::StartPageWidget(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("startPage"));
	setWindowTitle(tr("Start Page"));

	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	_whatsNewDue = WhatsNew::dueOnStartup(settings);

	buildUi();
	applyTheme();
	refreshRecentFiles();
}

void StartPageWidget::buildUi()
{
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);

	// The page fills the whole tab; the scroll area only matters when the window is smaller than the content needs.
	auto* canvas = new QWidget(scroll);
	canvas->setObjectName(QStringLiteral("startCanvas"));
	scroll->setWidget(canvas);
	auto* canvasLayout = new QVBoxLayout(canvas);
	canvasLayout->setContentsMargins(0, 0, 0, 0);
	auto* column = new QWidget(canvas);
	column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	canvasLayout->addWidget(column);

	auto* page = new QVBoxLayout(column);
	page->setContentsMargins(24, 22, 24, 22);
	page->setSpacing(16);

	// A titled panel; returns its content layout.
	auto makePanel = [column, page](const QString& title, int stretch = 0, QBoxLayout* into = nullptr) {
		auto* frame = new QFrame(column);
		frame->setObjectName(QStringLiteral("panel"));
		auto* layout = new QVBoxLayout(frame);
		layout->setContentsMargins(18, 14, 18, 16);
		layout->setSpacing(10);
		auto* heading = new QLabel(title, frame);
		heading->setObjectName(QStringLiteral("panelTitle"));
		layout->addWidget(heading);
		(into ? into : static_cast<QBoxLayout*>(page))->addWidget(frame, stretch);
		return layout;
	};

	// Hero: the splash artwork with a welcome card on it.
	auto* hero = new QFrame(column);
	hero->setObjectName(QStringLiteral("hero"));
	hero->setMinimumHeight(240);
	hero->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	auto* heroStack = new QStackedLayout(hero);
	heroStack->setContentsMargins(0, 0, 0, 0);
	heroStack->setStackingMode(QStackedLayout::StackAll);
	heroStack->addWidget(new ScaledBackdropWidget(QPixmap(QStringLiteral(":/icons/res/Splashscreen.png")), hero));
	auto* heroOverlay = new QWidget(hero);
	auto* heroOverlayLayout = new QVBoxLayout(heroOverlay);
	heroOverlayLayout->setContentsMargins(24, 20, 24, 20);
	heroOverlayLayout->addStretch(1);
	auto* heroCard = new QFrame(heroOverlay);
	heroCard->setObjectName(QStringLiteral("heroCard"));
	heroCard->setFixedWidth(520);
	auto* heroCardLayout = new QVBoxLayout(heroCard);
	heroCardLayout->setContentsMargins(20, 14, 20, 14);
	heroCardLayout->setSpacing(6);
	auto* heroTitle = new QLabel(tr("Welcome to ModelViewer"), heroCard);
	heroTitle->setObjectName(QStringLiteral("heroTitle"));
	auto* heroText = new QLabel(
		tr("Inspect, render and analyse 3D models, CAD data and simulation results. "
		   "Pick up where you left off, open a sample, or take the tutorial."),
		heroCard);
	heroText->setObjectName(QStringLiteral("bodyText"));
	heroText->setWordWrap(true);
	// A word-wrapped label in an aligned overlay does not always get its full wrapped height, so reserve three lines.
	heroText->setMinimumHeight(heroText->fontMetrics().lineSpacing() * 3 + 8);
	heroCardLayout->addWidget(heroTitle);
	heroCardLayout->addWidget(heroText);
	heroOverlayLayout->addWidget(heroCard, 0, Qt::AlignLeft | Qt::AlignBottom);
	heroStack->addWidget(heroOverlay);
	heroStack->setCurrentWidget(heroOverlay);
	page->addWidget(hero, 3);

	// Actions.
	auto* actions = new QFrame(column);
	actions->setObjectName(QStringLiteral("panel"));
	auto* actionsLayout = new QHBoxLayout(actions);
	actionsLayout->setContentsMargins(16, 12, 16, 12);
	actionsLayout->setSpacing(12);
	auto addAction = [this, actions, actionsLayout](const QString& text, bool primary, void (StartPageWidget::*signal)()) {
		auto* button = new QPushButton(text, actions);
		button->setObjectName(primary ? QStringLiteral("primaryAction") : QStringLiteral("secondaryAction"));
		button->setCursor(Qt::PointingHandCursor);
		actionsLayout->addWidget(button, 1);
		connect(button, &QPushButton::clicked, this, signal);
	};
	addAction(tr("Open Model"), true, &StartPageWidget::openModelRequested);
	addAction(tr("Start Tutorial"), false, &StartPageWidget::tutorialRequested);
	addAction(tr("View Shortcuts"), false, &StartPageWidget::shortcutsRequested);
	addAction(tr("Mouse Controls"), false, &StartPageWidget::mouseControlsRequested);
	page->addWidget(actions);

	// Recent files next to the what's-new card.
	auto* middle = new QHBoxLayout();
	middle->setSpacing(16);
	page->addLayout(middle, 4);

	QVBoxLayout* recentPanel = makePanel(tr("Recent Files"), 3, middle);
	_recentLayout = new QVBoxLayout();
	_recentLayout->setSpacing(2);
	recentPanel->addLayout(_recentLayout);
	recentPanel->addStretch(1);

	QVBoxLayout* newPanel = makePanel(tr("What's New in %1").arg(QStringLiteral(APP_VERSION_STRING)), 2, middle);
	if (_whatsNewDue)
		newPanel->parentWidget()->setObjectName(QStringLiteral("panelHighlighted"));
	if (_whatsNewDue)
	{
		auto* badge = new QLabel(tr("NEW - not seen yet"), newPanel->parentWidget());
		badge->setObjectName(QStringLiteral("badge"));
		newPanel->insertWidget(1, badge, 0, Qt::AlignLeft);
	}
	// Headline items; a title that has a tutorial lesson is a link to it.
	struct Highlight { const char* title; const char* text; int lesson; };
	const Highlight highlights[] = {
		{ QT_TR_NOOP("Ray Tracing"), QT_TR_NOOP("CPU and GPU (OptiX) path tracing with denoising and offline export."), 29 },
		{ QT_TR_NOOP("Simulation results"), QT_TR_NOOP("Open, colour, animate, probe and compare FEM and CFD results."), 19 },
		{ QT_TR_NOOP("3D data plotting"), QT_TR_NOOP("Surfaces, scatter, lines, bars, voxels and vector fields from CSV or formulas."), 23 },
		{ QT_TR_NOOP("Measure and annotate"), QT_TR_NOOP("Distances, angles, radii and areas on the model, with a PDF report."), 27 },
		{ QT_TR_NOOP("Mesh tools"), QT_TR_NOOP("Union, shrink-wrap, subdivide, repair, split, merge and generate UVs."), 26 },
		{ QT_TR_NOOP("Analysis"), QT_TR_NOOP("Draft, zebra, curvature and wall thickness checks; mass properties."), 28 },
		{ QT_TR_NOOP("Selection and scenes"), QT_TR_NOOP("Lasso and material selection, scene states and batch rendering."), 28 },
		{ QT_TR_NOOP("Interface"), QT_TR_NOOP("Tabbed toolbars, clipping planes, oblique views, and de/es/fr/it translations."), 0 },
	};
	QString notes;
	for (const Highlight& item : highlights)
	{
		const QString title = tr(item.title).toHtmlEscaped();
		const QString head = item.lesson > 0
			? QStringLiteral("<a href=\"lesson:%1\"><b>%2</b></a>").arg(item.lesson).arg(title)
			: QStringLiteral("<b>%1</b>").arg(title);
		notes += QStringLiteral("<p style=\"margin:0 0 6px 0;\">%1 &mdash; %2</p>").arg(head, tr(item.text).toHtmlEscaped());
	}
	auto* newText = new QLabel(notes, newPanel->parentWidget());
	newText->setObjectName(QStringLiteral("bodyText"));
	newText->setTextFormat(Qt::RichText);
	newText->setWordWrap(true);
	newText->setToolTip(tr("Click a title to open its tutorial lesson."));
	connect(newText, &QLabel::linkActivated, this, [this](const QString& link) {
		if (link.startsWith(QLatin1String("lesson:")))
			emit lessonRequested(link.mid(7).toInt());
	});
	newPanel->addWidget(newText);
	auto* readNew = new QPushButton(tr("Full release notes on GitHub"), newPanel->parentWidget());
	readNew->setObjectName(QStringLiteral("secondaryAction"));
	readNew->setCursor(Qt::PointingHandCursor);
	newPanel->addWidget(readNew, 0, Qt::AlignLeft);
	newPanel->addStretch(1);
	connect(readNew, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(WhatsNew::releaseNotesUrl()); });

	// Tutorial videos (only when the bundled manifest is present).
	if (QWidget* videos = buildVideoPanel(column))
		page->addWidget(videos, 0);

	// Samples.
	QVBoxLayout* samplesPanel = makePanel(tr("Sample Models"), 4);
	struct Sample { const char* icon; const char* label; const char* path; };
	const Sample samples[] = {
		{ ":/icons/res/sample-models/ANC101.png", QT_TR_NOOP("ANC101"), "ANC101.step" },
		{ ":/icons/res/sample-models/CAD.png", QT_TR_NOOP("CAD"), "CAD.brep" },
		{ ":/icons/res/sample-models/CAD-Model.png", QT_TR_NOOP("CAD Model"), "CAD Model v0.step" },
		{ ":/icons/res/sample-models/Skateboard-Assy.png", QT_TR_NOOP("Skateboard Assy"), "Skate-Board-Assy/Skate-Board-Assy.stp" },
		{ ":/icons/res/sample-models/Teapot.png", QT_TR_NOOP("Teapot"), "Tea-Pot/Teapot.obj" },
	};
	auto* sampleRow = new QHBoxLayout();
	sampleRow->setSpacing(12);
	sampleRow->addStretch(1);
	for (const Sample& sample : samples)
	{
		auto* tile = new SampleTile(QPixmap(QString::fromLatin1(sample.icon)), tr(sample.label), column);
		const QString relativePath = QString::fromUtf8(sample.path);
		connect(tile, &QAbstractButton::clicked, this, [this, relativePath]() { emit openSampleRequested(relativePath); });
		sampleRow->addWidget(tile);
		sampleRow->addStretch(1);
	}
	samplesPanel->addStretch(1);
	samplesPanel->addLayout(sampleRow);
	samplesPanel->addStretch(1);

	// First steps.
	QVBoxLayout* stepsPanel = makePanel(tr("Recommended First Steps"), 2);
	const QStringList steps = {
		tr("1. Open a model and press <b>F</b> to fit the full scene."),
		tr("2. Use <b>1 / 2 / 3</b> to switch between Orbit, Fly, and First Person camera modes."),
		tr("3. Right-click in the viewport for common actions like visibility control, transformations, and visualization settings."),
		tr("4. Open <b>Help &gt; Quick Help</b> whenever you need shortcuts, view controls, or tips.")
	};
	for (const QString& step : steps)
	{
		auto* label = new QLabel(step, column);
		label->setObjectName(QStringLiteral("bodyText"));
		label->setWordWrap(true);
		stepsPanel->addWidget(label);
	}

	// Footer.
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	_showOnStartup = new QCheckBox(tr("Show this page on startup"), column);
	_showOnStartup->setChecked(settings.value(kShowOnStartupKey, true).toBool());
	page->addWidget(_showOnStartup);
	connect(_showOnStartup, &QCheckBox::toggled, this, [](bool on) {
		QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		s.setValue(kShowOnStartupKey, on);
	});
}

QWidget* StartPageWidget::buildVideoPanel(QWidget* parent)
{
	const QString dataDir = PathUtils::getDataDirectory() + QStringLiteral("/data/start-page/");
	QFile manifestFile(dataDir + QStringLiteral("videos.json"));
	if (!manifestFile.open(QIODevice::ReadOnly))
		return nullptr;
	const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
	const QJsonArray videos = root.value(QStringLiteral("videos")).toArray();
	if (videos.isEmpty())
		return nullptr;
	const QUrl playlistUrl(root.value(QStringLiteral("playlistUrl")).toString());

	auto* frame = new QFrame(parent);
	frame->setObjectName(QStringLiteral("panel"));
	auto* layout = new QVBoxLayout(frame);
	layout->setContentsMargins(18, 14, 18, 14);
	layout->setSpacing(10);

	auto* header = new QHBoxLayout();
	auto* heading = new QLabel(tr("Video Tutorials"), frame);
	heading->setObjectName(QStringLiteral("panelTitle"));
	header->addWidget(heading);
	header->addStretch(1);
	auto* previous = new QToolButton(frame);
	previous->setText(QStringLiteral("◀"));
	previous->setObjectName(QStringLiteral("arrowButton"));
	previous->setCursor(Qt::PointingHandCursor);
	previous->setToolTip(tr("Previous videos"));
	previous->setAccessibleName(tr("Previous videos"));
	auto* next = new QToolButton(frame);
	next->setText(QStringLiteral("▶"));
	next->setObjectName(QStringLiteral("arrowButton"));
	next->setCursor(Qt::PointingHandCursor);
	next->setToolTip(tr("Next videos"));
	next->setAccessibleName(tr("Next videos"));
	header->addWidget(previous);
	header->addWidget(next);
	if (playlistUrl.isValid())
	{
		auto* all = new QPushButton(tr("Watch all on YouTube"), frame);
		all->setObjectName(QStringLiteral("secondaryAction"));
		all->setCursor(Qt::PointingHandCursor);
		header->addWidget(all);
		connect(all, &QPushButton::clicked, this, [playlistUrl]() { QDesktopServices::openUrl(playlistUrl); });
	}
	layout->addLayout(header);

	auto* scroll = new QScrollArea(frame);
	scroll->setObjectName(QStringLiteral("videoScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	auto* row = new QWidget(scroll);
	row->setObjectName(QStringLiteral("videoRow"));
	auto* rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(kVideoRowSpacing);
	scroll->setWidget(row);

	QStringList credits;
	for (const QJsonValue& value : videos)
	{
		const QJsonObject entry = value.toObject();
		const QString id = entry.value(QStringLiteral("id")).toString();
		if (id.isEmpty())
			continue;
		const QString title = entry.value(QStringLiteral("title")).toString();
		const QString credit = entry.value(QStringLiteral("credit")).toString();
		QString toolTip = title;
		if (!credit.isEmpty())
		{
			toolTip += QLatin1Char('\n') + credit;
			if (!credits.contains(credit))
				credits << credit;
		}
		auto* tile = new VideoTile(QPixmap(dataDir + entry.value(QStringLiteral("thumb")).toString()),
			entry.value(QStringLiteral("number")).toInt(), title, toolTip, row);
		const QUrl url(QStringLiteral("https://www.youtube.com/watch?v=") + id);
		connect(tile, &QAbstractButton::clicked, this, [url]() { QDesktopServices::openUrl(url); });
		rowLayout->addWidget(tile);
	}
	rowLayout->addStretch(1);
	scroll->setFixedHeight(kVideoThumbHeight + kVideoCaptionHeight + scroll->horizontalScrollBar()->sizeHint().height() + 6);
	layout->addWidget(scroll);

	// Step by about two tiles; the scroll bar and Shift + wheel work as usual.
	QScrollBar* bar = scroll->horizontalScrollBar();
	const int step = 2 * (kVideoTileWidth + kVideoRowSpacing);
	connect(previous, &QToolButton::clicked, this, [bar, step]() { bar->setValue(bar->value() - step); });
	connect(next, &QToolButton::clicked, this, [bar, step]() { bar->setValue(bar->value() + step); });

	if (!credits.isEmpty())
	{
		auto* creditLabel = new QLabel(tr("Models shown in the thumbnails:") + QLatin1Char('\n') + credits.join(QLatin1Char('\n')), frame);
		creditLabel->setObjectName(QStringLiteral("creditText"));
		creditLabel->setWordWrap(true);
		layout->addWidget(creditLabel);
	}
	return frame;
}

void StartPageWidget::refreshRecentFiles()
{
	if (!_recentLayout)
		return;
	clearLayout(_recentLayout);

	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	const QStringList files = MainWindow::readRecentFiles(settings);
	QWidget* host = _recentLayout->parentWidget();

	int shown = 0;
	for (const QString& path : files)
	{
		if (shown >= kMaxRecentFilesShown)
			break;
		auto* row = new RecentFileButton(path, host);
		connect(row, &QAbstractButton::clicked, this, [this, path]() { emit openFileRequested(path); });
		_recentLayout->addWidget(row);
		++shown;
	}

	if (shown == 0)
	{
		auto* empty = new QLabel(tr("No recent files yet. Files you open will be listed here."), host);
		empty->setObjectName(QStringLiteral("bodyText"));
		empty->setWordWrap(true);
		_recentLayout->addWidget(empty);
	}
}

void StartPageWidget::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	refreshRecentFiles(); // the list may have changed while another tab was in front
}

void StartPageWidget::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (!_applyingTheme && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange))
		applyTheme();
}

void StartPageWidget::applyTheme()
{
	// The widget-level rules are keyed by object name on purpose: a bare "QFrame" rule would also hit every QLabel (a QFrame
	// subclass) and draw a border round each one.
	_applyingTheme = true;
	const QPalette pal = palette();
	auto hex = [](const QColor& color) {
		return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
	};
	QColor window = pal.color(QPalette::Window);
	QColor base = pal.color(QPalette::Base);
	QColor mid = pal.color(QPalette::Mid);
	QColor text = pal.color(QPalette::WindowText);
	QColor accent = pal.color(QPalette::Highlight);
	QColor accentText = pal.color(QPalette::HighlightedText);
	QColor dim = text;
	dim.setAlpha(190);
	QColor cardBackground = window;
	cardBackground.setAlpha(225);
	QColor accentWash = accent;
	accentWash.setAlpha(35);

	setStyleSheet(QStringLiteral(
		"QWidget#startCanvas { background: %1; }"
		"QFrame#panel { background: %2; border: 1px solid %3; border-radius: 10px; }"
		"QFrame#panelHighlighted { background: %4; border: 1px solid %5; border-radius: 10px; }"
		"QFrame#hero { border: 1px solid %3; border-radius: 14px; }"
		"QFrame#heroCard { background: %6; border: 1px solid %3; border-radius: 12px; }"
		"QLabel#panelTitle { background: transparent; border: none; font-size: 13pt; font-weight: 600; color: %7; }"
		"QLabel#heroTitle { background: transparent; border: none; font-size: 22px; font-weight: 700; color: %7; }"
		"QLabel#bodyText { background: transparent; border: none; font-size: 10.5pt; color: %8; }"
		"QLabel#badge { background: %5; border: none; border-radius: 8px; padding: 1px 8px; font-size: 9pt; font-weight: 700; color: %9; }"
		"QScrollArea#videoScroll, QWidget#videoRow { background: transparent; border: none; }"
		"QToolButton#arrowButton { background: %2; color: %7; border: 1px solid %3; border-radius: 6px; min-width: 28px; min-height: 28px; }"
		"QToolButton#arrowButton:hover { border: 1px solid %5; }"
		"QLabel#creditText { background: transparent; border: none; font-size: 8.5pt; color: %8; }"
		"QPushButton { min-height: 36px; padding: 0 16px; font-size: 11pt; border-radius: 8px; }"
		"QPushButton#primaryAction { background: %5; color: %9; border: none; }"
		"QPushButton#primaryAction:hover { background: %5; border: 2px solid %7; }"
		"QPushButton#secondaryAction { background: %2; color: %7; border: 1px solid %3; }"
		"QPushButton#secondaryAction:hover { border: 1px solid %5; }")
		.arg(hex(window), hex(base), hex(mid), hex(accentWash), hex(accent), hex(cardBackground), hex(text), hex(dim), hex(accentText)));
	_applyingTheme = false;
}
