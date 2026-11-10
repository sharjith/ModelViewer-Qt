#include "PrintDialog.h"
#include "SheetTemplate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

PrintDialog::PrintDialog(const QString& defaultTitle, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Print"));
    setMinimumWidth(420);
    QSettings settings;

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Prints the view as it is shown now, on a white background. The printer is chosen next."), this));

    _useTemplate = new QCheckBox(tr("Print on a sheet template (border and title block)"), this);
    _useTemplate->setChecked(settings.value(QStringLiteral("Print/useTemplate"), true).toBool());
    layout->addWidget(_useTemplate);

    auto* form = new QFormLayout;
    _template = new QComboBox(this);
    QVector<Pdf3D::SheetTemplate> templates = Pdf3D::builtInSheetTemplates();
    templates += Pdf3D::loadUserSheetTemplates(Pdf3D::userSheetTemplateFolder());
    for (const Pdf3D::SheetTemplate& tpl : templates)
        _template->addItem(tpl.name, tpl.id);
    const int saved = _template->findData(settings.value(QStringLiteral("Print/templateId")).toString());
    _template->setCurrentIndex(saved >= 0 ? saved : 0);
    form->addRow(tr("Sheet template:"), _template);

    _title = new QLineEdit(defaultTitle, this);
    _drawingNumber = new QLineEdit(this);
    _revision = new QLineEdit(this);
    _author = new QLineEdit(settings.value(QStringLiteral("Print/author")).toString(), this);
    _company = new QLineEdit(settings.value(QStringLiteral("Print/company")).toString(), this);
    _scale = new QLineEdit(this);
    _scale->setPlaceholderText(tr("e.g. 1:2 or NTS"));
    _notes = new QLineEdit(this);
    form->addRow(tr("Title:"), _title);
    form->addRow(tr("Drawing number:"), _drawingNumber);
    form->addRow(tr("Revision:"), _revision);
    form->addRow(tr("Author:"), _author);
    form->addRow(tr("Company:"), _company);
    form->addRow(tr("Scale:"), _scale);
    form->addRow(tr("Notes:"), _notes);

    _quality = new QComboBox(this);
    _quality->addItem(tr("Draft (150 dpi)"), 150);
    _quality->addItem(tr("Normal (300 dpi)"), 300);
    _quality->addItem(tr("Fine (600 dpi)"), 600);
    const int savedQuality = _quality->findData(settings.value(QStringLiteral("Print/dpi"), 300).toInt());
    _quality->setCurrentIndex(savedQuality >= 0 ? savedQuality : 1);
    form->addRow(tr("Quality:"), _quality);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* print = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
    print->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(_useTemplate, &QCheckBox::toggled, this, &PrintDialog::updateEnabled);
    updateEnabled();
}

void PrintDialog::updateEnabled()
{
    const bool on = _useTemplate->isChecked();
    for (QWidget* w : { static_cast<QWidget*>(_template), static_cast<QWidget*>(_title), static_cast<QWidget*>(_drawingNumber),
                        static_cast<QWidget*>(_revision), static_cast<QWidget*>(_author), static_cast<QWidget*>(_company),
                        static_cast<QWidget*>(_scale), static_cast<QWidget*>(_notes) })
        w->setEnabled(on);
}

void PrintDialog::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("Print/useTemplate"), _useTemplate->isChecked());
    settings.setValue(QStringLiteral("Print/templateId"), _template->currentData().toString());
    settings.setValue(QStringLiteral("Print/author"), _author->text());
    settings.setValue(QStringLiteral("Print/company"), _company->text());
    settings.setValue(QStringLiteral("Print/dpi"), dpi());
}

bool PrintDialog::useTemplate() const
{
    return _useTemplate->isChecked();
}

int PrintDialog::dpi() const
{
    return _quality->currentData().toInt();
}

Pdf3D::Pdf3DOptions PrintDialog::options() const
{
    Pdf3D::Pdf3DOptions o;
    o.templateId = _template->currentData().toString();
    o.title = _title->text();
    o.drawingNumber = _drawingNumber->text();
    o.revision = _revision->text();
    o.author = _author->text();
    o.company = _company->text();
    o.scale = _scale->text();
    o.notes = _notes->text();
    return o;
}
