#include "ArchiveReader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <minizip/unzip.h>
#include <minizip/ioapi.h>
#include <limits>

namespace ThreeDXml {
QString ArchiveReader::memberPath(const QString& input)
{
    QString name = input;
    if (name.startsWith(QStringLiteral("urn:3DXML:"))) name.remove(0, 10);
    if (name.isEmpty() || name.contains(QChar(0)) || name.contains('\\') ||
        name.contains(':') || name.startsWith('/') || name.split('/').contains(QStringLiteral("..")))
        throw Error(QStringLiteral("Invalid/external 3DXML member: %1").arg(input));
    return QDir::cleanPath(name);
}

ArchiveReader::ArchiveReader(const QString& path, const Options& options) : _path(path), _options(options)
{
    _options.check();
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly)) throw Error(QStringLiteral("Cannot open 3DXML file: %1").arg(path));
    if (!probe.peek(4).startsWith("PK")) {
        if (probe.size()>128LL*1024*1024) throw Error(QStringLiteral("3DXML XML document size limit exceeded"));
        QXmlStreamReader xml(&probe);
        while (!xml.atEnd() && !xml.isStartElement()) {
            _options.check(); xml.readNext();
            if (xml.isDTD() || xml.isEntityReference()) throw Error(QStringLiteral("Unsupported XML DTD/entity"));
        }
        if (!xml.isStartElement() || xml.name()!=QLatin1String("Model_3dxml"))
            throw Error(QStringLiteral("Expected a 3DXML ZIP package or standalone Model_3dxml document: %1").arg(path));
        const QFileInfo info(path); _directory=info.canonicalPath(); _rootDocument=info.fileName();
        return;
    }
    probe.close();
    // QFile callbacks preserve Unicode archive paths on Windows as well as Unix.
    zlib_filefunc64_def io{};
    io.opaque = &_path;
    io.zopen64_file = [](voidpf opaque, const void*, int) -> voidpf {
        auto file = std::make_unique<QFile>(*static_cast<QString*>(opaque));
        return file->open(QIODevice::ReadOnly) ? file.release() : nullptr;
    };
    io.zread_file = [](voidpf, voidpf stream, void* buffer, uLong size) -> uLong {
        const qint64 n = static_cast<QFile*>(stream)->read(static_cast<char*>(buffer), size);
        return n < 0 ? 0 : static_cast<uLong>(n);
    };
    io.zwrite_file = [](voidpf, voidpf, const void*, uLong) -> uLong { return 0; };
    io.ztell64_file = [](voidpf, voidpf stream) -> ZPOS64_T { return static_cast<QFile*>(stream)->pos(); };
    io.zseek64_file = [](voidpf, voidpf stream, ZPOS64_T offset, int origin) -> long {
        auto* file = static_cast<QFile*>(stream);
        if (offset > static_cast<ZPOS64_T>(std::numeric_limits<qint64>::max())) return -1;
        qint64 base = origin == ZLIB_FILEFUNC_SEEK_CUR ? file->pos() : origin == ZLIB_FILEFUNC_SEEK_END ? file->size() : 0;
        if (static_cast<qint64>(offset) > std::numeric_limits<qint64>::max()-base) return -1;
        return file->seek(base+static_cast<qint64>(offset)) ? 0 : -1;
    };
    io.zclose_file = [](voidpf, voidpf stream) -> int { delete static_cast<QFile*>(stream); return 0; };
    io.zerror_file = [](voidpf, voidpf stream) -> int { return static_cast<QFile*>(stream)->error() != QFile::NoError; };
    _zip = unzOpen2_64("QFile", &io);
    if (!_zip) throw Error(QStringLiteral("Cannot open 3DXML ZIP archive: %1").arg(path));
    try {
        unz_global_info64 info{};
        if (unzGetGlobalInfo64(_zip, &info) != UNZ_OK || info.number_entry > 100000)
            throw Error(QStringLiteral("Invalid 3DXML archive catalog"));
        int status = unzGoToFirstFile(_zip);
        for (quint64 i = 0; i < info.number_entry; ++i) {
            _options.check();
            unz_file_info64 entry{};
            if (status != UNZ_OK || unzGetCurrentFileInfo64(_zip, &entry, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK || entry.size_filename > 4096)
                throw Error(QStringLiteral("Invalid ZIP member header"));
            QByteArray name(static_cast<qsizetype>(entry.size_filename)+1, '\0');
            if (unzGetCurrentFileInfo64(_zip, &entry, name.data(), name.size(), nullptr, 0, nullptr, 0) != UNZ_OK)
                throw Error(QStringLiteral("Truncated ZIP member header"));
            name.resize(entry.size_filename);
            if (!name.endsWith('/')) {
                const QString decoded = QString::fromUtf8(name);
                if (decoded.toUtf8() != name) throw Error(QStringLiteral("Unsupported ZIP member name encoding"));
                const QString key = memberPath(decoded);
                if (_names.contains(key) || entry.flag & 1)
                    throw Error(QStringLiteral("Duplicate/encrypted ZIP member: %1").arg(key));
                _names.insert(key, name);
            }
            status = unzGoToNextFile(_zip);
        }
    } catch (...) { unzClose(_zip); _zip = nullptr; throw; }
}
ArchiveReader::~ArchiveReader() { if (_zip) unzClose(_zip); }

