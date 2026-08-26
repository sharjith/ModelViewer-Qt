#pragma once

#include <QWidget>

class QPlainTextEdit;

/**
 * @class ConsolePanel
 * @brief A plain Qt top-level window that visually mimics an OS console
 * (black background, monospace text, auto-scrolling), used by Logger in
 * place of a real Win32 console.
 *
 * A real console (AllocConsole()) shares an OS-level job/attachment
 * relationship with the process that owns it; on Windows, closing it via
 * some paths (notably the taskbar's "Close all windows" on a grouped icon)
 * force-terminates the whole application with no chance to check for
 * unsaved documents, and no in-process handler can reliably intercept
 * that. A plain QWidget has none of that OS-level coupling - closing it
 * only affects this window, through Qt's normal close-event handling.
 */
class ConsolePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    void setMaxLineCount(int lines);

public slots:
    void appendLine(const QString& line);

protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QPlainTextEdit* _textEdit;
};
