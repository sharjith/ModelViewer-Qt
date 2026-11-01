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
// MeshInfoDialog (mesh / tree context menu -> Mesh Info)
//
// Table for the selected meshes: always the mesh statistics (points, triangles, memory, material, source file and node),
// then the source properties an importer kept on the nodes they hang from (JT properties, 3DXML references ... anything
// the importer put in Assimp node metadata, flattened by NodeProperties.h). Formats that keep no node properties
// therefore still show the statistics. Each property row names the node it came from; a mesh's ancestors that carry
// properties are listed too but never merged into the mesh's own node. Meshes that share a node are shown once.
// Values are shown exactly as stored: no units are interpreted or converted and nothing is written back to the model.
//
// Non-modal like MassPropertiesDialog, with the same selection box; the table is rebuilt when that list changes or on
// Refresh. Properties are import-time data: they are not stored in MVF files and are absent from cloned meshes.
// ---------------------------------------------------------------------------
class MeshInfoDialog : public QDialog
{
	Q_OBJECT
public:
	explicit MeshInfoDialog(ModelViewer* modelViewer, QWidget* parent = nullptr);

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
