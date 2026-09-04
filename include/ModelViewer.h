#pragma once


#include "ui_ModelViewer.h"

#include "ViewportWidget.h"
#include "Material.h"
#include "SceneGraph.h"
#include "SceneTreeWidget.h"
#include "UVPromptDialog.h"
#include "AssImpModelLoader.h"
#include "ApplyMaterialCommand.h"
#include "RenameMeshCommand.h"
#include "AnimationsPanel.h"
#include "CamerasPanel.h"
#include "ExplodedViewPanel.h"
#include "MaterialPropertiesPanel.h"
#include "ObjectTransformPanel.h"
#include "VisualizationEnvironmentPanel.h"
#include "SceneClipboard.h"
#include "CutCommand.h"
#include "MaterialVariantsPanel.h"
#include "TextureDebugPanel.h"

#include <QUndoStack>

#include <functional>

class QTabWidget;
class QToolButton;
class QFrame;

struct UVDialogResult
{
	UVMethod method = UVMethod::None;
};

namespace Mvf
{
struct Document;
struct MVFPackage;
}

class ModelViewer : public QWidget, public Ui::ModelViewer
{
	Q_OBJECT
public:
	ModelViewer(QWidget* parent = 0);
	~ModelViewer();

	void retranslateUI();

	void close();

	ViewportWidget*    getViewportWidget()    const { return _viewportWidget; }
	SceneGraph*  sceneGraph()   const { return _sceneGraph; }
	QMap<QString, CachedMaterial>* getMaterialCache() { return &_materialCache; }
	void registerOwnedUnsavedMaterial(const QString& materialKey) { _ownedUnsavedMaterials.insert(materialKey); }

	void setMaterialToSelectedItems(const Material& mat);
	void setTexturesToSelectedItems(const Material& mat);
	void setTextureSamplersToSelectedItems(const Material* material, Material::TextureType type);

	void setTransformation();
	void resetTransformation();
	void syncLightPositionUiToScene();

	SceneTreeWidget* getTreeModel() { return treeWidgetModel; }

	// Aliases to MainWindow's single shared instances (used to be owned
	// per-document via ui/ModelViewer.ui) - set once in the constructor,
	// rebound to whichever document is active by
	// MainWindow::rebindSharedPanelsTo(), not by ModelViewer itself. Public
	// to match their old effective accessibility as public members inherited
	// from Ui::ModelViewer (RtRenderDialog/ViewportWidget read these).
	MaterialPropertiesPanel* predefinedMaterialsPanel = nullptr;
	ObjectTransformPanel* objectTransformPanel = nullptr;
	QTabWidget* tabWidgetVizAttribs = nullptr;
	VisualizationEnvironmentPanel* visualizationEnvironmentPanel = nullptr;

	void updateTransformationValues();
	void resetTransformationValues();

	void switchToRealisticRendering();

	static QString getLastOpenedDir();
	static void setLastOpenedDir(const QString& lastOpenedDir);

	static QString getLastSelectedFilter();
	static void setLastSelectedFilter(const QString& lastSelectedFilter);

	void setCurrentFile(const QString& fileName);
	QString currentFile() const;

	bool loadFile(const QString& fileName);

	void importModel();
	void exportModel();

	bool saveToFile(const QString& fileName);
	bool loadFromFile(const QString& fileName);

	bool documentModified() const { return _documentModified; }
	void setDocumentModified(bool modified = true);
	void markNonUndoDocumentModified();

	bool save();
	bool saveAs();

	void closeEvent(QCloseEvent* event);

	void setDocumentSaved(bool saved = true);
	bool isDocumentSaved() const { return _documentSaved; }

	void selectAll();
	void deselectAll();
	void deselectAllWithUndo();

	// For UV generation dialog user selection
	static UVDialogResult askUserForUVMethod(QWidget* parent);

	// Skybox index accessors for VisualizationEnvironmentPanel
	int getSkyBoxLDRIIndex() const { return _skyBoxLDRIIndex; }
	int getSkyBoxHDRIIndex() const { return _skyBoxHDRIIndex; }
	void setSkyBoxLDRIIndex(int index) { _skyBoxLDRIIndex = index; }
	void setSkyBoxHDRIIndex(int index) { _skyBoxHDRIIndex = index; }

