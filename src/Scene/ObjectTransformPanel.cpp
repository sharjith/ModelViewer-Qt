#include "ObjectTransformPanel.h"
#include "LanguageManager.h"
#include "ui_ObjectTransformPanel.h"

ObjectTransformPanel::ObjectTransformPanel(QWidget* parent)
	: QWidget(parent), ui(std::make_unique<Ui::ObjectTransformPanel>())
{
	ui->setupUi(this);

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		ui->retranslateUi(this);
		});

	// Connect button signals
	connect(ui->pushButtonApplyTransformations, &QPushButton::clicked,
		this, &ObjectTransformPanel::onApplyButtonClicked);

	connect(ui->pushButtonResetTransformations, &QPushButton::clicked,
		this, &ObjectTransformPanel::onResetButtonClicked);

	// Initialize to enabled
	setControlsEnabled(true);
}

ObjectTransformPanel::~ObjectTransformPanel() = default;

QVector3D ObjectTransformPanel::getTranslation() const
{
	return QVector3D(
		ui->doubleSpinBoxDX->value(),
		ui->doubleSpinBoxDY->value(),
		ui->doubleSpinBoxDZ->value()
	);
}

QVector3D ObjectTransformPanel::getRotation() const
{
	return QVector3D(
		ui->doubleSpinBoxRX->value(),
		ui->doubleSpinBoxRY->value(),
		ui->doubleSpinBoxRZ->value()
	);
}

QVector3D ObjectTransformPanel::getScale() const
{
	return QVector3D(
		ui->doubleSpinBoxSX->value(),
		ui->doubleSpinBoxSY->value(),
		ui->doubleSpinBoxSZ->value()
	);
}

void ObjectTransformPanel::setTranslationValues(const QVector3D& trans)
{
	ui->doubleSpinBoxDX->blockSignals(true);
	ui->doubleSpinBoxDY->blockSignals(true);
	ui->doubleSpinBoxDZ->blockSignals(true);

	ui->doubleSpinBoxDX->setValue(trans.x());
	ui->doubleSpinBoxDY->setValue(trans.y());
	ui->doubleSpinBoxDZ->setValue(trans.z());

	ui->doubleSpinBoxDX->blockSignals(false);
	ui->doubleSpinBoxDY->blockSignals(false);
	ui->doubleSpinBoxDZ->blockSignals(false);
}

void ObjectTransformPanel::setRotationValues(const QVector3D& rot)
{
	ui->doubleSpinBoxRX->blockSignals(true);
	ui->doubleSpinBoxRY->blockSignals(true);
	ui->doubleSpinBoxRZ->blockSignals(true);

	ui->doubleSpinBoxRX->setValue(rot.x());
	ui->doubleSpinBoxRY->setValue(rot.y());
	ui->doubleSpinBoxRZ->setValue(rot.z());

	ui->doubleSpinBoxRX->blockSignals(false);
	ui->doubleSpinBoxRY->blockSignals(false);
	ui->doubleSpinBoxRZ->blockSignals(false);
}

void ObjectTransformPanel::setScaleValues(const QVector3D& scale)
{
	ui->doubleSpinBoxSX->blockSignals(true);
	ui->doubleSpinBoxSY->blockSignals(true);
	ui->doubleSpinBoxSZ->blockSignals(true);

	ui->doubleSpinBoxSX->setValue(scale.x());
	ui->doubleSpinBoxSY->setValue(scale.y());
	ui->doubleSpinBoxSZ->setValue(scale.z());

	ui->doubleSpinBoxSX->blockSignals(false);
	ui->doubleSpinBoxSY->blockSignals(false);
	ui->doubleSpinBoxSZ->blockSignals(false);
}

void ObjectTransformPanel::resetAllValues()
{
	ui->doubleSpinBoxDX->blockSignals(true);
	ui->doubleSpinBoxDY->blockSignals(true);
	ui->doubleSpinBoxDZ->blockSignals(true);
	ui->doubleSpinBoxRX->blockSignals(true);
	ui->doubleSpinBoxRY->blockSignals(true);
	ui->doubleSpinBoxRZ->blockSignals(true);
	ui->doubleSpinBoxSX->blockSignals(true);
	ui->doubleSpinBoxSY->blockSignals(true);
	ui->doubleSpinBoxSZ->blockSignals(true);

	// Reset translations
	ui->doubleSpinBoxDX->setValue(0.0);
	ui->doubleSpinBoxDY->setValue(0.0);
	ui->doubleSpinBoxDZ->setValue(0.0);

	// Reset rotations
	ui->doubleSpinBoxRX->setValue(0.0);
	ui->doubleSpinBoxRY->setValue(0.0);
	ui->doubleSpinBoxRZ->setValue(0.0);

	// Reset scales
	ui->doubleSpinBoxSX->setValue(1.0);
	ui->doubleSpinBoxSY->setValue(1.0);
	ui->doubleSpinBoxSZ->setValue(1.0);

	ui->doubleSpinBoxDX->blockSignals(false);
	ui->doubleSpinBoxDY->blockSignals(false);
	ui->doubleSpinBoxDZ->blockSignals(false);
	ui->doubleSpinBoxRX->blockSignals(false);
	ui->doubleSpinBoxRY->blockSignals(false);
	ui->doubleSpinBoxRZ->blockSignals(false);
	ui->doubleSpinBoxSX->blockSignals(false);
	ui->doubleSpinBoxSY->blockSignals(false);
	ui->doubleSpinBoxSZ->blockSignals(false);
}

void ObjectTransformPanel::setControlsEnabled(bool enabled)
{
	ui->doubleSpinBoxDX->setEnabled(enabled);
	ui->doubleSpinBoxDY->setEnabled(enabled);
	ui->doubleSpinBoxDZ->setEnabled(enabled);
	ui->doubleSpinBoxRX->setEnabled(enabled);
	ui->doubleSpinBoxRY->setEnabled(enabled);
	ui->doubleSpinBoxRZ->setEnabled(enabled);
	ui->doubleSpinBoxSX->setEnabled(enabled);
	ui->doubleSpinBoxSY->setEnabled(enabled);
	ui->doubleSpinBoxSZ->setEnabled(enabled);
	ui->pushButtonApplyTransformations->setEnabled(enabled);
	ui->pushButtonResetTransformations->setEnabled(enabled);
}

void ObjectTransformPanel::onApplyButtonClicked()
{
	emit applyTransformationsRequested();
}

void ObjectTransformPanel::onResetButtonClicked()
{
	emit resetTransformationsRequested();
}
