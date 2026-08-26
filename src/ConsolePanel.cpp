#include "ConsolePanel.h"

#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QHideEvent>
#include <QFontDatabase>
#include <QSettings>
#include <QCoreApplication>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
// Same QSettings scope/convention MainWindow::readSettings()/writeSettings()
// use for its own "geometry" key - a distinct key here to avoid colliding
// with it.
const char* kGeometrySettingsKey = "consoleGeometry";
}

ConsolePanel::ConsolePanel(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    // Qt::Tool was tried here instead of Qt::Window to keep this off the
    // taskbar, but that also drops it from Alt+Tab and any OS-level way to
    // bring it back to front - if MainWindow (created after this, see
    // main.cpp) ends up on top of it, it becomes practically unreachable.
    // Back to a normal Qt::Window.
    setWindowTitle(tr("Console"));
    setAttribute(Qt::WA_DeleteOnClose, false);

    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QByteArray savedGeometry = settings.value(kGeometrySettingsKey, QByteArray()).toByteArray();
    if (savedGeometry.isEmpty() || !restoreGeometry(savedGeometry))
    {
        resize(900, 500);
    }

#ifdef _WIN32
    // Deliberately NOT using setWindowIcon(QIcon(...)) here. Every icon
    // fix tried so far (a single-res PNG, then the real multi-res .ico via
    // Qt's resource system) still went through Qt's own QIcon->HICON
    // conversion, and the taskbar icon stayed inconsistent specifically
    // while this window was open/active - reverting the moment it closed.
    // MainWindow never calls setWindowIcon() at all and has never shown
    // this problem, relying purely on Windows' own default resolution of
    // the exe's embedded icon resource. This applies that exact same
    // resource (ModelViewer.rc names it "A") the same native way, via
    // WM_SETICON directly, bypassing Qt's icon pipeline for this window
    // entirely rather than trying to fix it through QIcon.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    HICON hIconBig = static_cast<HICON>(LoadImageW(hInstance, L"A", IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON hIconSmall = static_cast<HICON>(LoadImageW(hInstance, L"A", IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (hIconBig)
    {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
    }
    if (hIconSmall)
    {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
    }
#endif

    _textEdit = new QPlainTextEdit(this);
    _textEdit->setReadOnly(true);
    _textEdit->setUndoRedoEnabled(false);
    _textEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    _textEdit->setMaximumBlockCount(20000);
    _textEdit->setFrameStyle(QFrame::NoFrame);

    QFont consoleFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    consoleFont.setPointSize(12);
    _textEdit->setFont(consoleFont);

    _textEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #0C0C0C; color: #CCCCCC; border: none; }");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_textEdit);
}

void ConsolePanel::appendLine(const QString& line)
{
    _textEdit->appendPlainText(line);
}

void ConsolePanel::setMaxLineCount(int lines)
{
    _textEdit->setMaximumBlockCount(lines);
}

void ConsolePanel::closeEvent(QCloseEvent* event)
{
    // Mirror the old real console's show/hide toggle behavior: clicking
    // this window's own close button just hides it (re-enabling the
    // console setting shows the same instance again, log history intact),
    // it never takes anything else down with it.
    event->ignore();
    hide();
}

void ConsolePanel::hideEvent(QHideEvent* event)
{
    // Covers every way this stops being visible - the close button above
    // (which routes through hide()), and the direct hide() calls from
    // Logger::setConsoleEnabled(false)/setConsoleWindowVisible(false) -
    // so size/position survive both a plain toggle and a full app restart.
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue(kGeometrySettingsKey, saveGeometry());
    QWidget::hideEvent(event);
}
