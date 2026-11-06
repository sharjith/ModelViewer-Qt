#include "Pdf3DExportDialog.h"
#include "ui_Pdf3DExportDialog.h"

#include "Pdf3DExporter.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

namespace
{
	constexpr int kViewIndexRole = Qt::UserRole;   // the index into the caller's ViewItem list, on the leaf items
}

Pdf3DExportDialog::Pdf3DExportDialog(const QString& defaultTitle, const QVector<ViewItem>& views, const QString& viewsInfo,
	const QVector<OverlayItem>& overlays, QWidget* parent)
	: QDialog(parent)
	, ui(std::make_unique<Ui::Pdf3DExportDialog>())
	, _viewCount(views.size())
	, _overlayCount(overlays.size())
{
	ui->setupUi(this);

	QSettings settings;
	restoreGeometry(settings.value(QStringLiteral("pdf3d/dialogGeometry")).toByteArray());
	ui->titleEdit->setText(defaultTitle);
	ui->drawingNumberEdit->setText(settings.value(QStringLiteral("pdf3d/drawingNumber")).toString());
	ui->revisionEdit->setText(settings.value(QStringLiteral("pdf3d/revision")).toString());
	ui->authorEdit->setText(settings.value(QStringLiteral("pdf3d/author")).toString());
	ui->companyEdit->setText(settings.value(QStringLiteral("pdf3d/company")).toString());
	ui->scaleEdit->setText(settings.value(QStringLiteral("pdf3d/scale")).toString());
	ui->partsListCheck->setChecked(settings.value(QStringLiteral("pdf3d/partsList"), true).toBool());
	ui->titleEdit->selectAll();

	// the views, grouped in the order the groups first appear
	QTreeWidgetItem* group = nullptr;
	QString groupName;
	for (int i = 0; i < views.size(); ++i)
	{
		const ViewItem& view = views[i];
		if (!group || view.group != groupName)
		{
			group = new QTreeWidgetItem(ui->viewsTree, QStringList{view.group});
			group->setFlags(group->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
			group->setCheckState(0, Qt::Checked);
			QFont f = group->font(0);
			f.setBold(true);
			group->setFont(0, f);
			groupName = view.group;
		}
		auto* item = new QTreeWidgetItem(group, QStringList{view.name, view.note});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Checked);
		item->setData(0, kViewIndexRole, i);
		item->setCheckState(2, view.overlays ? Qt::Checked : Qt::Unchecked);   // show the measurements and annotations in this view
		if (!view.note.isEmpty())
		{
			item->setToolTip(1, view.note);
			if (view.warning)
				item->setForeground(1, palette().color(QPalette::Link));
			else
				item->setForeground(1, palette().color(QPalette::Disabled, QPalette::Text));
		}
	}
	ui->viewsTree->expandAll();
	ui->viewsTree->resizeColumnToContents(0);
	// the column is only useful when there is something to show
	ui->viewsTree->setColumnHidden(2, overlays.isEmpty());

	// the info line is deliberately subtle: a remark, not a warning dialog
	ui->viewsInfoLabel->setText(viewsInfo);
	ui->viewsInfoLabel->setVisible(!viewsInfo.isEmpty());
	QPalette infoPalette = ui->viewsInfoLabel->palette();
	infoPalette.setColor(QPalette::WindowText, palette().color(QPalette::Disabled, QPalette::Text));
	ui->viewsInfoLabel->setPalette(infoPalette);

	connect(ui->viewsTree, &QTreeWidget::itemChanged, this, [this]() { updateOkEnabled(); });
	connect(ui->selectAllButton, &QPushButton::clicked, this, [this]() { setAllViewsChecked(true); });
	connect(ui->selectNoneButton, &QPushButton::clicked, this, [this]() { setAllViewsChecked(false); });
	ui->tabWidget->setTabEnabled(1, !views.isEmpty());

	// the measurements and annotations, grouped; all ticked at first
	QTreeWidgetItem* overlayGroup = nullptr;
	QString overlayGroupName;
	for (int i = 0; i < overlays.size(); ++i)
	{
		const OverlayItem& overlay = overlays[i];
		if (!overlayGroup || overlay.group != overlayGroupName)
		{
			overlayGroup = new QTreeWidgetItem(ui->overlayTree, QStringList{overlay.group});
			overlayGroup->setFlags(overlayGroup->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
			overlayGroup->setCheckState(0, Qt::Checked);
			QFont f = overlayGroup->font(0);
			f.setBold(true);
			overlayGroup->setFont(0, f);
			overlayGroupName = overlay.group;
		}
		auto* item = new QTreeWidgetItem(overlayGroup, QStringList{overlay.name});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Checked);
		item->setData(0, kViewIndexRole, i);
	}
	ui->overlayTree->expandAll();
	connect(ui->overlayAllButton, &QPushButton::clicked, this, [this]() { setAllOverlaysChecked(true); });
	connect(ui->overlayNoneButton, &QPushButton::clicked, this, [this]() { setAllOverlaysChecked(false); });
	ui->tabWidget->setTabEnabled(2, !overlays.isEmpty());
	updateOkEnabled();
}

