#include "PathUtils.h"
#include "TutorialDialog.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScreen>
#include <QTextStream>
#include <QVBoxLayout>

#ifdef HAVE_WEBENGINE
// ============================================================================
// TutorialWebPage Implementation
// ============================================================================

TutorialWebPage::TutorialWebPage(QObject* parent)
    : QWebEnginePage(parent)
{
}

bool TutorialWebPage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame)
{
    // Intercept link clicks and emit signal instead of navigating
    if (type == QWebEnginePage::NavigationTypeLinkClicked && isMainFrame)
    {
        emit linkClicked(url);
        return false; // Prevent automatic navigation
    }

    // Allow initial page loads and other navigation types
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}
#endif

// ============================================================================
// TutorialDialog Implementation
// ============================================================================

TutorialDialog::TutorialDialog(QWidget* parent)
    : QDialog(parent)
    , _currentListIndex(0)
{
    setupUI();
#ifndef HAVE_WEBENGINE
    populateLessonList();
#endif
    setWindowTitle(tr("ModelViewer Tutorial"));

    // Set dialog size to 80% of screen
    QScreen* screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int width = static_cast<int>(screenGeometry.width() * 0.8);
    int height = static_cast<int>(screenGeometry.height() * 0.8);
    resize(width, height);

    // Select index page by default (first item)
#ifdef HAVE_WEBENGINE
    loadIndexPage();
#else
    _lessonList->setCurrentRow(0);
#endif
}

void TutorialDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Content viewer
#ifdef HAVE_WEBENGINE
    qDebug() << "Using QWebEngineView for tutorial display";

    _webView = new QWebEngineView();
    _webPage = new TutorialWebPage(this);
    _webView->setPage(_webPage);

    // Connect custom page's linkClicked signal. Must be queued: it's emitted from inside
    // acceptNavigationRequest(), which is itself called synchronously from Chromium's
    // navigation machinery. onLinkClicked() ends up calling _webView->setUrl() to load the
    // next lesson - starting a new navigation reentrantly on the same page, before
    // acceptNavigationRequest() has even returned, crashes QtWebEngine. Queuing defers the
    // navigation to the next event loop iteration, after the original request has resolved.
    connect(_webPage, &TutorialWebPage::linkClicked,
        this, &TutorialDialog::onLinkClicked, Qt::QueuedConnection);

    mainLayout->addWidget(_webView);
#else
    qDebug() << "Using QTextBrowser for tutorial display";

    // Create splitter for lesson list and content
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // Lesson list on the left
    _lessonList = new QListWidget();
    _lessonList->setMaximumWidth(300);
    _lessonList->setStyleSheet(
        "QListWidget {"
        "    border: 1px solid palette(mid);"
        "    border-radius: 4px;"
        "    background-color: palette(base);"
        "    color: palette(text);"
        "}"
        "QListWidget::item {"
        "    padding: 10px;"
        "    border-bottom: 1px solid palette(midlight);"
        "}"
        "QListWidget::item:selected {"
        "    background-color: palette(highlight);"
        "    color: palette(highlighted-text);"
        "}"
        "QListWidget::item:hover {"
        "    background-color: palette(alternate-base);"
        "}"
    );

    _textBrowser = new QTextBrowser();
    _textBrowser->setOpenExternalLinks(false);

    // Set search paths for relative resources (images, CSS)
    QString basePath = getTutorialBasePath();
    _textBrowser->setSearchPaths(QStringList() << basePath);
    _textBrowser->setStyleSheet(R"(
    QTextBrowser {
        background-color: white;
        color: black;
    }
    QTextBrowser a {
        color: #0066cc;
    }
)");


    // Connect QTextBrowser's anchorClicked signal
    connect(_textBrowser, &QTextBrowser::anchorClicked,
        this, &TutorialDialog::onLinkClicked);

    splitter->addWidget(_lessonList);
    splitter->addWidget(_textBrowser);
    splitter->setStretchFactor(0, 0); // Lesson list doesn't stretch
    splitter->setStretchFactor(1, 1); // Content stretches

    mainLayout->addWidget(splitter);
#endif

    // Navigation buttons at bottom
    QHBoxLayout* buttonLayout = new QHBoxLayout();

#ifndef HAVE_WEBENGINE
    _previousButton = new QPushButton(tr("◀ Previous"), this);
    _previousButton->setMinimumWidth(120);
    buttonLayout->addWidget(_previousButton);
#endif
    buttonLayout->addStretch();

    _closeButton = new QPushButton(tr("Close"), this);
    _closeButton->setMinimumWidth(100);

    buttonLayout->addWidget(_closeButton);
    buttonLayout->addStretch();

