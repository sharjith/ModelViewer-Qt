#pragma once

#include "ResultReader.h"

// VTK XML UnstructuredGrid (.vtu) reader. Supports data arrays stored as ASCII, inline base64
// ("binary"), and "appended" (raw or base64), each optionally zlib-compressed (vtkZLibDataCompressor),
// with UInt32 or UInt64 headers and either byte order. Other compressors (LZ4, LZMA, zstd) are
// rejected with a clear error. Only the first <Piece> is read; parallel (.pvtu) files are not
// supported. Quadratic cells are read (their corner nodes drive the display); polyhedra are read as
// Unsupported placeholders (Phase 0).
//
// Not part of the public loading API - use readResultFile() in ResultReader.h.
ResultReadOutcome readVtkXmlUnstructuredGrid(const QString& path, const std::atomic<bool>* cancel);
