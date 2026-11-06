#include "Pdf3DExportDialog.h"
#include "ui_Pdf3DExportDialog.h"

#include "Pdf3DExporter.h"

#include <QSettings>

Pdf3DExportDialog::Pdf3DExportDialog(const QString& defaultTitle, QWidget* parent)
	: QDialog(parent)
	, ui(std::make_unique<Ui::Pdf3DExportDialog>())
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
	ui->savedViewsCheck->setChecked(settings.value(QStringLiteral("pdf3d/savedViews"), true).toBool());
	ui->titleEdit->selectAll();
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
	options.includeSavedViews = ui->savedViewsCheck->isChecked();
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
		settings.setValue(QStringLiteral("pdf3d/savedViews"), ui->savedViewsCheck->isChecked());
	}
	QDialog::done(result);
}
