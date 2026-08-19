#pragma once

#include <QPalette>
#include <QTreeWidget>
#include <QVector>
#include <QWidget>

#include "GltfVariantData.h"

class SceneGraph;
class QPushButton;

// ---------------------------------------------------------------------------
// MaterialVariantsPanel
//
// A tree widget that lets the user switch between KHR_materials_variants for
// every loaded glTF file that carries the extension.
//
// Tree structure:
//   ▼ Astronaut.glb                (top-level: source file display name)
//       ◉ Default                  (always first; resets to the file's original materials)
//       ○ Midnight                 (named variant)
//       ○ Desert
//   ▼ Shoe.glb
//       ◉ Summer
//       ○ Winter
//
// Active variant shown with a filled-circle icon; inactive with empty circle.
// Single-click applies immediately (no Apply button needed).
//
// Transparency: call setDetachedOverlayMode(true) when the navigation panel
// is detached as a ViewportWidget overlay.  Mirrors SceneTreeWidget's approach:
// palette base colours are zeroed, autoFillBackground disabled, and the
// custom paintEvent paints a semi-transparent fill.
// ---------------------------------------------------------------------------
class MaterialVariantsPanel : public QWidget
{
    Q_OBJECT

public:
    // Custom data roles on QTreeWidgetItem
    enum ItemRole
    {
        SourceFileRole  = Qt::UserRole,       // QString  — valid on file-level items
        VariantIndexRole = Qt::UserRole + 1,  // int      — valid on variant items (-1 = Default)
        IsFileItemRole  = Qt::UserRole + 2,   // bool
    };

    explicit MaterialVariantsPanel(QWidget* parent = nullptr);

    void setSceneGraph(SceneGraph* sg);

    // Rebuild the tree from the current SceneGraph variant data.
    void refresh();

    // Enable / disable the frosted-glass rendering used when the navigation
    // panel is floating as a ViewportWidget overlay. Styling only - does NOT
    // reparent. Unlike SceneTreeWidget's per-document instances, this class
    // is one of MainWindow's shared/singleton panels (see MainWindow.h); it
    // must stay parented under MainWindow for its whole lifetime. Nothing
    // currently calls this method or reparents this panel into a document's
    // ViewportWidget - if that's ever wired up, note that Qt would then
    // destroy this singleton along with whichever document it got reparented
    // into, breaking every other document that still expects it to exist.
    void setDetachedOverlayMode(bool enabled);
    void refreshDetachedOverlayTheme();

signals:
    // Emitted when the user clicks a variant item.
    // variantIndex = -1 → reset to file default.
    void variantActivated(const QString& sourceFile, int variantIndex);
    void variantDeleteRequested(const QString& sourceFile, int variantIndex);

    // Emitted when the user captures the file's current live material state
    // as a new variant via the file item's context menu.
    void captureVariantRequested(const QString& sourceFile, const QString& variantName);

    // Emitted when the user sets a named variant as the file's Default
    // (fallback) material via that variant item's context menu.
    void setDefaultVariantRequested(const QString& sourceFile, int variantIndex);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onTreeContextMenuRequested(const QPoint& pos);
    void onCaptureButtonClicked();
    void onSetDefaultButtonClicked();
    void onDeleteButtonClicked();

private:
    void paintEvent(QPaintEvent* event) override;

    QTreeWidgetItem* makeFileItem(const QString& sourceFile,
                                  const QString& displayName) const;
    QTreeWidgetItem* makeVariantItem(const QString& label,
                                     int variantIndex,
                                     bool active) const;

    void markActiveVariant(const QString& sourceFile, int variantIndex);

    // Shared by the file item's context menu action and the bottom
    // "Capture Variant..." button.
    void promptCaptureVariant(const QString& sourceFile);

    // Enables/disables the bottom buttons based on _currentFile and its
    // currently-active variant. Called after refresh() and any click.
    void updateButtonStates();

    QIcon activeIcon()   const;
    QIcon inactiveIcon() const;

    QTreeWidget* _tree        = nullptr;
    QPushButton* _captureButton    = nullptr;
    QPushButton* _setDefaultButton = nullptr;
    QPushButton* _deleteButton     = nullptr;
    SceneGraph*  _sceneGraph  = nullptr;
    bool         _overlayMode = false;

    // The file the bottom buttons act on - the last file interacted with
    // (its own item, or any variant under it), defaulting to the first
    // loaded file. There's no tree-selection concept here (NoSelection
    // mode, single-click activates instead), so this is the closest
    // equivalent to AnimationsPanel's _selectedSourceFile.
    QString _currentFile;

    // Saved state for overlay mode toggle (mirrors SceneTreeWidget)
    QPalette _savedPalette;
    QPalette _savedViewportPalette;
    bool     _savedAutoFill         = false;
    bool     _savedViewportAutoFill = false;
    QString  _savedStyleSheet;
    QColor   _detachedOverlayFillColor = QColor(255, 255, 255, 65);
};
