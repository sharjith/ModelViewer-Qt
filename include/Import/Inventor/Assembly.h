// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <gp_Trsf.hxx>

#include <functional>
#include <vector>

namespace Inventor {

// One occurrence of a component in an Inventor assembly (.iam), as the assembly itself saves it. A component is a part
// (.ipt) or a sub-assembly (.iam); a sub-assembly is read by calling readAssemblyOccurrences on its own file and composing
// the placements.
struct AssemblyOccurrence {
    QString name;      // as the model browser names the occurrence ("Part:1")
    QString reference; // the saved path of the component file, as stored in the assembly
    gp_Trsf placement; // relative to the assembly that holds the occurrence, in millimetres
    bool visible = true;
    bool suppressed = false;
    // Non-empty when several unused files carry the occurrence's name and the first was taken: a wrong choice would place other
    // geometry with a valid placement, so the scene reports it on the occurrence.
    QString bindingNote;
    // True when no saved file name equals the row's name, the file was taken by its position in the saved table, and the
    // occurrence's own name does not repeat the file's name either (nothing confirms the choice). The scene reports one summary
    // per assembly; a positional binding whose name agrees with the file is not flagged.
    bool boundByPosition = false;
};

// The direct occurrences of the assembly in browser order. Throws std::runtime_error when the assembly's saved tables are not
// in a layout this reader recognises (the message says which table), and on cancellation. savedPath, when given, receives the
// pathname the assembly was saved under; component references are absolute paths on the machine that saved it, so they are
// resolved relative to that (see resolveAssemblyComponent).
std::vector<AssemblyOccurrence> readAssemblyOccurrences(const QString& path,
                                                        const std::function<bool()>& cancelled = {},
                                                        QString* savedPath = nullptr);

// The file a component reference names, on this machine. Component references are saved as absolute paths of the machine that
// saved the assembly (for example C:\Users\<name>\Desktop\Project\Sub\Part.ipt), so they are tried in turn: as given; relative to
// the folder the assembly was saved in, applied to the folder it is in now; then the last 3, 2 and 1 path components under the
// assembly's folder and up to two folders above it. path is empty and problem says why when no file is found (a standard-library
// part that was not shipped, a renamed file) or the reference is an embedded one.
struct ComponentFile {
    QString path;
    QString problem;
};
ComponentFile resolveAssemblyComponent(const QString& assembly, const QString& savedAssemblyPath, const QString& reference);

} // namespace Inventor