QByteArray ArchiveReader::read(const QString& member)
{
    _options.check();
    const QString key = memberPath(member);
    if (!_directory.isEmpty()) {
        const QString canonical=QFileInfo(QDir(_directory).filePath(key)).canonicalFilePath();
        QString prefix=QDir::fromNativeSeparators(_directory);
        if (!prefix.endsWith('/')) prefix+=QLatin1Char('/');
#ifdef Q_OS_WIN
        constexpr Qt::CaseSensitivity caseMode=Qt::CaseInsensitive;
#else
        constexpr Qt::CaseSensitivity caseMode=Qt::CaseSensitive;
#endif
        if (canonical.isEmpty()) throw Error(QStringLiteral("Missing 3DXML companion file: %1").arg(key));
        if (!QDir::fromNativeSeparators(canonical).startsWith(prefix,caseMode))
            throw Error(QStringLiteral("3DXML companion escapes the model directory: %1").arg(key));
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly)) throw Error(QStringLiteral("Cannot read 3DXML companion: %1").arg(key));
        const qint64 size=file.size();
        if (size<0 || size>128LL*1024*1024 || _expanded+quint64(size)>1024ULL*1024*1024)
            throw Error(QStringLiteral("3DXML expanded-data limit exceeded: %1").arg(key));
        QByteArray data; data.reserve(static_cast<qsizetype>(size));
        while (data.size()<size) {
            _options.check(); const auto chunk=file.read(qMin<qint64>(1024*1024,size-data.size()));
            if (chunk.isEmpty()) throw Error(QStringLiteral("Truncated/unreadable 3DXML companion: %1").arg(key));
            data.append(chunk);
        }
        if (!file.atEnd()) throw Error(QStringLiteral("3DXML companion changed during read: %1").arg(key));
        _expanded+=data.size(); return data;
    }
    if (!_names.contains(key) || unzLocateFile(_zip, _names.value(key).constData(), 1) != UNZ_OK)
        throw Error(QStringLiteral("Missing 3DXML member: %1").arg(key));
    unz_file_info64 info{};
    if (unzGetCurrentFileInfo64(_zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
        info.uncompressed_size > 128ULL*1024*1024 || _expanded + info.uncompressed_size > 1024ULL*1024*1024)
        throw Error(QStringLiteral("3DXML expanded-data limit exceeded: %1").arg(key));
    if (unzOpenCurrentFile(_zip) != UNZ_OK) throw Error(QStringLiteral("Cannot decompress %1").arg(key));
    try {
        QByteArray bytes(static_cast<qsizetype>(info.uncompressed_size), Qt::Uninitialized);
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            _options.check();
            const int n = unzReadCurrentFile(_zip, bytes.data()+offset, static_cast<unsigned>(qMin<qsizetype>(1024*1024, bytes.size()-offset)));
            if (n <= 0) throw Error(QStringLiteral("Truncated/corrupt ZIP member: %1").arg(key));
            offset += n;
        }
        char extra;
        if (unzReadCurrentFile(_zip, &extra, 1) != 0) throw Error(QStringLiteral("ZIP member size mismatch: %1").arg(key));
        const int closed = unzCloseCurrentFile(_zip);
        if (closed != UNZ_OK) throw Error(QStringLiteral("ZIP CRC mismatch: %1").arg(key));
        _expanded += bytes.size();
        return bytes;
    } catch (...) { unzCloseCurrentFile(_zip); throw; }
}
}