	// Undo/Redo interface (called by MainWindow)
	bool hasUndo() const;
	bool hasRedo() const;
	void undo();
	void redo();

	// Opens (or raises) the Texture Debug Panel for the current selection.
	// Called by MainWindow when Visualization → Texture Debugger is triggered.
	void showTextureDebugPanel();

	// Undo stack access
	QUndoStack* getUndoStack() const { return _undoStack; }

	// Selection helpers (used by SelectionCommand)
	void setSelectionWithUndo(const QSet<int>& newSelection);
	void setSelectionWithoutUndo(const QSet<int>& selection);
	// Selection helpers (for DuplicateCommand)
	void setSelectionWithoutUndo(const QSet<QUuid>& uuids);

	// Visibility helpers (used by VisibilityCommand)
	QSet<QUuid> getVisibleUuids() const;
	void setVisibilityWithUndo(const QSet<QUuid>& newVisibleUuids,
		const QString& commandText);
	void setVisibilityWithoutUndo(const QSet<QUuid>& visibleUuids);

signals:
	void documentModifiedChanged(bool modified);
	// Emitted from updateVisibilityUiFromState() alongside its own overlay
	// labelMeshCount update - lets MainWindow's Document dock mirror the
	// same count for whichever document is currently active, without
	// polling.
	void visibleMeshCountChanged(int count);

public:
	bool hasSelection() const;
	std::vector<int> getSelectedIDs() const;

	// Selection helpers (for DuplicateCommand)
	QSet<QUuid> getSelectedUuids() const;

	// Attaches the navigation tree as a permanent transparent overlay on this
	// document's own viewport - called once from the constructor; there's no
	// docked/detached toggle anymore, only collapsed/expanded (see
	// _navCollapseButton below). updateNavigationOverlayGeometry()
	// repositions/resizes it on viewport resize (see resizeEvent()) and on
	// collapse/expand.
	void attachNavigationOverlay();
	void updateNavigationOverlayGeometry();

	// Applies material to meshUuid via an undo-able ApplyMaterialCommand.
	// Extracted from what used to be an inline lambda on
	// MaterialPropertiesPanel::meshMaterialApplied, back when that panel was
	// constructed once per document rather than as a single shared instance
	// MainWindow dispatches to whichever document is currently active.
	void applyMeshMaterial(const QUuid& meshUuid, const Material& material);

	// Apply a named variant to all meshes from the given source file.
	// variantIndex = -1 resets to the file's default material assignments.
	void applyVariant(const QString& sourceFile, int variantIndex);

	// Extracted from what used to be inline lambdas on
	// MaterialVariantsPanel::variantDeleteRequested/
	// AnimationsPanel::clipDeleteRequested/
	// CamerasPanel::gltfCameraDeleteRequested, same reasoning as
	// applyMeshMaterial() above - now dispatched from MainWindow via
	// activeMdiChild() instead of connected directly per-document.
	void deleteVariant(const QString& sourceFile, int variantIndex);
	void deleteAnimationClip(const QString& sourceFile, int clipIndex);
	void deleteGltfCamera(const QString& sourceFile, int cameraIndex);

	// Capture the live material state of every mesh in sourceFile as a new
	// named KHR_materials_variants variant ("Capture Current as Variant..."
	// in the Variants tab). Undoable via CaptureVariantCommand.
	void captureVariant(const QString& sourceFile, const QString& variantName);

	// Replace sourceFile's "Default" (fallback) material with variantIndex's
	// material, for every mesh in that file ("Set as Default" in the
	// Variants tab). This is what glTF/GLB export writes as each
	// primitive's base material. Undoable via SetDefaultVariantCommand.
	void setVariantAsDefault(const QString& sourceFile, int variantIndex);

