#include "ChannelPackingEditorDialog.h"
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>

ChannelPackingEditorDialog::ChannelPackingEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Channel Packing"));
    setModal(true);

    _titleLabel = new QLabel(this);
    _titleLabel->setText(tr("Map:"));

    _channelCombo = new QComboBox(this);
    _channelCombo->addItem("R", 0);
    _channelCombo->addItem("G", 1);
    _channelCombo->addItem("B", 2);
    _channelCombo->addItem("A", 3);

    _invertCheck = new QCheckBox(tr("Invert"), this);

    _scaleSpin = new QDoubleSpinBox(this);
    _scaleSpin->setRange(-10.0, 10.0);
    _scaleSpin->setSingleStep(0.1);
    _scaleSpin->setDecimals(3);
    _scaleSpin->setValue(1.0);

    _biasSpin = new QDoubleSpinBox(this);
    _biasSpin->setRange(-10.0, 10.0);
    _biasSpin->setSingleStep(0.1);
    _biasSpin->setDecimals(3);
    _biasSpin->setValue(0.0);

    auto form = new QFormLayout();
    form->addRow(tr("Channel"), _channelCombo);
    form->addRow(QString(), _invertCheck);
    form->addRow(tr("Scale"), _scaleSpin);
    form->addRow(tr("Bias"), _biasSpin);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _ok = buttons->button(QDialogButtonBox::Ok);
    _cancel = buttons->button(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto vbox = new QVBoxLayout();
    vbox->addWidget(_titleLabel);
    vbox->addLayout(form);
    vbox->addWidget(buttons);
    setLayout(vbox);
}

void ChannelPackingEditorDialog::setCurrentPacking(const Material::ChannelPacking& p, const QString& mapDisplayName)
{
    setWindowTitle(tr("Channel Packing — %1").arg(mapDisplayName));
    // channel in 0..3, clamp just in case
    int ch = p.channel;
    if (ch < 0) ch = 0;
    if (ch > 3) ch = 3;
    int idx = _channelCombo->findData(ch);
    _channelCombo->setCurrentIndex(idx < 0 ? 0 : idx);

    _invertCheck->setChecked(p.invert);
    _scaleSpin->setValue(p.scale);
    _biasSpin->setValue(p.bias);
}

Material::ChannelPacking ChannelPackingEditorDialog::packing() const
{
    Material::ChannelPacking p;
    p.channel = _channelCombo->currentData().toInt();
    p.invert = _invertCheck->isChecked();
    p.scale = static_cast<float>(_scaleSpin->value());
    p.bias = static_cast<float>(_biasSpin->value());
    return p;
}
