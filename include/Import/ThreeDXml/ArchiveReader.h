#pragma once
#include "Document.h"

namespace ThreeDXml {
class ArchiveReader {
public:
    explicit ArchiveReader(const QString& path, const Options& options);
    ~ArchiveReader();
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
    QByteArray read(const QString& member);
    static QString memberPath(const QString& name);
private:
    QString _path;
    Options _options;
    void* _zip = nullptr;
    quint64 _expanded = 0;
    QHash<QString, QByteArray> _names;
};
}