	// Capture the live camera pose as a new named view ("Capture View" in
	// the Cameras tab), stored as a GltfCameraEntry under SceneGraph's
	// synthetic capturedViewsSourceFileKey() bucket. Activation and
	// deletion reuse the existing glTF-camera signals/methods
	// (gltfCameraActivated -> ViewportWidget::activateGltfCamera(),
	// deleteGltfCamera()) - no separate bookmark path, see GltfCameraData.h.
	// Undoable via CaptureCameraCommand.
	void captureCameraView(const QString& name);

	// Add a completed Measurement (Point or Distance) to SceneGraph's
	// document-level list. Undoable via AddMeasurementCommand. Called from
	// ViewportWidget::handleMeasurementClick() once a click finishes a
	// measurement, instead of writing to SceneGraph directly.
	void addMeasurement(const Measurement& measurement);

	// Delete one measurement by id. Undoable via DeleteMeasurementCommand.
	void deleteMeasurement(const QUuid& measurementId);

	// Deletes every measurement currently selected in the viewport (see
	// ViewportWidget::selectedMeasurementIds()) - the Measurement dialog's
	// results list supports multi/shift-select, so this can be more than
	// one. Wrapped in a single undo macro when there's more than one, so
	// one Undo restores the whole batch rather than needing N presses.
	// Wired to the same Key_Delete shortcut as mesh deletion in MainWindow -
	// a selected measurement takes priority over a selected mesh.
	void deleteSelectedMeasurements();

	// Add a completed Annotation to SceneGraph's document-level list.
	// Undoable via AddAnnotationCommand. Called from
	// AnnotationController::handleAnnotationClick() once a click places a
	// note, instead of writing to SceneGraph directly. Same guard/shape as
	// addMeasurement() above.
	void addAnnotation(const Annotation& annotation);

	// Delete one annotation by id. Undoable via DeleteAnnotationCommand.
	void deleteAnnotation(const QUuid& annotationId);

	// Deletes every annotation currently selected in the viewport - same
	// multi-select/undo-macro shape as deleteSelectedMeasurements().
	void deleteSelectedAnnotations();

	// Commits an annotation's text edit. Undoable via AnnotationTextCommand
	// (unlike SceneGraph::setAnnotationVisible()'s plain display toggle -
	// text is core content). Called from AnnotationDialog's details pane on
	// focus-out, not per-keystroke; no-ops if the text hasn't actually
	// changed so an unedited focus-out doesn't push a no-op undo step.
	void setAnnotationText(const QUuid& annotationId, const QString& text);

	// Shows (findChild-reuse) or creates the non-modal MeasurementDialog for
	// this document - the single implementation behind both the Tools ->
	// Measure... menu action and ViewportWidget::mouseDoubleClickEvent()'s
	// double-click-a-measurement gesture, so the two can't drift apart. If
	// selectId is non-null, selects it in the viewport afterward - both
	// dialogs already sync their list selection AND scroll to it off that
	// signal (see MeasurementDialog/AnnotationDialog::onViewportSelectionChanged()),
	// so this is enough to land the user on the right row with no dialog-side
	// changes needed.
	void openMeasurementDialog(const QUuid& selectId = QUuid());
	// Same role as openMeasurementDialog() above, for AnnotationDialog.
	void openAnnotationDialog(const QUuid& selectId = QUuid());

	// ---- Measurement/Annotation visibility (undo-integrated tier) --------
	// Mirrors the mesh visibility split exactly: getVisibleUuids()/
	// setVisibilityWithUndo()/setVisibilityWithoutUndo() (see those methods'
	// own doc comments) has a context-menu/keyboard tier that's undo-
	// integrated (VisibilityCommand) and a scene-tree-checkbox tier that
	// isn't. The Measurement/Annotation dialogs' own checkbox lists already
	// ARE that second, non-undo tier (SceneGraph::setMeasurementVisible()/
	// setAnnotationVisible(), called directly from MeasurementDialog::
	// onResultItemChanged()/AnnotationDialog::onResultItemChanged() -
	// unchanged). These four methods are the missing first tier, for the new
	// viewport context-menu Hide/Show actions.
	QSet<QUuid> getVisibleMeasurementUuids() const;
	void setMeasurementVisibilityWithUndo(const QSet<QUuid>& newVisibleIds, const QString& commandText);
	void setMeasurementVisibilityWithoutUndo(const QSet<QUuid>& visibleIds);
	void hideSelectedMeasurements();
	void showSelectedMeasurements();

