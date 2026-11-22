// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "Inventor/AssemblyScene.h"

#include "BRepToAssimpConverter.h"
#include "ImportMetadata.h"
#include "Inventor/Assembly.h"
#include "Inventor/PartSolid.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QtGlobal>

#include <Quantity_Color.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Last: it pulls in the Windows headers.
#include "MemoryGuard.h"

namespace Inventor {
namespace {

// The converter returns one flat child of the root per placed body, with the placement already in the geometry. The index
// of the instance rides in the node and mesh names behind this character and is removed again when the nodes are regrouped
// under the assembly's occurrence tree (the same approach as the Parasolid importer).
constexpr char kInstanceTag = '\x1f';

struct PartData {
    std::vector<TopoDS_Shape> shapes;     // solids and incomplete surfaces, already tessellated
    std::vector<QString> shapeNames;      // "<part> Body k", or the part name for a part with one body
    std::vector<QString> shapeWarnings;   // the message of an incomplete body, empty otherwise
    std::vector<std::pair<QString, QString>> failed; // bodies that could not be built: name, reason
    Quantity_Color color = Quantity_Color(0.7, 0.7, 0.7, Quantity_TOC_RGB);
    QString error;                        // the whole part could not be read
};

// One group above a leaf. The id is unique per occurrence in the scene, so two groups of one parent that carry the same name
// stay two groups.
struct Step {
    QString name;
    int id = 0;
};

struct Instance {
    std::vector<Step> path; // groups above the leaf, outermost first
    QString name;
    QString warning;
    bool hidden = false; // the occurrence, or an assembly above it, is hidden in the saved design view
    int order = 0;
};

struct Placeholder {
    std::vector<Step> path;
    QString name;
    QString message;
    int order = 0;
};

struct TreeGroup {
    std::string name;
    struct Entry {
        TreeGroup* group = nullptr;
        aiNode* leaf = nullptr;
    };
    std::vector<Entry> entries;
    std::map<std::string, std::unique_ptr<TreeGroup>> byName;
};

aiNode* materialize(const TreeGroup& group, aiNode* parent)
{
    auto* node = new aiNode(group.name);
    node->mParent = parent;
    node->mNumChildren = static_cast<unsigned>(group.entries.size());
    if (!group.entries.empty()) {
        node->mChildren = new aiNode*[group.entries.size()];
        for (std::size_t i = 0; i < group.entries.size(); ++i) {
            const auto& entry = group.entries[i];
            if (entry.group) {
                node->mChildren[i] = materialize(*entry.group, node);
            } else {
                entry.leaf->mParent = node;
                node->mChildren[i] = entry.leaf;
            }
        }
    }
    return node;
}

// The metadata the viewer reads from a node: an import issue (severity and message), and the flag that the node starts hidden.
void setNodeMetadata(aiNode* node, const char* severity, const QString& message, bool hidden)
{
    const unsigned count = (severity ? 2u : 0u) + (hidden ? 1u : 0u);
    if (count == 0)
        return;
    node->mMetaData = aiMetadata::Alloc(count);
    unsigned index = 0;
    if (severity) {
        node->mMetaData->Set(index++, "ModelViewer.ImportIssue.Severity", aiString(severity));
        node->mMetaData->Set(index++, "ModelViewer.ImportIssue.Message", aiString(message.toStdString()));
    }
    if (hidden)
        node->mMetaData->Set(index++, ImportMetadata::InitiallyHidden, true);
}

void setIssue(aiNode* node, const char* severity, const QString& message)
{
    setNodeMetadata(node, severity, message, false);
}

class SceneBuilder
{
public:
    explicit SceneBuilder(const AssemblySceneOptions& options) : _options(options) {}

