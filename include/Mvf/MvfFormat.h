#pragma once

#include <QByteArray>
#include <QDataStream>

namespace Mvf
{
constexpr quint32 CurrentVersion = 1;
constexpr quint32 Magic = 0x3346564D; // "MVF3"

enum class ChunkType : quint32
{
    Json     = 0x4E4F534A, // "JSON"
    Geometry = 0x4D4F4547, // "GEOM"
    Images   = 0x53474D49, // "IMGS"
    Aux0     = 0x30585541  // "AUX0"
};

struct Header
{
    quint32 magic = Magic;
    quint32 version = CurrentVersion;
    quint32 fileLength = 0;
    quint32 flags = 0;
};

struct ChunkHeader
{
    quint32 length = 0;
    ChunkType type = ChunkType::Json;
};

bool writeHeader(QDataStream& out, const Header& header);
bool readHeader(QDataStream& in, Header& header);
bool isSupportedHeader(const Header& header);

bool writeChunk(QDataStream& out, ChunkType type, const QByteArray& payload);
bool readChunkHeader(QDataStream& in, ChunkHeader& header);
QByteArray readChunkPayload(QDataStream& in, const ChunkHeader& header);
}