	QSet<QUuid> getVisibleAnnotationUuids() const;
	void setAnnotationVisibilityWithUndo(const QSet<QUuid>& newVisibleIds, const QString& commandText);
	void setAnnotationVisibilityWithoutUndo(const QSet<QUuid>& visibleIds);
	void hideSelectedAnnotations();
	void showSelectedAnnotations();

public slots:
	void updateDisplayList();
	void updateSelectionStatusMessage();
	void showAllItems();
	void showSelectedItems();
	void showOnlySelectedItems();
	void hideAllItems();
	void hideSelectedItems();
	void centerScreen();
	void copySelectedItems();
	void cutSelectedItems();
	void pasteIntoSelectedNode(const SceneNode* targetNode);

	// The clipboard is shared across every open document (static) so a
	// Copy/Cut in one document's window can be pasted into another's - see
	// s_clipboardSourceViewer's doc comment for why this needs a source-
	// document guard rather than being a bare shared list.
	static bool hasClipboardContent() { return !s_clipboard.isEmpty(); }
	static quint64 currentClipboardGeneration() { return s_clipboardGeneration; }
	void duplicateSelectedItems();
	// For every selected mesh containing more than one spatially-disconnected
	// triangle island (see SceneMesh::findConnectedTriangleGroups()), replaces
	// it with one independently-selectable fragment mesh per island. Meshes
	// that are already a single connected piece are left untouched and
	// reported separately. Undoable (SplitByConnectivityCommand), one per
	// split mesh, grouped into a single undo macro when more than one mesh
	// is split at once.
	void splitSelectedMeshesByConnectivity();

	// Inverse of splitSelectedMeshesByConnectivity(): clusters the selected
	// meshes by shared-vertex (world-space) adjacency, and replaces each
	// touching cluster of 2+ meshes with one combined mesh (see SceneMesh::
	// mergeMeshes()). A cluster merges unconditionally if every member
	// shares the same source file, tracked original material index, and
	// primitive mode; if any touching cluster has mixed materials, asks
	// once (covering every mismatched cluster) whether to merge them anyway
	// by cascading each cluster's first mesh's material onto the rest, or
	// leave them unmerged - never changes appearance silently. Undoable
	// (MergeByAdjacencyCommand), one per merged cluster, grouped into a
	// single undo macro when more than one cluster is merged at once.
	void mergeSelectedMeshesByAdjacency();

	// Unconditional counterpart to mergeSelectedMeshesByAdjacency(): combines
	// the ENTIRE current selection into one mesh regardless of whether the
	// meshes touch - no clustering, just the same material-compatibility
	// gate (same source file + tracked original material index + primitive
	// mode across every selected mesh) applied once to the whole set. On a
	// mismatch, asks once whether to merge anyway by cascading the first
	// mesh's material, same as the by-adjacency path. Undoable (a single
	// MergeByAdjacencyCommand - the command itself doesn't care how its
	// source set was chosen). Plain vertex/index concatenation via
	// SceneMesh::mergeMeshes() - overlapping geometry stays overlapping
	// triangle soup, not a real solid. See unionSelectedMeshes() for that.
	void mergeSelectedMeshes();

	// True-solid counterpart to mergeSelectedMeshes(): identical selection
	// gathering, material-compatibility gate, and undo bookkeeping, but
	// combines geometry via SceneMesh::booleanUnionMeshes() - a real CGAL
	// boolean union (corefine_and_compute_union(), with a repair pipeline
	// establishing its documented preconditions), silently falling back to
	// plain concatenation if the input can't be repaired into something
	// corefinement can use. Kept as its own separate command rather than
	// folded into "Merge Selected" - unlike plain concatenation, a real
	// union changes the merged mesh's actual geometry (e.g. it hard-splits
	// vertex normals across a >=15 degree crease at any new seam, which
	// visibly changes shading), so it isn't a safe drop-in replacement for
	// the old, purely non-destructive merge.
	void unionSelectedMeshes();

