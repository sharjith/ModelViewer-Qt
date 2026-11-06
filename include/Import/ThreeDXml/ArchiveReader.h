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
    QString rootDocument() const { return _rootDocument; }
    const QString& path() const { return _path; }
    static QString memberPath(const QString& name);
private:
    QString _path, _directory, _rootDocument;
    Options _options;
    void* _zip = nullptr;
    quint64 _expanded = 0;
    QHash<QString, QByteArray> _names;
};
}
