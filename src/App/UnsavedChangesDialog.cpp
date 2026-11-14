#include "UnsavedChangesDialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace
{
const char* const kGeometryKey = "unsavedChangesDialog/geometry";
}

QString UnsavedChangesDialog::statusText(const UnsavedDocumentInfo& document)
{
	QString primary;
	if (document.disk == UnsavedDocumentInfo::Disk::Missing)
		primary = tr("File deleted from disk - will be written again");
	else if (document.disk == UnsavedDocumentInfo::Disk::Changed)
		primary = tr("Changed on disk - asks before overwriting");
	else if (document.kind == UnsavedDocumentInfo::Kind::Untitled)
		primary = tr("Never saved - asks for a file name");
	else if (document.kind == UnsavedDocumentInfo::Kind::ImportedNotMvf)
		primary = tr("Not yet saved as .mvf - asks for a file name");
	else if (document.modified)
		primary = tr("Modified");

	if (document.unsavedMaterials > 0)
	{
		if (primary.isEmpty())
			return tr("%n unsaved material(s)", nullptr, document.unsavedMaterials);
		return primary + QStringLiteral("  ") + tr("+ %n unsaved material(s)", nullptr, document.unsavedMaterials);
	}
	return primary;
}

UnsavedChangesDialog::UnsavedChangesDialog(const QList<UnsavedDocumentInfo>& documents, bool exiting, QWidget* parent)
	: QDialog(parent), _documents(documents)
{
	setWindowTitle(tr("Save changes"));
	setModal(true);

	auto* layout = new QVBoxLayout(this);

	const int count = static_cast<int>(_documents.size());
	auto* question = new QLabel(exiting ? tr("Save changes to these %n documents before exiting?", nullptr, count)
	                                    : tr("Save changes to these %n documents before closing?", nullptr, count), this);
	question->setWordWrap(true);
	layout->addWidget(question);

	_tree = new QTreeWidget(this);
	_tree->setColumnCount(3);
	_tree->setHeaderLabels({ tr("Document"), tr("Location"), tr("Status") });
	_tree->setRootIsDecorated(false);
	_tree->setUniformRowHeights(true);
	_tree->setAlternatingRowColors(true);
	_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	_tree->setTextElideMode(Qt::ElideMiddle);
	_tree->setAccessibleName(tr("Documents with unsaved changes"));
	bool anyDiskChange = false;
	for (const UnsavedDocumentInfo& document : std::as_const(_documents))
	{
		auto* item = new QTreeWidgetItem(_tree);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Checked);
		item->setText(0, document.name);
		const QString folder = document.path.isEmpty() ? QString() : QFileInfo(document.path).absolutePath();
		item->setText(1, folder.isEmpty() ? QStringLiteral("-") : folder);
		item->setToolTip(1, document.path);
		const QString status = statusText(document);
		item->setText(2, status);
		item->setToolTip(2, status);
		item->setData(0, Qt::AccessibleTextRole, tr("%1, %2").arg(document.name, status));
		if (document.disk != UnsavedDocumentInfo::Disk::Unchanged)
			anyDiskChange = true;
	}
	_tree->header()->setStretchLastSection(false);
	_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	layout->addWidget(_tree, 1);

	// A text line, not only a colour: screen readers read it.
	_diskWarning = new QLabel(tr("Some files were changed on disk. Saving asks before overwriting them."), this);
	_diskWarning->setWordWrap(true);
	_diskWarning->setVisible(anyDiskChange);
	layout->addWidget(_diskWarning);

	_consequence = new QLabel(this);
	_consequence->setWordWrap(true);
	layout->addWidget(_consequence);

	auto* buttons = new QDialogButtonBox(this);
	_saveButton = buttons->addButton(tr("Save &All"), QDialogButtonBox::AcceptRole);
	QPushButton* dontSave = buttons->addButton(tr("Do&n't Save"), QDialogButtonBox::DestructiveRole);
	QPushButton* cancel = buttons->addButton(QDialogButtonBox::Cancel);
	_saveButton->setDefault(true);
	layout->addWidget(buttons);

	connect(_saveButton, &QPushButton::clicked, this, [this]() { _result = Result::Save; accept(); });
	connect(dontSave, &QPushButton::clicked, this, [this]() { _result = Result::DontSave; accept(); });
	connect(cancel, &QPushButton::clicked, this, [this]() { _result = Result::Cancel; reject(); });   // Esc and the title-bar X reach reject() with Cancel
	connect(_tree, &QTreeWidget::itemChanged, this, [this]() { updateState(); });

	setTabOrder(_tree, _saveButton);
	setTabOrder(_saveButton, dontSave);
	setTabOrder(dontSave, cancel);

	resize(620, 340);
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	restoreGeometry(settings.value(kGeometryKey).toByteArray());

	updateState();
	_tree->setFocus();
	if (_tree->topLevelItemCount() > 0)
		_tree->setCurrentItem(_tree->topLevelItem(0));
}

UnsavedChangesDialog::~UnsavedChangesDialog()
{
	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
	settings.setValue(kGeometryKey, saveGeometry());
}

QList<int> UnsavedChangesDialog::checkedRows() const
{
	QList<int> rows;
	for (int i = 0; i < _tree->topLevelItemCount(); ++i)
		if (_tree->topLevelItem(i)->checkState(0) == Qt::Checked)
			rows.append(i);
	return rows;
}

void UnsavedChangesDialog::updateState()
{
	const int total = _tree->topLevelItemCount();
	const int checked = static_cast<int>(checkedRows().size());
	const int unchecked = total - checked;

	_saveButton->setText(checked == total ? tr("Save &All") : tr("&Save Selected"));
	_saveButton->setEnabled(checked > 0);
	if (checked > 0)
		_saveButton->setDefault(true);

	QStringList parts;
	if (checked > 0)
		parts << tr("%n document(s) will be saved.", nullptr, checked);
	if (unchecked > 0)
		parts << tr("%n document(s) will be closed without saving.", nullptr, unchecked);
	_consequence->setText(parts.join(QLatin1Char(' ')));
}