	// Organizational "Group": creates a new (empty) assembly SceneNode as a
	// sibling of the first selected mesh's own owner node, and moves every
	// selected mesh's UUID into it - no geometry is touched, this is pure
	// scene-tree reorganization (the inversion of Split by Connectivity in
	// spirit only - that one splits GEOMETRY, this one only reorganizes
	// HIERARCHY). Works on a selection of just one mesh too (wraps it alone
	// in a new group), unlike Merge/Split which need 2+. Undoable
	// (GroupMeshesCommand).
	void groupSelectedMeshes();

	// Shrink Wrap: opens the non-modal ShrinkWrapDialog (Tools -> Shrink
	// Wrap...), findChild-reuse-or-create/show/raise, same pattern as
	// openMeasurementDialog()/openAnnotationDialog() below. Whatever's
	// currently tree-selected is seeded into the dialog's working list
	// immediately (dialog->addCurrentTreeSelection()) - opening with a
	// selection already made shouldn't require re-picking it inside the
	// dialog.
	void openShrinkWrapDialog();

	// The Shrink Wrap dialog's one-line bridge into the undo stack (same
	// "dialog never touches _undoStack directly" convention as
	// addMeasurement()/addAnnotation() below) - called once per Generate
	// result, immediately after ShrinkWrapDialog::onGenerateClicked() builds
	// it (matching MeasurementDialog: every result is independently
	// undoable without closing the dialog first - confirmed bug when this
	// was instead deferred to closeEvent() as a "live preview"). wrapNode/
	// wrapParent/wrapPosition/wrappedMeshUuid must already be live in the
	// scene (attached/inserted) - this only pushes the command that
	// remembers how to undo/redo that already-done attachment, same
	// "already happened, command just replays it" convention as
	// GroupMeshesCommand/MergeByAdjacencyCommand.
	void commitShrinkWrap(SceneNode* wrapNode, SceneNode* wrapParent, int wrapPosition,
	                       const QUuid& wrappedMeshUuid, const QSet<QUuid>& originalSelection);

	// Undoably deletes a set of previously-committed tool results (Shrink
	// Wrap/Subdivision) - the "Replace previous result" checkbox's delete
	// half. Called from ShrinkWrapDialog/SubdivisionDialog right before a
	// new Generate, in place of the old discardAllPreviews() (which deleted
	// outside the undo stack entirely, back when results were uncommitted
	// scratch state). A thin push of DeleteMeshCommand, same as
	// deleteSelectedItems() uses, just without its selection/confirmation-
	// dialog steps - meshUuids is caller-supplied directly.
	void replaceToolResults(const QVector<QUuid>& meshUuids, const QString& text);

	// Subdivide Surface: opens the non-modal SubdivisionDialog (Tools ->
	// Subdivide Surface...), same findChild-reuse-or-create/show/raise/
	// seed-with-tree-selection pattern as openShrinkWrapDialog() above.
	void openSubdivisionDialog();

	// The Subdivision dialog's one-line bridge into the undo stack - same
	// convention and immediate-per-result timing as commitShrinkWrap()
	// above, and in fact reuses the exact same ShrinkWrapCommand class (it's
	// already fully generic: "add one new node+mesh, know how to undo/redo
	// that" - nothing about it is Shrink-Wrap-specific beyond the default
	// `text` argument), just with text = tr("Subdivide") so the undo-stack
	// entry reads correctly.
	void commitSubdivision(SceneNode* node, SceneNode* parent, int position,
	                        const QUuid& meshUuid, const QSet<QUuid>& originalSelection);

	// Reconstruct Surface: opens the non-modal ReconstructSurfaceDialog
	// (Tools -> Reconstruct Surface...), same findChild-reuse-or-create/
	// show/raise/seed-with-tree-selection pattern as openShrinkWrapDialog()/
	// openSubdivisionDialog() above.
	void openReconstructSurfaceDialog();

	// The Reconstruct Surface dialog's one-line bridge into the undo stack -
	// same convention and immediate-per-result timing as commitShrinkWrap()/
	// commitSubdivision() above, reusing the exact same ShrinkWrapCommand
	// class with text = tr("Reconstruct Surface").
	void commitReconstructSurface(SceneNode* node, SceneNode* parent, int position,
	                               const QUuid& meshUuid, const QSet<QUuid>& originalSelection);

