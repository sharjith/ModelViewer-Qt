// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

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

// The colours of a part with several bodies. The single-body appearance above gives up on those, because their graphics hang
// from several shells. Here every graphics face is read, keyed by its persistent face key (the key the B-rep stores on each
// face), and a face is listed only when the file gives it one unambiguous colour. Faces not listed take defaultColor.
struct PartColorTable {
    std::optional<DiffuseColor> defaultColor;
    std::unordered_map<quint32, DiffuseColor> faces;
    std::size_t conflicts = 0; // keys shared by faces with different colours; left out of faces
};

std::optional<PartColorTable> readPartColorTable(CompoundFile& file);

} // namespace Inventor