#ifndef HAVE_WEBENGINE
    _nextButton = new QPushButton(tr("Next ▶"), this);
    _nextButton->setMinimumWidth(120);
    buttonLayout->addWidget(_nextButton);
#endif

    mainLayout->addLayout(buttonLayout);

    // Connect signals
#ifndef HAVE_WEBENGINE
    connect(_lessonList, &QListWidget::currentItemChanged,
        this, &TutorialDialog::onLessonSelected);
    connect(_previousButton, &QPushButton::clicked,
        this, &TutorialDialog::onPreviousClicked);
    connect(_nextButton, &QPushButton::clicked,
        this, &TutorialDialog::onNextClicked);
#endif
    connect(_closeButton, &QPushButton::clicked,
        this, &QDialog::close);
}

#ifndef HAVE_WEBENGINE
void TutorialDialog::populateLessonList()
{
    QStringList items;

    // Add index/home at the top
    items << tr("📚 Tutorial Home");

    // Add all lessons
    items << tr("1. Getting Started")
        << tr("2. Opening Models")
        << tr("3. Basic Navigation")
        << tr("4. Selecting Objects")
        << tr("5. View Modes")
        << tr("6. Camera Modes")
        << tr("7. Display Modes")
        << tr("8. Manipulating Objects")
        << tr("9. Materials & Textures")
        << tr("10. Lighting & Environment")
        << tr("11. Working with Visibility")
        << tr("12. Advanced Features")
        << tr("13. Performance Optimization")
        << tr("14. Tips & Workflows")
        << tr("15. Exploded Views")
        << tr("16. Morph Target Animation")
        << tr("17. Node Transform Editing")
        << tr("18. Edge & Wireframe Rendering");

    _lessonList->addItems(items);
}
#endif

QString TutorialDialog::getTutorialBasePath() const
{
    return PathUtils::getDataDirectory() + "/data/tutorials";
}

QString TutorialDialog::getLessonPath(int lessonIndex) const
{
    QString basePath = getTutorialBasePath();

    if (lessonIndex == -1)
    {
        // Index page
        return basePath + "/index.html";
    }

    // Regular lesson (1-18)
    return basePath + QString("/lesson%1.html").arg(lessonIndex, 2, 10, QChar('0'));
}

QString TutorialDialog::getLessonTitle(int lessonIndex) const
{
    if (lessonIndex == -1)
    {
        return tr("Tutorial Home");
    }

    QStringList titles = {
        "Getting Started", "Opening Models", "Basic Navigation", "Selecting Objects",
        "View Modes", "Camera Modes", "Display Modes", "Manipulating Objects",
        "Materials & Textures", "Lighting & Environment", "Working with Visibility",
        "Advanced Features", "Performance Optimization", "Tips & Workflows",
        "Exploded Views", "Morph Target Animation", "Node Transform Editing",
        "Edge & Wireframe Rendering"
    };

    if (lessonIndex >= 1 && lessonIndex <= titles.size())
    {
        return titles[lessonIndex - 1];
    }
    return QString();
}

QString TutorialDialog::loadHtmlFile(const QString& filename)
{
    QString basePath = getTutorialBasePath();
    QString filePath = basePath + "/" + filename;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "Failed to open HTML file:" << filePath;
        return QString();
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();

    return content;
}

void TutorialDialog::showError(const QString& title, const QString& message)
{
    QString errorHtml = QString(
        "<html><head><style>"
        "body { font-family: 'Segoe UI', sans-serif; padding: 40px; }"
        "h1 { color: #e74c3c; }"
        "code { background: #f8f9fa; padding: 10px; border-radius: 5px; display: block; margin: 10px 0; }"
        "</style></head><body>"
        "<h1>%1</h1>"
        "<p>%2</p>"
        "</body></html>"
    ).arg(title, message);

#ifdef HAVE_WEBENGINE
    _webView->setHtml(errorHtml);
#else
    _textBrowser->setHtml(errorHtml);
#endif
}

void TutorialDialog::loadIndexPage()
{
    QString indexPath = getLessonPath(-1);
    QFile indexFile(indexPath);

    if (!indexFile.exists())
    {
        QString errorMsg = QString(
            "The tutorial index page could not be found:<br/><code>%1</code>"
            "<p>Please ensure the tutorial files are installed correctly in:<br/>"
            "<code>%2</code></p>"
        ).arg(indexPath, getTutorialBasePath());

        showError(tr("Index Not Found"), errorMsg);
        return;
    }

    // Load index page
#ifdef HAVE_WEBENGINE
    QUrl indexUrl = QUrl::fromLocalFile(indexPath);
    _webView->setUrl(indexUrl);
    qDebug() << "Loading index page via QWebEngineView:" << indexUrl.toString();
#else
    QUrl indexUrl = QUrl::fromLocalFile(indexPath);
    _textBrowser->setSource(indexUrl);
    qDebug() << "Loading index page via QTextBrowser:" << indexPath;
#endif

    _currentListIndex = 0;
#ifndef HAVE_WEBENGINE
    updateNavigationButtons();
#endif
}