    AssemblySceneResult build(const QString& assemblyPath)
    {
        std::vector<Step> groups;
        std::vector<QString> stack;
        report(say(QT_TRANSLATE_NOOP("InventorImport", "Reading the assembly structure")), 0.0);
        walk(QFileInfo(assemblyPath).absoluteFilePath(), gp_Trsf(), groups, stack, false);
        if (_tuples.empty()) {
            throw std::runtime_error(_firstProblem.isEmpty() ? std::string("The Inventor assembly contains no components that can be shown")
                                                              : _firstProblem.toStdString());
        }
        if (_suppressed != 0) {
            _warnings.append(QStringLiteral("%1 suppressed component(s) were not loaded.").arg(_suppressed));
        }
        if (_hidden != 0) {
            _warnings.append(QStringLiteral("%1 component(s) are hidden in the saved design view; they are loaded hidden.").arg(_hidden));
        }

        // Faces carry the colour the part saves for them; the placed copies share the faces, so the colours still match.
        BRepToAssimpConverter::setFaceColourOverrides(_overrides);
        report(say(QT_TRANSLATE_NOOP("InventorImport", "Converting %1 bodies to the scene")).arg(_tuples.size()), 0.90);
        // The converter moves the bar itself; keep it inside the 90 to 97 % reserved for the conversion.
        std::unique_ptr<aiScene> scene(BRepToAssimpConverter::convert(_tuples, false, nullptr, 90, 97));
        BRepToAssimpConverter::setFaceColourOverrides({});
        if (!scene || !scene->mRootNode || scene->mNumMeshes == 0)
            throw std::runtime_error("Inventor assembly did not produce display geometry");
        report(say(QT_TRANSLATE_NOOP("InventorImport", "Arranging the model tree")), 0.97);
        regroup(scene.get());

        AssemblySceneResult result;
        result.scene = std::move(scene);
        result.warnings = _warnings;
        return result;
    }

private:
    static QString say(const char* source) { return QCoreApplication::translate("InventorImport", source); }

    // Progress bar bands: parts and structure 0-90 % (the number of parts is only known at the end, so each distinct part moves
    // the bar on an approaching curve and its own read progress fills its slice), conversion 90-97 %, tree 97-100 %.
    static double partPosition(std::size_t parts) { return 0.90 * double(parts) / (double(parts) + 25.0); }

    void report(const QString& stage, double fraction) const
    {
        if (_options.progress)
            _options.progress(stage, fraction);
    }

    void checkCancelled() const
    {
        if (_options.cancelled && _options.cancelled())
            throw std::runtime_error("Inventor import cancelled");
    }

    // A component that cannot be shown. Strict fails the import; otherwise it becomes an error item in the tree.
    void problem(const std::vector<Step>& path, const QString& name, const QString& message)
    {
        if (_firstProblem.isEmpty())
            _firstProblem = QStringLiteral("%1: %2").arg(name, message);
        if (_options.strict)
            throw std::runtime_error(QStringLiteral("%1: %2").arg(name, message).toStdString());
        _placeholders.push_back({path, name, message, _order++});
        _warnings.append(QStringLiteral("%1 could not be loaded and is shown as an error node: %2").arg(name, message));
    }

    const PartData& part(const QString& file)
    {
        const auto cached = _parts.find(file);
        if (cached != _parts.end())
            return cached->second;
        PartData& data = _parts[file];
        const QString base = QFileInfo(file).completeBaseName();
        try {
            PartAppearance appearance;
            // This is the n-th distinct part; its read fills the slice between the curve positions before and after it.
            const double from = partPosition(_parts.size() - 1), to = partPosition(_parts.size());
            report(say(QT_TRANSLATE_NOOP("InventorImport", "Reading part %1: %2")).arg(_parts.size()).arg(QFileInfo(file).fileName()), from);
            auto outcomes = readPartBodies(file, _options.cancelled, &appearance, _options.strict,
                [this, from, to](double fraction) { report(QString(), from + (to - from) * 0.9 * fraction); },
                [this, &file](const QString& stage) { report(stage + QStringLiteral(": ") + QFileInfo(file).fileName(), -1.0); });
            if (appearance.body)
                data.color = *appearance.body;
            for (const auto& [face, color] : appearance.faces)
                _overrides.emplace(face, color);
            for (std::size_t i = 0; i < outcomes.size(); ++i) {
                const QString name = outcomes.size() == 1 ? base : QStringLiteral("%1 Body %2").arg(base).arg(i + 1);
                const auto& outcome = outcomes[i];
                if (!outcome.solid.IsNull()) {
                    data.shapes.push_back(outcome.solid);
                    data.shapeNames.push_back(name);
                    data.shapeWarnings.emplace_back();
                } else if (!outcome.incompleteShape.IsNull()) {
                    data.shapes.push_back(outcome.incompleteShape);
                    data.shapeNames.push_back(name);
                    data.shapeWarnings.push_back(outcome.isSurface ? QString() : outcome.error);
                } else {
                    data.failed.emplace_back(name, outcome.error);
                }
            }
            report(say(QT_TRANSLATE_NOOP("InventorImport", "Tessellating part %1: %2")).arg(_parts.size()).arg(QFileInfo(file).fileName()), from + (to - from) * 0.9);
            for (const auto& shape : data.shapes)
                BRepToAssimpConverter::preTessellate(shape); // once per distinct part; every placed copy shares the result
        } catch (const Detail::ResourceError&) {
            throw; // out of memory is never a reason to skip a part
        } catch (const std::exception& error) {
            if (_options.strict || (_options.cancelled && _options.cancelled()))
                throw;
            data.shapes.clear();
            data.shapeNames.clear();
            data.shapeWarnings.clear();
            data.failed.clear();
            data.error = QString::fromUtf8(error.what());
        }
        return data;
    }

