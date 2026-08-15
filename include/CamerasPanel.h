#pragma once

#include <QPalette>
#include <QTreeWidget>
#include <QWidget>

class ViewportWidget;
class SceneGraph;

// ---------------------------------------------------------------------------
// CamerasPanel
//
// A tree widget that lets the user switch between the viewer's built-in
// system camera and any glTF-defined cameras in the loaded file(s).
//
// The panel is shown only when at least one glTF camera is present in the
// current scene (it is a conditional navigation sub-tab, like Variants and
// Animations).
//
// Tree structure:
//   ◉ System Camera              (permanent top-level item)
//   ▼ Astronaut.glb              (top-level file group, bold, non-selectable)
//       ○ CameraTop              (named glTF camera)
//       ○ CameraClose
//
// Active camera shown with a filled-circle icon; inactive with empty circle.
// Single-click switches the viewport camera immediately.
//
// The panel supports the same detached-overlay transparency mode used by
// MaterialVariantsPanel and AnimationsPanel.
// ---------------------------------------------------------------------------
class CamerasPanel : public QWidget
{
    Q_OBJECT

public:
    // Custom data roles on QTreeWidgetItem
    enum ItemRole
    {
        SourceFileRole   = Qt::UserRole,        // QString  — valid on file-level items and camera items
        CameraIndexRole  = Qt::UserRole + 1,    // int      — camera index within the file, or -1 for System Camera
        IsFileItemRole   = Qt::UserRole + 2,    // bool     — true on file-group items
        IsSystemCamRole  = Qt::UserRole + 3,    // bool     — true on the System Camera item
    };

    explicit CamerasPanel(QWidget* parent = nullptr);

    void setSceneGraph(SceneGraph* sg);
    void setViewportWidget(ViewportWidget* viewportWidget);

    // Rebuild the tree from the current SceneGraph camera data.
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
    // Emitted when the user clicks a glTF camera item.
    void gltfCameraActivated(const QString& sourceFile, int cameraIndex);
    void gltfCameraDeleteRequested(const QString& sourceFile, int cameraIndex);

    // Emitted when the user clicks the System Camera item.
    void systemCameraRequested();

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onTreeContextMenuRequested(const QPoint& pos);

private:
    void paintEvent(QPaintEvent* event) override;

    QTreeWidgetItem* makeSystemCameraItem(bool active) const;
    QTreeWidgetItem* makeFileItem(const QString& sourceFile,
                                  const QString& displayName) const;
    QTreeWidgetItem* makeCameraItem(const QString& label,
                                    const QString& sourceFile,
                                    int cameraIndex,
                                    bool active) const;

    void markActive(const QString& sourceFile, int cameraIndex, bool isSystemCam);

    QIcon activeIcon()   const;
    QIcon inactiveIcon() const;

    QTreeWidget* _tree       = nullptr;
    SceneGraph*  _sceneGraph = nullptr;
    ViewportWidget*    _viewportWidget   = nullptr;
    bool         _overlayMode = false;

    // Saved state for overlay mode toggle (mirrors MaterialVariantsPanel)
    QPalette _savedPalette;
    QPalette _savedViewportPalette;
    bool     _savedAutoFill         = false;
    bool     _savedViewportAutoFill = false;
    QString  _savedStyleSheet;
    QColor   _detachedOverlayFillColor = QColor(255, 255, 255, 65);
};
