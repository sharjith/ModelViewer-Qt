#pragma once

#include <QSyntaxHighlighter>
#include <QTextDocument>

/**
 * @class LogHighlighter
 * @brief Syntax highlighter for log entries
 *
 * Highlights log format: [timestamp] LEVEL | context | message
 * Colors by log level:
 * - DEBUG: Gray
 * - INFO: Black
 * - WARNING: Orange
 * - ERROR: Red
 */
class LogHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit LogHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct TextFormat
    {
        QTextCharFormat timestamp;
        QTextCharFormat debug;
        QTextCharFormat info;
        QTextCharFormat warning;
        QTextCharFormat error;
    };

    TextFormat formats;
};
