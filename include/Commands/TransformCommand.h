#pragma once

#include "ModelViewerCommand.h"
#include <QQuaternion>
#include <QVector3D>
#include <QMap>
#include <QSet>
#include <QUuid>

/**
 * @brief State of a mesh's transformation
 */
struct TransformState
{
    QVector3D translation;
    QVector3D rotation;
    QVector3D scale;
    QQuaternion rotationQuat;
    bool hasExactRotation = false;

    TransformState()
        : translation(0, 0, 0)
        , rotation(0, 0, 0)
        , scale(1, 1, 1)
        , rotationQuat()
        , hasExactRotation(false)
    {
    }

    TransformState(const QVector3D& trans, const QVector3D& rot, const QVector3D& scl)
        : translation(trans)
        , rotation(rot)
        , scale(scl)
        , rotationQuat()
        , hasExactRotation(false)
    {
    }

    TransformState(const QVector3D& trans, const QVector3D& rot, const QVector3D& scl, const QQuaternion& quat)
        : translation(trans)
        , rotation(rot)
        , scale(scl)
        , rotationQuat(quat)
        , hasExactRotation(true)
    {
    }
};

/**
 * @brief Undoable command for mesh transformations
 *
 * Handles transformation of one or more meshes (translation, rotation, scale).
 *
 * Each mesh can have different transformation values, so states are stored
 * per-mesh using UUID as key.
 */
class TransformCommand : public ModelViewerCommand
{
public:
    enum class Target
    {
        UserTransform,
        ExplodedViewTransform
    };

    /**
     * @brief Construct a transform command
     * @param viewer The ModelViewer instance
     * @param glWidget The ViewportWidget instance
     * @param meshUuids The UUIDs of meshes to transform
     * @param newTranslation New translation value for all meshes
     * @param newRotation New rotation value for all meshes (degrees)
     * @param newScale New scale value for all meshes
     * @param text Description (default: "Transform")
     */
    TransformCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QVector<QUuid>& meshUuids,
        const QVector3D& newTranslation,
        const QVector3D& newRotation,
        const QVector3D& newScale,
        const QString& text = QObject::tr("Transform"));

    TransformCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QMap<QUuid, TransformState>& oldStates,
        const QMap<QUuid, TransformState>& newStates,
        const QString& text = QObject::tr("Transform"),
        bool fitView = true,
        Target target = Target::UserTransform);

    void undo() override;
    void redo() override;

    /**
     * @brief Command ID for merging
     * @return Unique ID for TransformCommand (8)
     */
    int id() const override { return 8; }

    /**
     * @brief Get all UUIDs referenced by this command
     * @return Set of mesh UUIDs that are transformed
     *
     * Used by cleanup system to determine if command references deleted meshes
     */
    QSet<QUuid> getReferencedUuids() const;

private:
    QMap<QUuid, TransformState> _oldStates;  // Transform states before command
    QMap<QUuid, TransformState> _newStates;  // Transform states after command
    bool _fitView = true;
    Target _target = Target::UserTransform;

    /**
     * @brief Apply transformation states to meshes
     * @param states Map of UUID to TransformState
     */
    void applyTransformStates(const QMap<QUuid, TransformState>& states);
};
