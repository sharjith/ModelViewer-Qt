#include "LogHighlighter.h"

#include <QTextDocument>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QDebug>

LogHighlighter::LogHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{

    // Timestamp format - gray
    formats.timestamp.setForeground(QColor(128, 128, 128));
    formats.timestamp.setFontWeight(QFont::Bold);

    // DEBUG - brown
    formats.debug.setForeground(QColor(150, 75, 0));

    // INFO - green
    formats.info.setForeground(QColor(20, 220, 60));

    // WARNING - orange
    formats.warning.setForeground(QColor(255, 140, 0));

    // ERROR - red
    formats.error.setForeground(QColor(240, 20, 60));
    formats.error.setFontWeight(QFont::Bold);
}

void LogHighlighter::highlightBlock(const QString& text)
{
    // Log format: [timestamp] LEVEL | context | message
    // Example: [2025-01-21 14:30:45.123] DEBUG | TextureCache | Texture loaded

    // Match: [anything] LEVEL
    QRegularExpression logPattern(R"(\[(.*?)\]\s+([A-Z]+)\s*)");
    QRegularExpressionMatch match = logPattern.match(text);

    if (match.hasMatch())
    {
        // Highlight timestamp (inside brackets)
        int timestampStart = match.capturedStart(1);
        int timestampLength = match.capturedLength(1);
        setFormat(timestampStart, timestampLength, formats.timestamp);

        // Highlight level based on type
        QString level = match.captured(2);
        int levelStart = match.capturedStart(2);
        int levelLength = match.capturedLength(2);

        if (level == "DEBUG")
        {
            setFormat(levelStart, levelLength, formats.debug);
        }
        else if (level == "INFO")
        {
            setFormat(levelStart, levelLength, formats.info);
        }
        else if (level == "WARN")
        {
            setFormat(levelStart, levelLength, formats.warning);
        }
        else if (level == "ERROR")
        {
            setFormat(levelStart, levelLength, formats.error);
        }
    }
}
