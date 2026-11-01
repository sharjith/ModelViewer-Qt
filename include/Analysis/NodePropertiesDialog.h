#pragma once

#include <QDialog>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QMdiSubWindow;
class QPushButton;
class QTableWidget;
class MeshSelectionBox;
class ModelViewer;

// ---------------------------------------------------------------------------
// NodePropertiesDialog (Tools -> Node Properties...)
//
// Read-only table of the source properties an importer kept on the nodes the selected meshes hang from (JT
// properties, 3DXML references, glTF extras ... anything the importer put in Assimp node metadata, flattened by
// NodeProperties.h). Each row names the node it came from; a mesh's ancestors are listed too but never merged into the
// mesh's own node. Meshes that share a node are shown once. No units are interpreted or converted and nothing is
// written back to the model.
//
// Non-modal like MassPropertiesDialog, with the same selection box; the table is rebuilt when that list changes or on
// Refresh. Properties are import-time data: they are not stored in MVF files and are absent from cloned meshes.
// ---------------------------------------------------------------------------
class NodePropertiesDialog : public QDialog
{
	Q_OBJECT
public:
	explicit NodePropertiesDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

	// Re-seeds the mesh list from the viewport selection (used when the tool is invoked while the dialog is open).
	void seedFromViewportSelection();

protected:
	void closeEvent(QCloseEvent* event) override;

public slots:
	void reject() override;

private slots:
	void onActiveSubWindowChanged(QMdiSubWindow* activeSubWindow);
	void applySearchFilter();
	void showTableContextMenu(const QPoint& pos);

private:
	void populate();
	void loadSettings();
	void saveSettings();

	ModelViewer* _modelViewer = nullptr;
	MeshSelectionBox* _selectionBox = nullptr;
	QLabel* _summaryLabel = nullptr;
	QLabel* _noSelectionLabel = nullptr;
	QLineEdit* _searchEdit = nullptr;
	QTableWidget* _table = nullptr;
	QPushButton* _refreshButton = nullptr;
	QPushButton* _closeButton = nullptr;
};
