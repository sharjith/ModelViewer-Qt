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
	void duplicateSelectedItems();

	// Called by CutCommand and PasteCommand to manage cut-mark state.
	void clearCutMarks();
	void reapplyCutMarks(const QList<ClipboardEntry>& entries,
	                     const QSet<QUuid>& meshUuids,
	                     const QSet<QUuid>& nodeUuids);
	void deleteSelectedItems();
	void generateUVsForSelectedItems();
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

	QList<ClipboardEntry> _clipboard;  // copy-paste clipboard (non-undoable)
};
