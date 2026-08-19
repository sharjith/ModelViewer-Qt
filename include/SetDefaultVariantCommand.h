#pragma once

#include "ModelViewerCommand.h"
#include "GltfVariantData.h"
#include "Material.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QUuid>
#include <QVector>

/**
 * @brief Undoable command that replaces a file's "Default" (fallback)
 * material with the currently-selected variant's material, for every mesh
 * in that file ("Set as Default" in the Variants tab).
 *
 * "Default" is the material stored at each mesh's allVariantMaterials()
 * key == getOriginalMaterialIndex() - this is exactly what
 * AssImpMeshExporter::exportedDefaultMaterial() writes as the primitive's
 * base material on glTF/GLB export, independent of whatever variant happens
 * to be live in the viewer at export time.
 *
 * A named variant can legitimately alias that same map slot: if the glTF
 * author set a variant's material to literally be the primitive's own
 * default material (a common pattern - "Default" and that variant look
 * identical on import), MaterialVizState::materialForVariant() resolves
 * both Default (-1) and that variant through the exact same
 * allVariantMaterials() key. Overwriting the slot for Default would then
 * silently repaint that variant too. So before overwriting, this command
 * de-aliases any other variant mapping that currently shares the Default
 * key - giving it its own independent copy of the material first - which
 * also means the per-file GltfVariantData::meshVariantMappings needs
 * updating alongside the per-mesh material map, not just the material map.
 */
class SetDefaultVariantCommand : public ModelViewerCommand
{
public:
    SetDefaultVariantCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QString& sourceFile,
        int variantIndex,
        const QString& text = QObject::tr("Set Default Variant"));

    void undo() override;
    void redo() override;

private:
    QString _sourceFile;
    int     _oldActiveVariant = -1;

    GltfVariantData _oldVariantData;
    GltfVariantData _newVariantData;

    QHash<QUuid, QMap<int, Material>>         _oldAllVariantMaterialsByMesh;
    QHash<QUuid, QVector<GltfVariantMapping>> _oldMappingsByMesh;

    QHash<QUuid, QMap<int, Material>>         _newAllVariantMaterialsByMesh;
    QHash<QUuid, QVector<GltfVariantMapping>> _newMappingsByMesh;
};
