#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QUuid>

class ModelViewer;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// ---------------------------------------------------------------------------
// ReportExportDialog
//
// Modal "Export Report" dialog (Tools -> Export Report...) - produces a PDF
// containing the user's already-captured camera views ("Capture View" in the
// Cameras tab), each with its measurement/annotation overlays baked in (they
// render into the same framebuffer the normal paint path already draws to,
// so a plain clean screenshot captures them for free), plus a table of every
// current measurement's and annotation's summary text.
//
// Deliberately modal, unlike MeasurementDialog/AnnotationDialog's non-modal
// findChild-reuse pattern - there is no persistent tool state to keep in
// sync with the viewport here (no armed tool, no live selection), it is a
// one-shot batch action, so QDialog::exec() is the simpler, correct fit.
//
// No manual view-placement/layout editor - deliberately out of scope (this
// app's philosophy is a CAD inspector producing documents for a human
// reader, not a drawing-sheet composer - see the design discussion this
// dialog came out of). Layout is fully automatic: QTextDocument assembles
// the images/tables as flowing HTML and QPdfWriter paginates it.
//
// Reuses only already-public API - no changes needed anywhere else in the
// app: ViewportWidget::activateGltfCamera()/captureCleanRayTracedImage()/
// resetToSystemCamera(), SceneGraph::gltfCameraDataForFile(
// capturedViewsSourceFileKey()), ViewportWidget::measurementSummaryText(),
// and Annotation::text directly.
// ---------------------------------------------------------------------------
class ReportExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReportExportDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

private slots:
    void onExportClicked();
    // Keeps the Export button's enabled state honest - disabled until
    // there's something to actually put in the PDF (see onExportClicked()'s
    // guard for the exact rule).
    void updateExportEnabled();
    // Double-click on a view row -> opens a small modal checklist letting
    // the user choose which measurements/annotations THAT view's captured
    // image should show, overriding the document's current global
    // visibility just for this report (see generateReport()). Mirrors the
    // "double-click opens a focused editor" convention ViewportWidget's own
    // double-click-a-measurement/annotation gesture just established.
    void editViewVisibility(QListWidgetItem* item);

private:
    // Builds the PDF at `path` from the currently checked views/options.
    // Returns false (and leaves a message for the caller to show) on
    // failure. Split out from onExportClicked() only so the "ask where to
    // save, then generate" and "actually generate" concerns aren't tangled
    // in one method.
    bool generateReport(const QString& path);

    // Per-view show/hide override, set via editViewVisibility(). A view
    // with no entry here just uses whatever's globally visible in the
    // document at export time (today's plain behavior) - only a view the
    // user has explicitly customized gets an entry.
    struct ViewVisibilityOverride
    {
        QSet<QUuid> visibleMeasurementIds;
        QSet<QUuid> visibleAnnotationIds;
    };

    ModelViewer* _modelViewer; // not owned - dialog is a transient child of the ModelViewer document

    QListWidget* _viewsList;
    QLabel*      _noViewsLabel;
    QCheckBox*   _includeTableCheck;
    QLineEdit*   _titleEdit;
    QPushButton* _exportButton;
    QPushButton* _cancelButton;

    // Keyed by the view's row/camera index (same index generateReport()
    // already uses to pair _viewsList rows with the captured-views vector).
    QHash<int, ViewVisibilityOverride> _perViewOverrides;
};
