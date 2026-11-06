// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <stdexcept>
#include <vector>

// Internal, versioned PNG text carrier for authored levels 1..N. The ordinary
// PNG pixels remain level zero; unaware readers still see a valid base image.
// This is ModelViewer metadata, not a standard glTF/JT texture extension.
namespace TextureMipChain
{
inline constexpr char Key[] = "ModelViewer.MipChain.v1";
inline constexpr char TerminalKey[] = "ModelViewer.MipChain.Terminal.v1";
inline constexpr char BorderTerminal[] = "border-3x3";
inline constexpr char BorderSTerminal[] = "border-3x1";
inline constexpr char BorderTTerminal[] = "border-1x3";
inline constexpr qint64 Limit = 64 * 1024 * 1024;

inline void validate(const QImage& base, const std::vector<QImage>& levels)
{
    if (base.isNull() || levels.size() > 15)
        throw std::runtime_error("Invalid authored mip chain");
    const auto terminal = base.text(QLatin1String(TerminalKey));
    const bool borderBoth=terminal==QLatin1String(BorderTerminal);
    const bool borderS=borderBoth||terminal==QLatin1String(BorderSTerminal);
    const bool borderT=borderBoth||terminal==QLatin1String(BorderTTerminal);
    if (!terminal.isEmpty() && !borderS && !borderT)
        throw std::runtime_error("Unknown authored mip terminal profile");
    const int interiorExtent=1<<levels.size();
    if (!terminal.isEmpty() && (levels.empty()
        ||(borderS?base.width()!=3*interiorExtent:base.width()>interiorExtent)
        ||(borderT?base.height()!=3*interiorExtent:base.height()>interiorExtent)))
        throw std::runtime_error("Border mip profile requires a longest 3*2^N sampled axis");
    int width = base.width(), height = base.height();
    qint64 bytes = qint64(width) * height * 4;
    for (const auto& image : levels)
    {
        if (width == 1 && height == 1)
            throw std::runtime_error("Authored mip chain extends past 1x1");
        width = std::max(1, width / 2); height = std::max(1, height / 2);
        bytes += qint64(width) * height * 4;
        if (image.isNull() || image.width() != width || image.height() != height || bytes > Limit)
            throw std::runtime_error("Authored mip dimensions or decoded byte limit mismatch");
    }
    if (bytes > Limit || (!levels.empty() && (width != (borderS?3:1) || height != (borderT?3:1))))
        throw std::runtime_error("Authored mip chain terminal dimensions or decoded byte limit mismatch");
}

inline void attach(QImage& base, const std::vector<QImage>& levels)
{
    validate(base, levels);
    if (levels.empty()) return;
    QJsonArray encoded;
    qint64 bytes = 0;
    for (auto image : levels)
    {
        // No nested carriers; every member is an ordinary pixel-only PNG.
        image.setText(QLatin1String(Key), QString());
        image.setText(QLatin1String(TerminalKey), QString());
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) throw std::runtime_error("Cannot encode authored mip");
        const auto text = png.toBase64(); bytes += text.size();
        if (bytes > Limit) throw std::runtime_error("Authored mip carrier exceeds encoded byte limit");
        encoded.append(QString::fromLatin1(text));
    }
    const auto carrier=QJsonDocument(encoded).toJson(QJsonDocument::Compact);
    if(carrier.size()>Limit)throw std::runtime_error("Authored mip carrier exceeds encoded byte limit");
    base.setText(QLatin1String(Key), QString::fromUtf8(carrier));
}

inline std::vector<QImage> decode(const QImage& base)
{
    const auto carrier = base.text(QLatin1String(Key));
    if (carrier.isEmpty()) {
        if (!base.text(QLatin1String(TerminalKey)).isEmpty())
            throw std::runtime_error("Terminal profile without an authored mip chain");
        return {};
    }
    if (carrier.size() > Limit) throw std::runtime_error("Authored mip carrier exceeds text limit");
    for(const auto c:carrier)if(c.unicode()>127)throw std::runtime_error("Authored mip carrier must be ASCII");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(carrier.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw std::runtime_error("Malformed authored mip carrier");
    const auto array = document.array();
    if (array.isEmpty() || array.size() > 15) throw std::runtime_error("Invalid authored mip count");
    std::vector<QImage> levels; levels.reserve(size_t(array.size()));
    int width = base.width(), height = base.height();
    qint64 bytes = qint64(width) * height * 4;
    for (const auto& value : array)
    {
        if (!value.isString() || (width == 1 && height == 1))
            throw std::runtime_error("Invalid authored mip entry");
        width = std::max(1, width / 2); height = std::max(1, height / 2);
        bytes += qint64(width) * height * 4;
        if (bytes > Limit) throw std::runtime_error("Authored mip decoded byte limit exceeded");
        const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) throw std::runtime_error("Invalid authored mip base64");
        QBuffer buffer; buffer.setData(decoded.decoded); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        if (reader.size() != QSize(width, height)) throw std::runtime_error("Authored mip dimensions mismatch");
        auto image = reader.read();
        if (image.isNull() || (!image.text(QLatin1String(Key)).isEmpty() || !image.text(QLatin1String(TerminalKey)).isEmpty()))
            throw std::runtime_error("Invalid or nested authored mip image");
        levels.push_back(image.convertToFormat(QImage::Format_RGBA8888));
    }
    validate(base, levels);
    return levels;
}
}