Pdf3DExportDialog::~Pdf3DExportDialog() = default;

void Pdf3DExportDialog::applyTo(Pdf3D::Pdf3DOptions& options) const
{
	options.title = ui->titleEdit->text().trimmed();
	options.drawingNumber = ui->drawingNumberEdit->text().trimmed();
	options.revision = ui->revisionEdit->text().trimmed();
	options.author = ui->authorEdit->text().trimmed();
	options.company = ui->companyEdit->text().trimmed();
	options.scale = ui->scaleEdit->text().trimmed();
	options.notes = ui->notesEdit->toPlainText().trimmed();
	options.includePartsList = ui->partsListCheck->isChecked();
}

QVector<bool> Pdf3DExportDialog::viewSelection() const
{
	QVector<bool> selection(_viewCount, false);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(0) == Qt::Checked);
	}
	return selection;
}

QVector<bool> Pdf3DExportDialog::viewOverlaySelection() const
{
	QVector<bool> selection(_viewCount, true);
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(2) == Qt::Checked);
	}
	return selection;
}

QVector<bool> Pdf3DExportDialog::overlaySelection() const
{
	QVector<bool> selection(_overlayCount, false);
	for (QTreeWidgetItemIterator it(ui->overlayTree); *it; ++it)
	{
		const QVariant index = (*it)->data(0, kViewIndexRole);
		if (index.isValid())
			selection[index.toInt()] = ((*it)->checkState(0) == Qt::Checked);
	}
	return selection;
}

void Pdf3DExportDialog::setAllOverlaysChecked(bool checked)
{
	for (QTreeWidgetItemIterator it(ui->overlayTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
}

void Pdf3DExportDialog::setAllViewsChecked(bool checked)
{
	for (QTreeWidgetItemIterator it(ui->viewsTree); *it; ++it)
	{
		if ((*it)->data(0, kViewIndexRole).isValid())
			(*it)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
}

void Pdf3DExportDialog::updateOkEnabled()
{
	// the PDF needs at least one view
	const QVector<bool> selection = viewSelection();
	bool any = false;
	for (const bool b : selection)
		any = any || b;
	if (QPushButton* ok = ui->buttonBox->button(QDialogButtonBox::Ok))
	{
		ok->setEnabled(any);
		ok->setToolTip(any ? QString() : tr("Tick at least one view."));
	}
}

void Pdf3DExportDialog::done(int result)
{
	QSettings settings;
	settings.setValue(QStringLiteral("pdf3d/dialogGeometry"), saveGeometry());
	if (result == QDialog::Accepted)
	{
		settings.setValue(QStringLiteral("pdf3d/drawingNumber"), ui->drawingNumberEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/revision"), ui->revisionEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/author"), ui->authorEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/company"), ui->companyEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/scale"), ui->scaleEdit->text().trimmed());
		settings.setValue(QStringLiteral("pdf3d/partsList"), ui->partsListCheck->isChecked());
	}
	QDialog::done(result);
}