	// Generate UVs: opens the non-modal UVGenerationDialog (Tools -> Generate
	// UVs...), same findChild-reuse-or-create/show/raise/seed-with-tree-
	// selection pattern as openShrinkWrapDialog()/openSubdivisionDialog()
	// above - replaces the old generateUVsForSelectedItems(), which opened
	// the dialog modally (dialog.exec()) from the scene-tree context menu
	// and required a pre-existing selection just to open it. The dialog now
	// owns its own working mesh list (Add/Remove Selected, same as
	// ShrinkWrapDialog) so it can be opened empty from the Tools menu and
	// populated afterward, and Generate can be clicked repeatedly with
	// different methods/settings without relaunching.
	void openUVGenerationDialog();

	// UV generation's one-line bridge into the undo stack - called by
	// UVGenerationDialog::onGenerateClicked() once per Generate click, after
	// the mutation has already happened (each SetMeshUVsCommand's before/
	// after snapshots are captured by the caller, same "already happened,
	// command just replays it" convention as commitShrinkWrap() above).
	// Wraps the whole batch in a single beginMacro()/endMacro() when more
	// than one mesh was targeted (same pattern as hideAllItems()), so one
	// Ctrl+Z undoes an entire multi-mesh Generate click as one step.
	void commitUVGeneration(QVector<QUndoCommand*> commands, const QString& methodName);

	// Called by CutCommand and PasteCommand to manage cut-mark state.
	// generation must match s_clipboardGeneration at the time of the call or
	// the call is a no-op - guards against a stale command (from a document
	// whose cut has since been superseded by a different document's Copy/Cut)
	// clobbering whichever document's clipboard/marks are current now.
	void clearCutMarks(quint64 generation);
	void reapplyCutMarks(quint64 generation,
	                     const QList<ClipboardEntry>& entries,
	                     const QSet<QUuid>& meshUuids,
	                     const QSet<QUuid>& nodeUuids);
	void deleteSelectedItems();
	void displaySelectedMeshInfo();
	void editMeshMaterial();
	void showVisualizationModelPage();
	void showEnvironmentPage();
	void showPredefinedMaterialsPage();
	void showTransformationsPage();
	void onDisplayModeChanged(int mode);
	void onTextureCacheCleared();
	void onRenderingModeSelected(const QString& mode);
	void onCustomMaterialApplied(const Material& mat);

private slots:
	void setListRow(int index);
	void setListRows(QList<int> indices);
	void showContextMenu(const QPoint& pos);

	void onFileImport();

	// Validates the cut clipboard whenever the scene structure changes.
	// Invalidates (clears) the clipboard if any cut source is no longer present.
	void validateCutClipboard();
	void importFiles(QStringList& fileNames);
	void onFileExport();

	void handleTreeWidgetVisibilityChanged();
	void handleTreeWidgetSelectionChanged();
	void handleTreeWidgetMeshRenamed(const QUuid& uuid, const QString& newName);

	void onPredefinedMaterialSelected(const Material& mat);

	void onTexturesApplied(const Material* mat = nullptr);


protected:
	void showEvent(QShowEvent* event);
	bool eventFilter(QObject* watched, QEvent* event) override;
	void keyPressEvent(QKeyEvent* event);
	void dragEnterEvent(QDragEnterEvent* event);
	void dropEvent(QDropEvent* event);
	void resizeEvent(QResizeEvent* event);
	void mouseMoveEvent(QMouseEvent* event);

private:
	// Shared implementation for mergeSelectedMeshes()/unionSelectedMeshes() -
	// see mergeSelectedMeshes()'s doc comment for what's common between them,
	// and combineSelectedMeshes()'s own .cpp doc comment for why combineFn
	// is type-erased (std::function) rather than a plain function pointer.
	void combineSelectedMeshes(
		const std::function<SceneMesh*(const QVector<SceneMesh*>&, const QString&, QString* outDetail)>& combineFn,
		const QString& actionName);

