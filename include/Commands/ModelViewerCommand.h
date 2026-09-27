#pragma once

#include <QUndoCommand>

// Forward declarations
class ModelViewer;
class ViewportWidget;

/**
 * @brief Base class for all undoable commands in ModelViewer
 *
 * This provides a common interface for commands that operate on ModelViewer
 * and its associated ViewportWidget. Derived classes implement specific operations
 * like selection changes, material application, transformations, etc.
 */
class ModelViewerCommand : public QUndoCommand
{
public:
    /**
     * @brief Construct a ModelViewer command
     * @param viewer The ModelViewer instance
     * @param glWidget The ViewportWidget instance
     * @param text Description of the command for undo/redo menu
     */
    explicit ModelViewerCommand(ModelViewer* viewer,
        ViewportWidget* viewportWidget,
        const QString& text = QString());

    virtual ~ModelViewerCommand() = default;

    virtual bool affectsDocument() const { return true; }

protected:
    ModelViewer* _viewer;
    ViewportWidget* _viewportWidget;
};