    // hiddenAbove: an assembly above this one is hidden, so everything in it is.
    void walk(const QString& file, const gp_Trsf& parentWorld, std::vector<Step>& groups, std::vector<QString>& stack,
              bool hiddenAbove)
    {
        checkCancelled();
        QString savedPath;
        std::vector<AssemblyOccurrence> occurrences;
        try {
            occurrences = readAssemblyOccurrences(file, _options.cancelled, &savedPath);
        } catch (const std::exception& error) {
            if (_options.strict || (_options.cancelled && _options.cancelled()))
                throw;
            problem(groups, QFileInfo(file).fileName(), QStringLiteral("The assembly could not be read (%1).")
                                                              .arg(QString::fromUtf8(error.what())));
            return;
        }
        stack.push_back(file);
        {
            const auto positional = std::count_if(occurrences.begin(), occurrences.end(),
                                                  [](const AssemblyOccurrence& item) { return item.boundByPosition; });
            if (positional > 0)
                _warnings.append(QStringLiteral("%1: %2 component file(s) were matched to their occurrences by position in the saved "
                                                "table, because no file name equals the occurrence name; check that the parts are the "
                                                "expected ones.").arg(QFileInfo(file).fileName()).arg(positional));
        }
        for (const auto& occurrence : occurrences) {
            checkCancelled();
            if (occurrence.suppressed) {
                ++_suppressed;
                continue;
            }
            // Hidden occurrences are loaded and flagged: the viewer shows them unchecked, and the user can turn them on.
            // Test hook: MODELVIEWER_INVENTOR_SIMULATE_HIDDEN=text treats every occurrence whose name contains the text as
            // hidden in the saved design view (no local sample has one). No effect when unset.
            static const QString simulateHidden = qEnvironmentVariable("MODELVIEWER_INVENTOR_SIMULATE_HIDDEN"); // Unicode, not the ANSI form
            const bool savedHidden = !occurrence.visible ||
                                     (!simulateHidden.isEmpty() && occurrence.name.contains(simulateHidden, Qt::CaseInsensitive));
            const bool hidden = hiddenAbove || savedHidden;
            if (savedHidden && !hiddenAbove)
                ++_hidden;
            const gp_Trsf world = parentWorld * occurrence.placement;
            if (!occurrence.bindingNote.isEmpty())
                _warnings.append(QStringLiteral("%1: %2").arg(occurrence.name, occurrence.bindingNote));
            const ComponentFile resolved = resolveAssemblyComponent(file, savedPath, occurrence.reference);
            if (resolved.path.isEmpty()) {
                problem(groups, occurrence.name, resolved.problem);
                continue;
            }
            if (occurrence.reference.endsWith(QStringLiteral(".iam"), Qt::CaseInsensitive)) {
                if (std::find(stack.begin(), stack.end(), resolved.path) != stack.end()) {
                    problem(groups, occurrence.name, QStringLiteral("The assembly contains itself."));
                    continue;
                }
                groups.push_back({occurrence.name, _nextStep++});
                walk(resolved.path, world, groups, stack, hidden);
                groups.pop_back();
            } else {
                place(resolved.path, occurrence, world, groups, hidden);
            }
        }
        stack.pop_back();
    }