	void checkAndRenameModel(SceneMesh* mesh, const QString& name);
	QString computeUniqueName(SceneMesh* exclude, const QString& name) const;
	bool checkForActiveSelection();
	Mvf::MVFPackage buildMVFPackage() const;
			
	void updateControls();
	QString getSupportedQtImagesFilter();

	// Cleanup methods
	void setupUndoStackMonitoring();
	void onUndoStackChanged();
	bool undoCommandAffectsDocument(const QUndoCommand* command) const;
	bool hasUnsavedUndoDocumentChanges() const;
	void cleanupOrphanedMeshes();
	void validateVariantData();   // removes variant data for files with no remaining meshes
	void validateAnimationData(); // removes animation data for files with no remaining meshes
	void validateCameraData();    // removes camera data for files with no remaining meshes
	void validateLightData();     // removes punctual-light data for files with no remaining meshes
	bool saveMaterialsBeforeClose();  // Save all unsaved materials to library before closing
	void cleanupUnsavedMaterialsFromLibrary();
	QSet<QUuid> scanStackForReferencedUuids();
	QSet<QUuid> collectVisibleUuidsFromDisplayList() const;
	std::vector<int> visibleIndicesFromState() const;
	void updateVisibilityUiFromState();
	void invalidateCutClipboard();  // clears cut clipboard + tree marks
	// Cross-document cut+paste: s_clipboardSourceViewer != this. Leaf mesh
	// entries are actually moved (cloned into target, deleted from source).
	// Assembly entries can't be cleanly removed from the source yet (no
	// mechanism to detach/clean up an emptied-out assembly node the way
	// detachEmptyFileNode() does for file nodes) - those fall back to
	// copy semantics instead of being rejected outright: cloned into
	// target, original left untouched in source, with a message explaining
	// why. Pushes up to two independent undo commands - one on each
	// document's own QUndoStack, since there is no cross-stack undo
	// mechanism in this codebase.
	void performCrossDocumentCutPaste(SceneNode* target);

	// Shared by the ordinary copy-paste path and performCrossDocumentCutPaste:
	// recursively clones a ClipboardNode subtree, resolving each mesh from
	// srcVp (the document being cloned FROM) and inserting into `parent`
	// (in THIS document). resolveTextures re-resolves material textures
	// through this document's own cache after cloning - needed whenever
	// srcVp belongs to a different document, a no-op cost otherwise.
	SceneNode* cloneClipboardSubtree(const ClipboardNode& cn, SceneNode* parent,
	                                 ViewportWidget* srcVp, bool resolveTextures,
	                                 QList<QUuid>& allUuids);

	// Recursively captures a live SceneNode's name/transform/mesh-UUID
	// structure into a pointer-free ClipboardNode snapshot. Shared by
	// copySelectedItems() (populates entry.assemblyRoot directly) and
	// performCrossDocumentCutPaste() (a CUT entry's assemblyRoot is left
	// default-constructed/empty at cut time - only cutNodeUuid is needed
	// for the same-document move-by-pointer path - so the cross-document
	// fallback has to build this snapshot lazily, at paste time, from the
	// still-live source node).
	static ClipboardNode snapshotSceneNode(const SceneNode* n);
	void scheduleTreeRebuild(int delayMs = 1200);
	void rebuildTreeFromCurrentState();
	void applyVisibleMeshState(bool syncTree,
	                           bool deferTreeSync = false,
	                           const QSet<QUuid>& changedUuids = {});
	void scheduleTreeVisibilitySync(int delayMs = 900);
	void syncTreeVisibilityFromModel();

private:
	ViewportWidget*   _viewportWidget;
	SceneGraph* _sceneGraph;

	Material _material;

	bool _bHasTexture;

	QString _albedoPBRTexture;
	QString _metallicPBRTexture;
	QString _roughnessPBRTexture;
	QString _normalPBRTexture;
	QString _aoPBRTexture;
	QString _opacityPBRTexture;
	QString _heightPBRTexture;
	bool    _hasPBRAlbedoTex;
	bool    _hasPBRMetallicTex;
	bool    _hasPBRRoughnessTex;
	bool    _hasPBRNormalTex;
	bool    _hasPBRAOTex;
	bool    _hasPBROpacTex;
	bool    _hasPBRHeightTex;
	float   _heightPBRTexScale;

