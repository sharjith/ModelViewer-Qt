#pragma once


#include <QDialog>
#include <QPushButton>
#include <QUrl>

#ifdef HAVE_WEBENGINE
#include <QWebEngineView>
#include <QWebEnginePage>

// Custom page to intercept link clicks
class TutorialWebPage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit TutorialWebPage(QObject* parent = nullptr);

protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override;

signals:
    void linkClicked(const QUrl& url);
};
#else
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#endif

class TutorialDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TutorialDialog(QWidget* parent = nullptr);
    ~TutorialDialog() = default;

private slots:
#ifndef HAVE_WEBENGINE
    void onLessonSelected(QListWidgetItem* current, QListWidgetItem* previous);
    void onPreviousClicked();
    void onNextClicked();
#endif
    void onLinkClicked(const QUrl& url);

private:
    void setupUI();
    void loadLesson(int listIndex);  // listIndex includes the index page
    void loadIndexPage();
#ifndef HAVE_WEBENGINE
    void populateLessonList();
    void updateNavigationButtons();
#endif

    QString getTutorialBasePath() const;
    QString getLessonPath(int lessonIndex) const;  // lessonIndex is 1-18 for lessons, -1 for index
    QString getLessonTitle(int lessonIndex) const;
    QString loadHtmlFile(const QString& filename);
    void showError(const QString& title, const QString& message);

#ifdef HAVE_WEBENGINE
    // Each lesson page already has its own sidebar nav and Previous/Next footer
    // links (see data/tutorials/common-styles.css), so there's no native list or
    // button row here - it would just be a second, duplicate sidebar next to the
    // one already rendered inside the page.
    QWebEngineView* _webView;
    TutorialWebPage* _webPage;
#else
    QListWidget* _lessonList;
    QTextBrowser* _textBrowser;
    QPushButton* _previousButton;
    QPushButton* _nextButton;
#endif
    QPushButton* _closeButton;

    int _currentListIndex;  // Current position in list (0=index, 1-18=lessons)
    static constexpr int TOTAL_LESSONS = 18;
    static constexpr int TOTAL_LIST_ITEMS = TOTAL_LESSONS + 1;  // Include index page
};
