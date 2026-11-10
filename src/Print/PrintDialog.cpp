#include "PrintDialog.h"
#include "PrintPage.h"
#include "SheetTemplate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPageLayout>
#include <QPageSetupDialog>
#include <QPageSize>
#include <QPrinter>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QtMath>

PrintDialog::PrintDialog(QPrinter& printer, const QImage& view, int partCount,
                         const QString& defaultTitle, const QString& declaredNumber, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Print"));
    resize(940, 560);
    QSettings settings;

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Use Page Setup to change the paper, orientation, or margins. The sheet adapts to the selected paper."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* body = new QHBoxLayout;
    auto* settingsLayout = new QVBoxLayout;
    body->addLayout(settingsLayout);
    layout->addLayout(body, 1);

    _useTemplate = new QCheckBox(tr("Print on a sheet template (border and title block)"), this);
    _useTemplate->setChecked(settings.value(QStringLiteral("Print/useTemplate"), true).toBool());
    settingsLayout->addWidget(_useTemplate);

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
    _drawingNumber = new QLineEdit(declaredNumber, this);   // empty unless the model declares one: a drawing number is not remembered between models
    if (!declaredNumber.isEmpty())
        _drawingNumber->setToolTip(tr("Taken from the model. You can change it."));
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
    settingsLayout->addLayout(form);
    settingsLayout->addStretch();

    auto* previewLayout = new QVBoxLayout;
    auto* preview = new QPrintPreviewWidget(&printer, this);
    preview->setMinimumSize(320, 280);
    preview->setZoomMode(QPrintPreviewWidget::FitInView);
    previewLayout->addWidget(preview, 1);
    auto* previewError = new QLabel(tr("The sheet could not be previewed at this paper size."), this);
    previewError->setWordWrap(true);
    previewError->hide();
    previewLayout->addWidget(previewError);
    body->addLayout(previewLayout, 1);

    const auto configurePaper = [this, &printer, viewSize = view.size()] {
        if (useTemplate())
        {
            const QSizeF pt = Pdf3D::sheetPageSizePt(Pdf3D::findSheetTemplate(options().templateId));
            printer.setPageSize(QPageSize(QSizeF(qMin(pt.width(), pt.height()), qMax(pt.width(), pt.height())), QPageSize::Point));
            printer.setPageOrientation(pt.width() > pt.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        }
        else
        {
            printer.setPageSize(QPageSize(QPageSize::A4));
            printer.setPageOrientation(viewSize.width() >= viewSize.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        }
    };
    configurePaper();

    connect(preview, &QPrintPreviewWidget::paintRequested, this, [this, view, partCount, previewError](QPrinter* device) {
        const QPageLayout layout = device->pageLayout();
        const QSizeF pagePt = layout.fullRect(QPageLayout::Point).size();
        const QSize printableSize = layout.paintRectPixels(device->resolution()).size();
        if (pagePt.isEmpty() || printableSize.isEmpty())
        {
            previewError->show();
            return;
        }

        // Limit the longest edge of the preview raster to about 2000 pixels, independently of print quality.
        const int previewDpi = qBound(1, qFloor(144000.0 / qMax(pagePt.width(), pagePt.height())), 120);
        const QImage page = PrintPage::render(pagePt, previewDpi, useTemplate(), options(), partCount, view);
        QPainter painter;
        if (page.isNull() || !painter.begin(device))
        {
            previewError->show();
            return;
        }
        PrintPage::paint(painter, printableSize, page);
        previewError->hide();
    });

    const auto refreshPaper = [this, configurePaper, preview] {
        if (!_paperOverridden)
            configurePaper();
        preview->updatePreview();
    };
    connect(_template, &QComboBox::currentIndexChanged, preview, refreshPaper);
    connect(_useTemplate, &QCheckBox::toggled, this, [this, refreshPaper] {
        updateEnabled();
        refreshPaper();
    });
    for (auto* edit : { _title, _drawingNumber, _revision, _author, _company, _scale, _notes })
        connect(edit, &QLineEdit::textChanged, preview, &QPrintPreviewWidget::updatePreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* pageSetup = buttons->addButton(tr("Page Setup..."), QDialogButtonBox::ActionRole);
    connect(pageSetup, &QPushButton::clicked, this, [this, &printer, preview] {
        QPageSetupDialog setup(&printer, this);
        if (setup.exec() == QDialog::Accepted)
        {
            _paperOverridden = true;
            preview->updatePreview();
        }
    });
    QPushButton* print = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
    print->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

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
    settings.remove(QStringLiteral("Print/drawingNumber"));   // an earlier build remembered it
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
