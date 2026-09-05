#pragma once

#include <QDialog>
#include <glm/glm.hpp>

#include "UVGenerator.h"
#include "AssImpModelLoader.h"

namespace Ui
{
    class UVGenerationDialog;
}

class ModelViewer;
class QCloseEvent;
class QMdiSubWindow;

// ---------------------------------------------------------------------------
// UVGenerationDialog
//
// Non-modal "Generate UVs" dialog (Tools -> Generate UVs...) - mirrors
// ShrinkWrapDialog's non-modal, per-document, findChild-reuse pattern (see
// ModelViewer::openUVGenerationDialog()) and its working-mesh-list shape
// (meshList/addSelectedButton/removeSelectedButton/generateButton/statusLabel,
// see ShrinkWrapDialog.h's own doc comment for the reasoning) - a curated
// list rather than reacting live to the tree's current selection, so the
// target set survives clicking around the viewport to inspect a result and
// can be built up across multiple separate tree-selection actions.
//
// Unlike ShrinkWrapDialog, Generate does NOT need a "replace previous
// result" mechanism: UV generation mutates each target mesh's UV data in
// place (SceneMesh::setMeshData()) rather than creating a new result mesh,
// so there's nothing to undoably delete before re-running. Every method's
// option-page UI (getUVConfig()/setConfig()/loadLastUsedSettings()/
// saveLastUsedSettings()) is unchanged from the dialog's original modal
// design - only the lifecycle and target-selection changed.
//
// Also owns the "Mark Seams" sub-command: an arm button + list, forwarding
// to ViewportWidget::setSeamMarkingToolArmed()/seamMarks()/removeSeamMarkAt()/
// clearSeamMarks() (SeamMarkingController's actual owner). Marks are session
// state, not persisted - see SeamMarkingController.h's doc comment.
// ---------------------------------------------------------------------------
class UVGenerationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UVGenerationDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);
    ~UVGenerationDialog();

    // Get the selected UV method
    UVMethod getSelectedMethod() const;

    // Get the configured UV parameters
    UVConfig getUVConfig() const;

    // Set initial values (optional - for editing existing settings)
    void setMethod(UVMethod method);
    void setConfig(const UVConfig& config);

    QString getMethodName(UVMethod method) const;

    // Adds whatever's currently selected in the tree to the working list
    // (same logic the Add Selected button runs) - public so
    // ModelViewer::openUVGenerationDialog() can seed the list immediately
    // with an existing tree selection when the dialog is (re)opened. Mirrors
    // ShrinkWrapDialog::addCurrentTreeSelection() exactly.
    void addCurrentTreeSelection();

protected:
    void closeEvent(QCloseEvent* event) override;
    // QDialog's own Escape handling calls reject(), which goes straight to
    // done()/hide() WITHOUT ever raising a QCloseEvent - same gotcha
    // ShrinkWrapDialog::reject() documents. This override exists only so
    // saveLastUsedSettings() still runs on an Escape-closed dialog.
    void reject() override;

private slots:
    void onMethodChanged(int index);
    void onRelaxationToggled(bool enabled);
    void onRelaxationToggled_Smart(bool enabled);
    void onCylAutoDetectAxisToggled(bool autoDetect);
    void onSphereAutoDetectAxisToggled(bool autoDetect);
    void onTorusAutoDetectAxisToggled(bool autoDetect);
    void onRemoveSelectedClicked();
    void onListSelectionChanged();
    void onGenerateClicked();
    void onResetDefaultsClicked();

    // ---- Seam marking (see SeamMarkingController's doc comment for the dialog-scoped design) --
    void onMarkSeamsToggled(bool armed);
    void onRemoveSeamMarkClicked();
    void onClearSeamMarksClicked();
    void onSeamMarkListSelectionChanged();
    // Refreshes seamMarkList from ViewportWidget::seamMarks() - connected to
    // ViewportWidget::seamMarksChanged().
    void refreshSeamMarkList();
    // Keeps markSeamsButton's checked state in sync when the tool is disarmed
    // externally (Escape, or arming Measure/Annotate instead) - connected to
    // ViewportWidget::seamToolArmedChanged().
    void onSeamToolArmedChanged(bool armed);

    // Hides/shows this dialog as its own document's MDI subwindow loses/gains focus - mirrors
    // RtRenderDialog's identical mechanism (see the constructor's connect() for why).
    void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);


private:
    // Helper methods
    void setupConnections();
    void updateOptionsPage(int methodIndex);
    void adjustDialogSize();  // Auto-resize based on content

    // Enables Generate only while the working list holds at least one item -
    // mirrors ShrinkWrapDialog::updateActionButtonsEnabled(). Called after
    // every change to the mesh list's contents (add, remove).
    void updateGenerateButtonEnabled();

	void loadLastUsedSettings();
	void saveLastUsedSettings();

private:
    ModelViewer* _modelViewer; // not owned - dialog is a child of the ModelViewer's window
    Ui::UVGenerationDialog* ui;
};
