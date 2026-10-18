#include "MvfFormat.h"

#include <QIODevice>

namespace Mvf
{
bool writeHeader(QDataStream& out, const Header& header)
{
    out << header.magic
        << header.version
        << header.fileLength
        << header.flags;
    return out.status() == QDataStream::Ok;
}

bool readHeader(QDataStream& in, Header& header)
{
    in >> header.magic
       >> header.version
       >> header.fileLength
       >> header.flags;
    return in.status() == QDataStream::Ok;
}

bool isSupportedHeader(const Header& header)
{
    return header.magic == Magic && header.version == CurrentVersion;
}

bool writeChunk(QDataStream& out, ChunkType type, const QByteArray& payload)
{
    ChunkHeader header;
    header.length = static_cast<quint32>(payload.size());
    header.type = type;

    out << header.length << static_cast<quint32>(header.type);
    if (out.status() != QDataStream::Ok)
        return false;

    if (!payload.isEmpty())
    {
        const qint64 written = out.writeRawData(payload.constData(), payload.size());
        if (written != payload.size())
            return false;
    }

    return out.status() == QDataStream::Ok;
}

bool readChunkHeader(QDataStream& in, ChunkHeader& header)
{
    quint32 type = 0;
    in >> header.length >> type;
    header.type = static_cast<ChunkType>(type);
    return in.status() == QDataStream::Ok;
}

QByteArray readChunkPayload(QDataStream& in, const ChunkHeader& header)
{
    QByteArray payload(static_cast<int>(header.length), Qt::Uninitialized);
    if (header.length == 0)
        return payload;

    const qint64 read = in.readRawData(payload.data(), payload.size());
    if (read != payload.size())
        return {};

    return payload;
}
}