void TutorialDialog::loadLesson(int listIndex)
{
    if (listIndex < 0 || listIndex >= TOTAL_LIST_ITEMS)
    {
        return;
    }

    // Index 0 = Tutorial Home/Index page
    if (listIndex == 0)
    {
        loadIndexPage();
        return;
    }

    // Convert list index to lesson number (1-18)
    int lessonNumber = listIndex;  // listIndex 1 = lesson 1, listIndex 2 = lesson 2, etc.

    QString lessonPath = getLessonPath(lessonNumber);
    QFile lessonFile(lessonPath);

    if (!lessonFile.exists())
    {
        QString errorMsg = QString(
            "The lesson file could not be found:<br/><code>%1</code>"
            "<p>Please ensure the tutorial files are installed correctly in:<br/>"
            "<code>%2</code></p>"
        ).arg(lessonPath, getTutorialBasePath());

        showError(tr("Lesson Not Found"), errorMsg);
        return;
    }

    // Load the lesson
#ifdef HAVE_WEBENGINE
    QUrl lessonUrl = QUrl::fromLocalFile(lessonPath);
    _webView->setUrl(lessonUrl);
    qDebug() << "Loading lesson" << lessonNumber << "via QWebEngineView:" << lessonUrl.toString();
#else
    QUrl lessonUrl = QUrl::fromLocalFile(lessonPath);
    _textBrowser->setSource(lessonUrl);
    qDebug() << "Loading lesson" << lessonNumber << "via QTextBrowser:" << lessonPath;
#endif

    _currentListIndex = listIndex;
#ifndef HAVE_WEBENGINE
    updateNavigationButtons();
#endif
}

#ifndef HAVE_WEBENGINE
void TutorialDialog::updateNavigationButtons()
{
    _previousButton->setEnabled(_currentListIndex > 0);
    _nextButton->setEnabled(_currentListIndex < TOTAL_LIST_ITEMS - 1);
}

void TutorialDialog::onLessonSelected(QListWidgetItem* current, QListWidgetItem* previous)
{
    Q_UNUSED(previous);

    if (!current)
    {
        return;
    }

    int listIndex = _lessonList->row(current);
    loadLesson(listIndex);
}

void TutorialDialog::onPreviousClicked()
{
    if (_currentListIndex > 0)
    {
        _lessonList->setCurrentRow(_currentListIndex - 1);
    }
}

void TutorialDialog::onNextClicked()
{
    if (_currentListIndex < TOTAL_LIST_ITEMS - 1)
    {
        _lessonList->setCurrentRow(_currentListIndex + 1);
    }
}
#endif

void TutorialDialog::onLinkClicked(const QUrl& url)
{
    qDebug() << "Link clicked:" << url.toString();

    // Get the filename from the URL
    QString fileName = url.fileName();

    if (fileName.isEmpty())
    {
        qWarning() << "Link has no filename, ignoring";
        return;
    }

    // Handle navigation to index.html
    if (fileName == "index.html")
    {
        qDebug() << "Navigating to index page";
#ifdef HAVE_WEBENGINE
        loadLesson(0);  // Index is at position 0
#else
        _lessonList->setCurrentRow(0);  // Index is at position 0
#endif
        return;
    }

    // Handle navigation to lesson files (lesson01.html, lesson02.html, etc.)
    if (fileName.startsWith("lesson") && fileName.endsWith(".html"))
    {
        QRegularExpression rx("lesson(\\d+)\\.html");
        QRegularExpressionMatch match = rx.match(fileName);

        if (match.hasMatch())
        {
            int lessonNum = match.captured(1).toInt();
            qDebug() << "Extracted lesson number:" << lessonNum;

            if (lessonNum > 0 && lessonNum <= TOTAL_LESSONS)
            {
                // Lesson 1 is at list index 1, lesson 2 at index 2, etc.
#ifdef HAVE_WEBENGINE
                loadLesson(lessonNum);
#else
                _lessonList->setCurrentRow(lessonNum);
#endif
            }
            else
            {
                qWarning() << "Lesson number out of range:" << lessonNum;
            }
        }
        else
        {
            qWarning() << "Failed to extract lesson number from:" << fileName;
        }
        return;
    }

    // Unknown link type
    qWarning() << "Unknown link type:" << fileName;
}