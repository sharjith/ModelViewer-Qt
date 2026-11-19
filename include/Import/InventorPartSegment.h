// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <array>
#include <optional>
#include <unordered_map>

namespace Inventor {

class CompoundFile;

// Return the decompressed saved B-rep segment. Both input and output sizes are
// bounded by maxBytes. Unsupported or malformed data throws std::runtime_error.
QByteArray readPartBRepSegment(CompoundFile& file, qsizetype maxBytes);

using DiffuseColor = std::array<float, 3>;
struct SavedAppearance {
    std::optional<DiffuseColor> body;
    std::unordered_map<quint32, std::optional<DiffuseColor>> faces;
};

std::optional<SavedAppearance> readPartAppearance(CompoundFile& file);

} // namespace Inventor
