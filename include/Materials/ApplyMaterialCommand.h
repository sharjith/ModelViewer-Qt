#pragma once

#include "ModelViewerCommand.h"
#include "Material.h"
#include <QMap>
#include <QSet>
#include <QUuid>
#include <QString>

/**
 * @brief Undoable command for setting material from predefined library
 *
 * Handles setting complete materials to one or more meshes. Typically triggered
 * when user selects a predefined material from the MaterialLibraryWidget (e.g.,
 * Gold, Aluminum, Glass, etc.).
 *
 * Stores the complete Material state for each affected mesh, allowing full
 * undo/redo of material changes. This includes all properties: colors, PBR
 * parameters, textures, rendering settings, etc.
 *
 * The command text shown in the undo stack includes the material name for clarity.
 */
class ApplyMaterialCommand : public ModelViewerCommand
{
public:
    /**
     * @brief Construct a set material command
     * @param viewer The ModelViewer instance
     * @param glWidget The ViewportWidget instance
     * @param meshUuids The UUIDs of meshes to apply material to
     * @param newMaterial The material to apply
     * @param materialName Optional name for undo stack display (e.g., "Gold")
     * @param text Base description (default: "Set Material")
     *
     * The constructor captures the current material state of all affected meshes
     * before any changes are made. This allows perfect restoration on undo.
     *
     * If materialName is provided, the command text becomes "Set Material: Gold"
     * instead of just "Set Material", making the undo stack more informative.
     */
    ApplyMaterialCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QVector<QUuid>& meshUuids,
        const Material& newMaterial,
        const QString& materialName = QString(),
        const QString& text = QObject::tr("Set Material"));

    void undo() override;
    void redo() override;

    /**
     * @brief Command ID for merging
     * @return Unique ID for ApplyMaterialCommand (9)
     */
    int id() const override { return 9; }

    /**
     * @brief Get all UUIDs referenced by this command
     * @return Set of mesh UUIDs affected by this material change
     *
     * Used by cleanup system to determine if command references deleted meshes
     */
    QSet<QUuid> getReferencedUuids() const;

private:
    QMap<QUuid, Material> _oldMaterials;  // Materials before command
    QMap<QUuid, Material> _newMaterials;  // Materials after command
    QString _materialName;                   // For display in undo stack

    /**
     * @brief Apply materials to meshes
     * @param materials Map of UUID to Material
     *
     * Helper method that applies a set of materials to their corresponding meshes.
     * Handles material application, transmission flag updates, and view refresh.
     */
    void applyMaterials(const QMap<QUuid, Material>& materials);
};
