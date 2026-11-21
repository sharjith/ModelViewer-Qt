// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QString>
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

// The colour of the appearance the part itself has applied (its "active colour style"), for a part whose graphics save no
// colour. The style is saved as text in the part's model-information property stream, and names an appearance asset
// ("MaterialId") whose colour is saved in the application segment. Empty when either is not there. The name of the style is
// returned too, for diagnostics.
struct PartStyleColor {
    DiffuseColor color;
    QString materialId;
    QString styleName;
};
std::optional<PartStyleColor> readPartStyleColor(CompoundFile& file);

// The colours of a part with several bodies. The single-body appearance above gives up on those, because their graphics hang
// from several shells. Here every graphics face is read, keyed by its persistent face key (the key the B-rep stores on each
// face), and a face is listed only when the file gives it one unambiguous colour. Faces not listed take defaultColor.
struct PartColorTable {
    std::optional<DiffuseColor> defaultColor;
    std::unordered_map<quint32, DiffuseColor> faces;
    std::size_t conflicts = 0; // keys shared by faces with different colours; left out of faces
};

std::optional<PartColorTable> readPartColorTable(CompoundFile& file);

// The bodies of the part as its model browser names them, in the order of its graphics bodies. A part made from an
// assembly keeps a solid body ("Твердое тело1") and the surface bodies of derived-surface features ("SRF5", owned by the
// feature "SRF1::Производный"); the saved B-rep has one record per body that holds geometry. faceCounts lists the face
// counts of the graphics shells of the body, which is how a B-rep record is matched to its body (a record's face count
// equals one of them exactly). Empty when the layout is not the one recognised; names are then not used.
struct PartBodyInfo {
    QString name;
    QString feature; // empty for a plain body
    std::vector<std::size_t> faceCounts;
};

std::vector<PartBodyInfo> readPartBodyInfo(CompoundFile& file);

} // namespace Inventor