	bool _runningFirstTime;

	QString _currentFile;
	bool _textureDirOpenedFirstTime;
	bool _documentSaved;
	bool _documentModified;

	bool _progressiveLoadingEnabled = false;
	bool _animateProgressiveFitEnabled = true;

	static QString _lastOpenedDir;
	static QString _lastSelectedFilter;

	int _skyBoxLDRIIndex = 0;
	int _skyBoxHDRIIndex = 0;

	QPointer<QWidget> _navigationOverlay;
	// Chevron button glued to the overlay's own left edge (a child of the
	// composite widget passed to attachOverlayPanel(), not of gridLayout -
	// the overlay is an absolutely-positioned floating child of
	// _viewportWidget, not a normal side-by-side grid column, so the
	// button has to live and move with it, not in the document's outer
	// layout). Collapsing hides modelNavigationWidget entirely (not just
	// its contents) and shrinks the overlay down to just this button via
	// updateNavigationOverlayGeometry().
	QToolButton* _navCollapseButton = nullptr;
	bool _navigationCollapsed = false;
	// User-draggable width, mirroring _lightTreeResizeHandle's pattern in
	// VisualizationEnvironmentPanel (a thin QFrame line, event-filtered for
	// mouse press/move/release) but horizontal instead of vertical - glued
	// to the overlay's right edge, a sibling of modelNavigationWidget inside
	// the same composite as _navCollapseButton above.
	QFrame* _navResizeHandle = nullptr;
	int _navigationOverlayWidth = 420;
	qreal _navResizeDragStartX = 0.0;
	int _navResizeDragStartWidth = 0;

	TextureDebugPanel*     _textureDebugPanel  = nullptr;

	QUndoStack* _undoStack;
	bool _lastCanUndo = false;
	bool _lastCanRedo = false;
	int _lastUndoIndex = 0;
	int _savedUndoIndex = 0;
	bool _nonUndoDocumentDirty = false;
	// Cleanup optimization
	int _lastStackCount = 0;
	QSet<QUuid> _cachedReferencedUuids;  // Meshes referenced in undo stack
	QSet<QUuid> _visibleMeshUuids;       // Authoritative visible mesh state
	int _treeRebuildGeneration = 0;
	bool _treeRebuildPending = false;
	int _treeVisibilitySyncGeneration = 0;
	bool _treeVisibilityDirty = false;

	// Material cache - MDI-scoped, auto-destroyed when MDI closes
	QMap<QString, CachedMaterial> _materialCache;  // Maps material keys to cached materials with metadata
	QSet<QString> _ownedUnsavedMaterials;  // Tracks unsaved materials created by this MDI (for cleanup)

	QUuid _currentEditingMeshUuid;  // UUID of mesh being edited (null if not editing)

	// Shared across every open document (static) so Paste can target a
	// different document than the one Copy/Cut ran in - non-undoable, same
	// as the old per-instance clipboard was.
	//
	// s_clipboardSourceViewer tracks which document's SceneGraph the
	// clipboard's UUIDs actually resolve against (needed because Copy/Cut
	// still only stores UUIDs, resolved lazily at paste time - see
	// SceneClipboard.h). QPointer auto-nulls if that document is closed
	// with a cut still pending, which is exactly the safety check needed
	// before touching it from elsewhere.
	//
	// s_clipboardGeneration guards CutCommand/PasteCommand's cut-mark calls
	// (clearCutMarks/reapplyCutMarks): each command captures the generation
	// current when it was built, and the guarded calls no-op if some other
	// document has since taken over the clipboard (bumped a new
	// generation) - without this, an Undo/Redo far up one document's stack
	// could clobber a completely different document's current clipboard
	// state.
	static QList<ClipboardEntry> s_clipboard;
	static QPointer<ModelViewer> s_clipboardSourceViewer;
	static quint64                s_clipboardGeneration;
};
