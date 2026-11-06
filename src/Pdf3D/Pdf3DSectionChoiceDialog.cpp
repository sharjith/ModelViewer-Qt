#include "Pdf3DSectionChoiceDialog.h"
#include "ui_Pdf3DSectionChoiceDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>

Pdf3DSectionChoiceDialog::Pdf3DSectionChoiceDialog(const QVector<Item>& items, QWidget* parent)
	: QDialog(parent)
	, ui(std::make_unique<Ui::Pdf3DSectionChoiceDialog>())
{
	ui->setupUi(this);
	restoreGeometry(QSettings().value(QStringLiteral("pdf3d/sectionChoiceGeometry")).toByteArray());

	auto* form = qobject_cast<QFormLayout*>(ui->rowsWidget->layout());
	for (const Item& item : items)
	{
		auto* combo = new QComboBox(ui->rowsWidget);
		for (int i = 0; i < item.planes.size(); ++i)
			combo->addItem(tr("Only plane %1: %2").arg(i + 1).arg(item.planes[i]), i);
		combo->addItem(tr("Every plane in its own view"), -1);
		combo->setCurrentIndex(combo->count() - 1);   // nothing is lost by default
		form->addRow(new QLabel(item.viewName, ui->rowsWidget), combo);
		_combos.append(combo);
	}
}

Pdf3DSectionChoiceDialog::~Pdf3DSectionChoiceDialog() = default;

QVector<int> Pdf3DSectionChoiceDialog::choices() const
{
	QVector<int> result;
	for (const QComboBox* combo : _combos)
		result.append(combo->currentData().toInt());
	return result;
}

void Pdf3DSectionChoiceDialog::done(int result)
{
	QSettings().setValue(QStringLiteral("pdf3d/sectionChoiceGeometry"), saveGeometry());
	QDialog::done(result);
}
