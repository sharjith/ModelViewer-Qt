// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>

namespace Inventor {

class CompoundFile;

// Return the decompressed saved B-rep segment. Both input and output sizes are
// bounded by maxBytes. Unsupported or malformed data throws std::runtime_error.
QByteArray readPartBRepSegment(CompoundFile& file, qsizetype maxBytes);

} // namespace Inventor