    void place(const QString& file, const AssemblyOccurrence& occurrence, const gp_Trsf& world, const std::vector<Step>& groups,
               bool hidden)
    {
        const PartData& data = part(file);
        if (!data.error.isEmpty()) {
            problem(groups, occurrence.name, QStringLiteral("The part could not be read: %1").arg(data.error));
            return;
        }
        // A part with several bodies is a group named after the occurrence; with one body the occurrence is the leaf.
        const bool several = data.shapes.size() + data.failed.size() > 1;
        std::vector<Step> path = groups;
        if (several)
            path.push_back({occurrence.name, _nextStep++});
        const TopLoc_Location location(world);
        for (std::size_t i = 0; i < data.shapes.size(); ++i) {
            Instance instance;
            instance.path = path;
            instance.name = several ? data.shapeNames[i] : occurrence.name;
            instance.warning = data.shapeWarnings[i];
            if (instance.warning.isEmpty())
                instance.warning = occurrence.bindingNote;
            instance.hidden = hidden;
            instance.order = _order++;
            const std::string tagged = instance.name.toStdString() + kInstanceTag + std::to_string(_instances.size());
            // Moved, not copied: the placed body shares the faces (and their triangulation) of the part.
            _tuples.emplace_back(data.shapes[i].Moved(location), tagged, TopLoc_Location(), data.color);
            _instances.push_back(std::move(instance));
        }
        for (const auto& [name, reason] : data.failed)
            problem(path, several ? name : occurrence.name, reason);
    }

    void regroup(aiScene* scene)
    {
        // The converter also names each mesh after its tuple, so the tag is in the mesh names too.
        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            const std::string meshName = scene->mMeshes[i]->mName.C_Str();
            if (const std::size_t tag = meshName.rfind(kInstanceTag); tag != std::string::npos)
                scene->mMeshes[i]->mName = aiString(meshName.substr(0, tag));
        }
        aiNode* root = scene->mRootNode;
        std::vector<std::pair<int, std::pair<const std::vector<Step>*, aiNode*>>> items; // order, (path, leaf)
        for (unsigned i = 0; i < root->mNumChildren; ++i) {
            aiNode* leaf = root->mChildren[i];
            std::string name = leaf->mName.C_Str();
            const std::size_t tag = name.rfind(kInstanceTag);
            if (tag == std::string::npos)
                continue;
            const std::size_t index = static_cast<std::size_t>(std::strtoull(name.c_str() + tag + 1, nullptr, 10));
            if (index >= _instances.size())
                continue;
            leaf->mName = aiString(name.substr(0, tag));
            const bool warned = !_instances[index].warning.isEmpty();
            setNodeMetadata(leaf, warned ? "warning" : nullptr, _instances[index].warning, _instances[index].hidden);
            items.push_back({_instances[index].order, {&_instances[index].path, leaf}});
        }
        for (const auto& placeholder : _placeholders) {
            auto* leaf = new aiNode(placeholder.name.toStdString());
            setIssue(leaf, "error", placeholder.message);
            items.push_back({placeholder.order, {&placeholder.path, leaf}});
        }
        std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

        TreeGroup top;
        top.name = root->mName.C_Str();
        for (const auto& [order, item] : items) {
            TreeGroup* group = &top;
            for (const Step& step : *item.first) {
                auto& slot = group->byName[std::to_string(step.id)];
                if (!slot) {
                    slot = std::make_unique<TreeGroup>();
                    slot->name = step.name.toStdString();
                    group->entries.push_back({slot.get(), nullptr});
                }
                group = slot.get();
            }
            group->entries.push_back({nullptr, item.second});
        }
        // Detach the old children (the nodes live on under their new parents), then rebuild the root's child list.
        delete[] root->mChildren;
        root->mNumChildren = static_cast<unsigned>(top.entries.size());
        root->mChildren = new aiNode*[top.entries.size()];
        for (std::size_t i = 0; i < top.entries.size(); ++i) {
            const auto& entry = top.entries[i];
            if (entry.group) {
                root->mChildren[i] = materialize(*entry.group, root);
            } else {
                entry.leaf->mParent = root;
                root->mChildren[i] = entry.leaf;
            }
        }
    }

    const AssemblySceneOptions& _options;
    std::map<QString, PartData> _parts; // by canonical file path; node-based, so references stay valid while others load
    BRepToAssimpConverter::StepColorMap _overrides;
    std::vector<ShapeWithNameAndTrsf> _tuples;
    std::vector<Instance> _instances;
    std::vector<Placeholder> _placeholders;
    QStringList _warnings;
    QString _firstProblem;
    int _order = 0;
    int _nextStep = 0;
    int _suppressed = 0;
    int _hidden = 0;
};

} // namespace

AssemblySceneResult buildAssemblyScene(const QString& assemblyPath, const AssemblySceneOptions& options)
{
    SceneBuilder builder(options);
    return builder.build(assemblyPath);
}

} // namespace Inventor
