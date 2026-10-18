#pragma once

#include <QIcon>
#include <QPoint>
#include <QUuid>
#include <QVector>
#include <QWidget>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class ModelViewer;
class SceneMesh;

// ---------------------------------------------------------------------------
// MeshSelectionBox
//
// The compact "which meshes does this tool act on" control shared by the tool dialogs (Mass Properties, Surface
// Analysis): a read-only field showing the mesh name or "N meshes" - never a long list of names - with three small
// buttons, the same affordances as the Exploded View panel's "Select assembly or meshes" box:
//   * Pick   - toggle on, select meshes in the viewport or scene tree, toggle off to add them to the list;
//   * Edit   - opens MeshSelectionEditor to review the list and remove meshes (its "Add..." goes back to picking);
//   * Clear  - empties the list.
// The field's right-click menu offers Edit and Clear as well.
//
// The box owns its own list of mesh UUIDs, independent of the viewer's live selection - so the tool's dialog can be
// non-modal and the user can select, hide or inspect other things without changing what the tool acts on. It is
// seeded from the viewer's selection when a dialog opens (seedFromViewportSelection()). Entries whose mesh is
// deleted while the box exists are dropped automatically.
//
// Single-mesh mode (setSingleMeshMode()) turns it into a one-mesh picker - Pick replaces the mesh, no Edit button -
// for "choose the reference mesh" style inputs, where a combo box of every mesh in the scene does not scale.
// setExcludedUuids() keeps meshes that are already used elsewhere (the other box's list) out of it.
//
// meshUuidsChanged() is emitted whenever the list changes, whether by the user or by setMeshUuids()/seeding.
// ---------------------------------------------------------------------------
class MeshSelectionBox : public QWidget
{
	Q_OBJECT
public:
	// For a box placed in a .ui file (promoted widget): call setModelViewer() once the owner is known.
	explicit MeshSelectionBox(QWidget* parent = nullptr);
	MeshSelectionBox(ModelViewer* modelViewer, QWidget* parent);
	void setModelViewer(ModelViewer* modelViewer);

	const QVector<QUuid>& meshUuids() const { return _uuids; }
	bool isEmpty() const { return _uuids.isEmpty(); }
	// The mesh-store indices of the listed meshes that still exist, in list order.
	std::vector<int> meshIds() const;

	// Replaces the list (duplicates and meshes no longer in the scene are dropped).
	void setMeshUuids(const QVector<QUuid>& uuids);
	// Replaces the list with the viewer's current selection; does nothing if nothing is selected there.
	void seedFromViewportSelection();
	// Adds the viewer's current selection to the list (in single-mesh mode it replaces the mesh); does nothing if
	// that adds no mesh.
	void addViewportSelection();

	// At most one mesh: Pick replaces it, the Edit button is hidden, and a longer list is cut to its first mesh.
	void setSingleMeshMode(bool single);
	// Meshes that may not be in the list (dropped now, refused later). Emits meshUuidsChanged() if that removed any.
	void setExcludedUuids(const QVector<QUuid>& excluded);

	// Wording that fits the tool: the label, the field's tooltip and the Edit Selection dialog's two labels.
	void setLabelText(const QString& text);
	void setFieldToolTip(const QString& text);
	void setEditorTexts(const QString& intro, const QString& members);

	bool isPicking() const;

signals:
	void meshUuidsChanged();
	// The user tried to put excluded meshes (setExcludedUuids()) into the list - by picking or adding them - or
	// the list already held one that became excluded and was removed. The mesh is NOT added; the count says how many.
	// Emitted so the owner can tell the user why, instead of the box silently ignoring them.
	void excludedMeshesRejected(int count);

private slots:
	void onPickToggled(bool checked);
	void editSelection();
	void clearSelection();
	void showContextMenu(const QPoint& pos);
	void onMeshAboutToBeDeleted(SceneMesh* mesh);

private:
	void updateDisplay();
	QString describe() const;
	void stopPicking();
	// Pick button look: the select icon while idle, the green check mark while it waits for the confirming click
	// (the same cue as the Exploded View panel's pick button).
	void updatePickVisual();
	QString emptyPlaceholder() const;

	ModelViewer* _modelViewer = nullptr; // not owned
	QVector<QUuid> _uuids;
	QLabel* _label = nullptr;
	QLineEdit* _field = nullptr;
	QPushButton* _pickButton = nullptr;
	QPushButton* _editButton = nullptr;
	QPushButton* _clearButton = nullptr;
	QVector<QUuid> _excluded;
	bool _single = false;
	QIcon _idlePickIcon;
	QIcon _confirmPickIcon;
	QString _editorIntro;
	QString _editorMembers;
};
