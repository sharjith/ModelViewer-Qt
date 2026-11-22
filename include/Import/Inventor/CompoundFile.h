// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QFile>
#include <QStringList>

#include <cstddef>
#include <optional>
#include <vector>

namespace Inventor {

// Read-only Microsoft Compound File Binary container. Format and I/O failures
// throw std::runtime_error. Stream indices belong to this open file.
class CompoundFile
{
public:
    struct Stream {
        QStringList path;
        quint64 size = 0;

    private:
        friend class CompoundFile;
        quint32 firstSector = 0;
    };

    explicit CompoundFile(const QString& path);

    const std::vector<Stream>& streams() const { return _streams; }
    QByteArray readStream(std::size_t index, qsizetype maxBytes);

private:
    QByteArray readAt(qint64 offset, qsizetype size);
    QByteArray readSector(quint32 sector);
    std::vector<quint32> readFat(const QByteArray& header);
    void readDirectory(const std::vector<quint32>& sectors);
    std::vector<quint32> sectorChain(quint32 first, const std::vector<quint32>& table,
                                     std::size_t sectorLimit, std::optional<std::size_t> expected) const;

    QFile _file;
    quint16 _majorVersion = 0;
    quint32 _sectorSize = 0;
    quint32 _sectorCount = 0;
    quint64 _miniStreamSize = 0;
    std::vector<quint32> _fat;
    std::vector<quint32> _miniFat;
    std::vector<quint32> _miniStreamSectors;
    std::vector<Stream> _streams;
    quint32 _firstMiniStreamSector = 0;
};

} // namespace Inventor
