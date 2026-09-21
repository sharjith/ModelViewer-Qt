#pragma once

#include <QPoint>
#include <QUuid>
#include <QVector>
#include <QWidget>
#include <vector>

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
// meshUuidsChanged() is emitted whenever the list changes, whether by the user or by setMeshUuids()/seeding.
// ---------------------------------------------------------------------------
class MeshSelectionBox : public QWidget
{
	Q_OBJECT
public:
	explicit MeshSelectionBox(ModelViewer* modelViewer, QWidget* parent = nullptr);

	const QVector<QUuid>& meshUuids() const { return _uuids; }
	bool isEmpty() const { return _uuids.isEmpty(); }
	// The mesh-store indices of the listed meshes that still exist, in list order.
	std::vector<int> meshIds() const;

	// Replaces the list (duplicates and meshes no longer in the scene are dropped).
	void setMeshUuids(const QVector<QUuid>& uuids);
	// Replaces the list with the viewer's current selection; does nothing if nothing is selected there.
	void seedFromViewportSelection();

	// Wording that fits the tool: the field's tooltip and the Edit Selection dialog's two labels.
	void setFieldToolTip(const QString& text);
	void setEditorTexts(const QString& intro, const QString& members);

	bool isPicking() const;

signals:
	void meshUuidsChanged();

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

	ModelViewer* _modelViewer; // not owned
	QVector<QUuid> _uuids;
	QLineEdit* _field = nullptr;
	QPushButton* _pickButton = nullptr;
	QPushButton* _editButton = nullptr;
	QPushButton* _clearButton = nullptr;
	QString _editorIntro;
	QString _editorMembers;
};
