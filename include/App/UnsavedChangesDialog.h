#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

// What the review dialog needs to know about one document that has unsaved content (see
// research/ui/UNSAVED_CHANGES_ON_CLOSE_DESIGN.md).
struct UnsavedDocumentInfo
{
	enum class Kind { Mvf, Untitled, ImportedNotMvf };
	enum class Disk { Unchanged, Changed, Missing };

	QString name;              // the document's title without the modified mark
	QString path;              // the file it was opened from or saved to (empty for a new session)
	Kind kind = Kind::Mvf;
	Disk disk = Disk::Unchanged;
	bool modified = false;     // the document itself has unsaved changes
	int unsavedMaterials = 0;  // materials made in this document that are not in the user library yet
};

// One dialog for closing several modified documents: a checklist (all ticked), "Save All" / "Save Selected", "Don't Save", "Cancel".
// The dialog saves nothing itself: the caller reads result() and checkedRows().
class UnsavedChangesDialog : public QDialog
{
	Q_OBJECT
public:
	enum class Result { Cancel, DontSave, Save };

	// exiting: the question says "exiting" instead of "closing".
	UnsavedChangesDialog(const QList<UnsavedDocumentInfo>& documents, bool exiting, QWidget* parent = nullptr);
	~UnsavedChangesDialog() override;

	Result result() const { return _result; }
	QList<int> checkedRows() const;   // indices into the list given to the constructor

	static QString statusText(const UnsavedDocumentInfo& document);

private:
	void updateState();

	QList<UnsavedDocumentInfo> _documents;
	QTreeWidget* _tree = nullptr;
	QLabel* _diskWarning = nullptr;
	QLabel* _consequence = nullptr;
	QPushButton* _saveButton = nullptr;
	Result _result = Result::Cancel;
};
