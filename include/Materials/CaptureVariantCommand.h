#pragma once

#include "ModelViewerCommand.h"
#include "GltfVariantData.h"
#include "Material.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QUuid>
#include <QVector>

/**
 * @brief Undoable command that captures the live material state of every
 * mesh belonging to a source file as a new KHR_materials_variants variant
 * ("Capture Current as Variant..." in the Variants tab).
 *
 * Follows the same before/after-snapshot shape as
 * MetadataDeleteCommand::Kind::Variant, but in the opposite direction: it
 * appends one new GltfVariantMapping entry (keyed by a fresh, per-mesh-local
 * material index) to every mesh in sourceFile, using each mesh's *current*
 * Material as that variant's material. Files with no prior variant data get
 * a synthesized "Default" entry (keyed by getOriginalMaterialIndex()) from
 * the same current material, so switching back to Default after the first
 * capture keeps showing today's look.
 */
class CaptureVariantCommand : public ModelViewerCommand
{
public:
    CaptureVariantCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QString& sourceFile,
        const QString& variantName,
        const QString& text = QObject::tr("Add Variant"));

    void undo() override;
    void redo() override;

    QSet<QUuid> getReferencedUuids() const;

private:
    QString _sourceFile;
    int     _newVariantIndex   = -1;
    int     _oldActiveVariant  = -1;

    GltfVariantData _oldVariantData;
    GltfVariantData _newVariantData;

    QHash<QUuid, QVector<GltfVariantMapping>> _oldMappingsByMesh;
    QHash<QUuid, QMap<int, Material>>         _oldAllVariantMaterialsByMesh;

    QHash<QUuid, QVector<GltfVariantMapping>> _newMappingsByMesh;
    QHash<QUuid, QMap<int, Material>>         _newAllVariantMaterialsByMesh;
};
