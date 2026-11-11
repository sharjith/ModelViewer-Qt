#include "PrintDialog.h"
#include "PartData.h"
#include "PrintPage.h"
#include "PrintPreset.h"
#include "SheetTemplate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

namespace
{

const auto kPresetKey = QStringLiteral("Print/presets");

QVariantMap readPresets()
{
    return QSettings().value(kPresetKey).toMap();
}

bool writePresets(QWidget* parent, const QVariantMap& presets)
{
    QSettings settings;
    settings.setValue(kPresetKey, presets);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    QMessageBox::warning(parent, PrintDialog::tr("Print Presets"),
                         PrintDialog::tr("The print presets could not be saved."));
    return false;
}

std::optional<QPageLayout> editPaperLayout(QWidget* parent, const QPageLayout& current)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(PrintDialog::tr("Page Setup"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* paper = new QComboBox(&dialog);
    const auto sizes = Pdf3D::sheetPageSizeChoices();
    for (const auto& size : sizes)
        paper->addItem(QPageSize(size.second).name(), size.second);
    paper->addItem(PrintDialog::tr("Custom"), QPageSize::Custom);
    const int currentSize = paper->findData(current.pageSize().id());
    paper->setCurrentIndex(currentSize >= 0 ? currentSize : paper->count() - 1);
    form->addRow(PrintDialog::tr("Paper:"), paper);

    const auto millimetres = [&](qreal value, qreal minimum) {
        auto* box = new QDoubleSpinBox(&dialog);
        box->setRange(minimum, 2000);
        box->setSuffix(QStringLiteral(" mm"));
        box->setValue(value);
        return box;
    };
    const QSizeF sizeMm = current.pageSize().size(QPageSize::Millimeter);
    auto* width = millimetres(sizeMm.width(), 10);
    auto* height = millimetres(sizeMm.height(), 10);
    form->addRow(PrintDialog::tr("Paper width (portrait):"), width);
    form->addRow(PrintDialog::tr("Paper height (portrait):"), height);

    auto* orientation = new QComboBox(&dialog);
    orientation->addItem(PrintDialog::tr("Portrait"), QPageLayout::Portrait);
    orientation->addItem(PrintDialog::tr("Landscape"), QPageLayout::Landscape);
    orientation->setCurrentIndex(orientation->findData(current.orientation()));
    form->addRow(PrintDialog::tr("Orientation:"), orientation);

    const QMarginsF margins = current.margins(QPageLayout::Millimeter);
    auto* left = millimetres(margins.left(), 0);
    auto* top = millimetres(margins.top(), 0);
    auto* right = millimetres(margins.right(), 0);
    auto* bottom = millimetres(margins.bottom(), 0);
    form->addRow(PrintDialog::tr("Left margin:"), left);
    form->addRow(PrintDialog::tr("Top margin:"), top);
    form->addRow(PrintDialog::tr("Right margin:"), right);
    form->addRow(PrintDialog::tr("Bottom margin:"), bottom);

    auto* error = new QLabel(PrintDialog::tr("Leave at least 10 mm of printable width and height."), &dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QPageLayout selected;
    const auto update = [&] {
        const auto id = static_cast<QPageSize::PageSizeId>(paper->currentData().toInt());
        const bool custom = id == QPageSize::Custom;
        form->setRowVisible(width, custom);
        form->setRowVisible(height, custom);
        const auto direction = static_cast<QPageLayout::Orientation>(orientation->currentData().toInt());
        const QSizeF portraitSize(qMin(width->value(), height->value()), qMax(width->value(), height->value()));
        const auto pageSize = custom ? QPageSize(portraitSize, QPageSize::Millimeter, QString(), QPageSize::ExactMatch)
                                     : QPageSize(id);
        selected = QPageLayout(pageSize, direction, {}, QPageLayout::Millimeter);
        const QMarginsF proposed(left->value(), top->value(), right->value(), bottom->value());
        const QSizeF area = selected.fullRect(QPageLayout::Millimeter).marginsRemoved(proposed).size();
        const bool valid = area.width() >= 10 && area.height() >= 10 && selected.setMargins(proposed);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        error->setVisible(!valid);
    };
    QObject::connect(paper, &QComboBox::currentIndexChanged, &dialog, update);
    QObject::connect(orientation, &QComboBox::currentIndexChanged, &dialog, update);
    for (auto* box : {width, height, left, top, right, bottom})
        QObject::connect(box, &QDoubleSpinBox::valueChanged, &dialog, update);
    update();

    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return selected;
}

} // namespace

PrintDialog::PrintDialog(QPrinter& printer, const QImage& view, const Pdf3D::PartSummary& parts,
                         const QString& defaultTitle, const QString& declaredNumber, const QSizeF& orthographicSizeMm,
                         bool openAfterSaving, QWidget* parent, const QVector<Pdf3D::StaticSheet>& sheets)
    : QDialog(parent), _sceneMaterial(parts.material), _sceneMass(parts.mass), _orthographicSizeMm(orthographicSizeMm)
{
    setWindowTitle(sheets.isEmpty() ? tr("Print") : tr("Publish Sheets"));
    resize(940, 560);
    QSettings settings;

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Use Page Setup to change the paper, orientation, or margins. The sheet adapts to the selected paper."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* body = new QHBoxLayout;
    auto* settingsPanel = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(settingsPanel);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(settingsPanel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body->addWidget(scroll, 1);
    layout->addLayout(body, 1);

    auto* presetsRow = new QHBoxLayout;
    auto* preset = new QComboBox(this);
    preset->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    preset->setMinimumContentsLength(12);
    preset->setToolTip(tr("Apply saved paper, margins, sheet template and model image quality."));
    auto* savePreset = new QPushButton(tr("Save..."), this);
    savePreset->setToolTip(tr("Save the current settings as a named print preset."));
    savePreset->setAutoDefault(false);
    auto* removePreset = new QPushButton(tr("Remove"), this);
    removePreset->setToolTip(tr("Remove the selected print preset."));
    removePreset->setAutoDefault(false);
    auto* presetsLabel = new QLabel(tr("Print preset:"), this);
    presetsLabel->setBuddy(preset);
    presetsRow->addWidget(presetsLabel);
    presetsRow->addWidget(preset, 1);
    presetsRow->addWidget(savePreset);
    presetsRow->addWidget(removePreset);
    settingsLayout->addLayout(presetsRow);

    _useTemplate = new QCheckBox(tr("Print on a sheet template (border and title block)"), this);
    _useTemplate->setChecked(settings.value(QStringLiteral("Print/useTemplate"), true).toBool());
    settingsLayout->addWidget(_useTemplate);

    auto* form = new QFormLayout;
    _template = new QComboBox(this);
    _templates = Pdf3D::builtInSheetTemplates();
    _templates += Pdf3D::loadUserSheetTemplates(Pdf3D::userSheetTemplateFolder());
    for (const Pdf3D::SheetTemplate& tpl : _templates)
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
    _notes = new QLineEdit(this);
    form->addRow(tr("Title:"), _title);
    form->addRow(tr("Drawing number:"), _drawingNumber);
    form->addRow(tr("Revision:"), _revision);
    form->addRow(tr("Author:"), _author);
    form->addRow(tr("Company:"), _company);
    form->addRow(tr("Notes:"), _notes);

    _drawingScale = new QComboBox(this);
    _drawingScale->addItem(tr("Fit to page (NTS)"), 0.0);
    for (const int denominator : {1, 2, 5, 10, 20, 50, 100})
        _drawingScale->addItem(QStringLiteral("1:%1").arg(denominator), 1.0 / denominator);
    for (const int numerator : {2, 5, 10})
        _drawingScale->addItem(QStringLiteral("%1:1").arg(numerator), double(numerator));
    _drawingScale->setEnabled(!_orthographicSizeMm.isEmpty());
    _drawingScale->setToolTip(_orthographicSizeMm.isEmpty()
        ? tr("Fixed scale requires a stationary orthographic view and consistent declared or assigned model units.")
        : tr("Print the current orthographic view at the selected physical scale."));
    form->addRow(tr("Drawing scale:"), _drawingScale);

    _imageDpi = new QComboBox(this);
    _imageDpi->addItem(tr("Draft (150 dpi)"), 150);
    _imageDpi->addItem(tr("Normal (300 dpi)"), 300);
    _imageDpi->addItem(tr("Fine (600 dpi)"), 600);
    const int savedDpi = _imageDpi->findData(settings.value(QStringLiteral("Print/dpi"), 300).toInt());
    _imageDpi->setCurrentIndex(savedDpi >= 0 ? savedDpi : 1);
    _imageDpi->setToolTip(tr("Sets the target resolution of the model image. Large sheets may use a lower resolution. "
                            "Some viewport modes use the on-screen image."));
    form->addRow(tr("Model image quality:"), _imageDpi);

    settingsLayout->addLayout(form);
    settingsLayout->addStretch();

    auto* previewLayout = new QVBoxLayout;
    auto* activeLayout = new QLabel(this);
    activeLayout->setTextFormat(Qt::PlainText);
    activeLayout->setWordWrap(true);
    previewLayout->addWidget(activeLayout);
    auto* preview = new QPrintPreviewWidget(&printer, this);
    preview->setMinimumSize(320, 280);
    preview->setZoomMode(QPrintPreviewWidget::FitInView);
    previewLayout->addWidget(preview, 1);
    if (!sheets.isEmpty())
    {
        auto* page = new QComboBox(this);
        page->addItem(tr("All sheets"));
        for (int i = 0; i < sheets.size(); ++i)
            page->addItem(tr("%1. %2").arg(i + 1).arg(sheets.at(i).name));
        previewLayout->insertWidget(1, page);
        preview->setViewMode(QPrintPreviewWidget::AllPagesView);
        connect(page, &QComboBox::currentIndexChanged, preview, [preview](int index) {
            preview->setViewMode(index == 0 ? QPrintPreviewWidget::AllPagesView : QPrintPreviewWidget::SinglePageView);
            if (index > 0)
                preview->setCurrentPage(index);
        });
        _imageDpi->setEnabled(false);
        _imageDpi->setToolTip(tr("Published sheets use the saved views captured for this preview."));
    }
    auto* previewError = new QLabel(tr("The sheet could not be previewed at this paper size."), this);
    previewError->setWordWrap(true);
    previewError->hide();
    previewLayout->addWidget(previewError);
    body->addLayout(previewLayout, 1);

    const auto configurePaper = [this, &printer, viewSize = view.size()] {
        const Pdf3D::SheetTemplate& tpl = primaryTemplate();
        QPageSize pageSize(QPageSize::A4);
        bool landscape = viewSize.width() >= viewSize.height();
        if (useTemplate())
        {
            const QSizeF custom = tpl.customSizeMm;
            const QSizeF portraitSize(qMin(custom.width(), custom.height()), qMax(custom.width(), custom.height()));
            pageSize = tpl.pageSizeId == QPageSize::Custom
                ? QPageSize(portraitSize, QPageSize::Millimeter, QString(), QPageSize::ExactMatch)
                : QPageSize(tpl.pageSizeId);
            landscape = tpl.landscape;
        }
        const qreal margin = useTemplate() ? 5.0 : 10.0;
        printer.setPageLayout(QPageLayout(pageSize, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                          QMarginsF(margin, margin, margin, margin), QPageLayout::Millimeter));
    };
    const auto refreshPaper = [this, &printer, configurePaper, preview, activeLayout] {
        if (!_paperOverridden)
            configurePaper();
        if (useTemplate())
        {
            const auto& primary = primaryTemplate();
            const bool landscape = printer.pageLayout().orientation() == QPageLayout::Landscape;
            const auto& effective = Pdf3D::printTemplateForOrientation(primary, landscape, _templates);
            const bool missingCompanion = primary.landscape != landscape && !primary.printCompanionId.isEmpty() &&
                                          effective.id == primary.id;
            activeLayout->setText(missingCompanion ? tr("Companion layout unavailable. Reflowing %1.").arg(primary.name)
                                                  : tr("Sheet layout: %1").arg(effective.name));
        }
        activeLayout->setVisible(useTemplate());
        preview->updatePreview();
    };
    const auto rebuildFields = [this, form, preview, refreshPaper] {
        Pdf3D::SheetTemplate tpl;
        if (useTemplate())
        {
            tpl = primaryTemplate();
            const auto& companion = Pdf3D::printTemplateForOrientation(primaryTemplate(), !tpl.landscape, _templates);
            if (companion.id != tpl.id)
            {
                QSet<QString> fieldIds;
                for (const auto& field : tpl.fields)
                    fieldIds.insert(field.id);
                for (const auto& field : companion.fields)
                    if (!fieldIds.contains(field.id))
                        tpl.fields.append(field);
            }
        }
        _customFields.rebuild(*form, tpl, preview, [preview] { preview->updatePreview(); });
        updateEnabled();
        refreshPaper();
    };
    connect(_template, &QComboBox::currentIndexChanged, this, rebuildFields);
    connect(_useTemplate, &QCheckBox::toggled, this, rebuildFields);
    for (auto* edit : { _title, _drawingNumber, _revision, _author, _company, _notes })
        connect(edit, &QLineEdit::textChanged, preview, &QPrintPreviewWidget::updatePreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* resetPaper = buttons->addButton(tr("Reset Paper"), QDialogButtonBox::ResetRole);
    resetPaper->setEnabled(false);
    const auto markManual = [preset] { preset->setCurrentIndex(0); };
    connect(resetPaper, &QPushButton::clicked, this, [this, refreshPaper, resetPaper, markManual] {
        _paperOverridden = false;
        resetPaper->setEnabled(false);
        markManual();
        refreshPaper();
    });
    const auto applyPaperOverride = [this, &printer, resetPaper](const QPageLayout& paper) {
        if (!printer.setPageLayout(paper))
        {
            QMessageBox::warning(this, tr("Page Setup"), tr("The selected paper settings could not be applied."));
            return false;
        }
        _paperOverridden = true;
        resetPaper->setEnabled(true);
        return true;
    };
    auto* pageSetup = buttons->addButton(tr("Page Setup..."), QDialogButtonBox::ActionRole);
    connect(pageSetup, &QPushButton::clicked, this, [this, &printer, refreshPaper, applyPaperOverride, markManual] {
        const auto paper = editPaperLayout(this, printer.pageLayout());
        if (paper && applyPaperOverride(*paper))
        {
            markManual();
            refreshPaper();
        }
    });

    const auto loadPresetList = [this, preset, removePreset](const QString& selected) {
        const QSignalBlocker blocker(preset);
        preset->clear();
        preset->addItem(tr("Current settings"));
        int selectedIndex = 0;
        const QVariantMap savedPresets = readPresets();
        for (auto it = savedPresets.cbegin(); it != savedPresets.cend(); ++it)
        {
            if (!PrintPreset::fromSettings(it.value().toMap()))
                continue;
            preset->addItem(it.key(), it.value());
            if (it.key() == selected)
                selectedIndex = preset->count() - 1;
        }
        preset->setCurrentIndex(selectedIndex);
        removePreset->setEnabled(preset->currentIndex() > 0);
    };
    loadPresetList(QString());

    connect(preset, &QComboBox::currentIndexChanged, this, [this, preset, removePreset, applyPaperOverride, markManual, rebuildFields, refreshPaper](int index) {
        removePreset->setEnabled(index > 0);
        if (index <= 0)
            return;
        const auto savedPreset = PrintPreset::fromSettings(preset->currentData().toMap());
        if (!savedPreset || !applyPaperOverride(savedPreset->paper))
        {
            markManual();
            return;
        }

        const int foundTemplate = _template->findData(savedPreset->templateId);
        const int templateIndex = qMax(0, foundTemplate);
        const bool rebuild = useTemplate() != savedPreset->useTemplate || _template->currentIndex() != templateIndex;
        {
            const QSignalBlocker useTemplateBlocker(_useTemplate), templateBlocker(_template), dpiBlocker(_imageDpi);
            const QSignalBlocker scaleBlocker(_drawingScale);
            _useTemplate->setChecked(savedPreset->useTemplate);
            _template->setCurrentIndex(templateIndex);
            _imageDpi->setCurrentIndex(_imageDpi->findData(savedPreset->imageDpi));
            _drawingScale->setCurrentIndex(_drawingScale->isEnabled()
                ? qMax(0, _drawingScale->findData(savedPreset->scaleRatio)) : 0);
        }
        if (rebuild)
            rebuildFields();
        else
            refreshPaper();

        if (savedPreset->useTemplate && foundTemplate < 0)
            QMessageBox::information(this, tr("Print Presets"), tr("The preset's template is unavailable. The default template was applied."));
    });
    connect(_template, &QComboBox::currentIndexChanged, this, markManual);
    connect(_useTemplate, &QCheckBox::toggled, this, markManual);
    connect(_imageDpi, &QComboBox::currentIndexChanged, this, markManual);
    connect(_drawingScale, &QComboBox::currentIndexChanged, this, [markManual, preview] {
        markManual();
        preview->updatePreview();
    });

    connect(savePreset, &QPushButton::clicked, this, [this, &printer, preset, loadPresetList] {
        bool accepted = false;
        const QString current = preset->currentIndex() > 0 ? preset->currentText() : QString();
        const QString name = QInputDialog::getText(this, tr("Save Print Preset"), tr("Preset name:"),
                                                  QLineEdit::Normal, current, &accepted).trimmed();
        if (!accepted || name.isEmpty())
            return;

        QVariantMap savedPresets = readPresets();
        if (savedPresets.contains(name) && QMessageBox::question(this, tr("Save Print Preset"),
            tr("Replace the print preset \"%1\"?").arg(name)) != QMessageBox::Yes)
            return;
        const PrintPreset currentSettings{printer.pageLayout(), _template->currentData().toString(), useTemplate(), imageDpi(),
                                          _drawingScale->currentData().toDouble()};
        savedPresets.insert(name, currentSettings.toSettings());
        if (writePresets(this, savedPresets))
            loadPresetList(name);
    });
    connect(removePreset, &QPushButton::clicked, this, [this, preset, loadPresetList] {
        if (preset->currentIndex() <= 0)
            return;
        const QString name = preset->currentText();
        if (QMessageBox::question(this, tr("Print Presets"), tr("Remove the print preset \"%1\"?").arg(name)) != QMessageBox::Yes)
            return;
        QVariantMap savedPresets = readPresets();
        savedPresets.remove(name);
        if (writePresets(this, savedPresets))
            loadPresetList(QString());
    });
    QPushButton* print = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
    print->setDefault(true);
    const auto addSaveButton = [this, buttons](const QString& label, Output output) {
        auto* button = buttons->addButton(label, QDialogButtonBox::ActionRole);
        connect(button, &QPushButton::clicked, this, [this, output] {
            _output = output;
            saveSettings();
            accept();
        });
        return button;
    };
    auto* savePdf = addSaveButton(tr("Save as PDF..."), Output::Pdf);
    auto* saveSvg = addSaveButton(tr("Save as SVG..."), Output::Svg);
    QPushButton* save3dPdf = nullptr;
    if (!sheets.isEmpty())
    {
        saveSvg->setToolTip(tr("Save each sheet as a numbered SVG file in a new folder."));
        save3dPdf = addSaveButton(tr("Save as 3D PDF..."), Output::Pdf3D);
        save3dPdf->setToolTip(tr("Choose the 3D PDF sheet template and interactive views in the next dialog."));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    _openAfterSaving = new QCheckBox(sheets.isEmpty() ? tr("Open PDF or SVG after saving") : tr("Open PDF or SVG folder after saving"), this);
    _openAfterSaving->setChecked(openAfterSaving);
    layout->addWidget(_openAfterSaving);
    layout->addWidget(buttons);

    connect(preview, &QPrintPreviewWidget::paintRequested, this,
        [this, view, sheets, partCount = parts.parts, previewError, print, savePdf, saveSvg, save3dPdf](QPrinter* device) {
            const QPageLayout paper = device->pageLayout();
            const QSizeF pagePt = paper.paintRect(QPageLayout::Point).size();
            bool scaleTooLarge = false;
            const auto sheetOptions = options(paper.orientation());
            bool drawn;
            if (sheets.isEmpty())
            {
                QPainter painter(device);
                drawn = PrintPage::paint(painter, pagePt, device->resolution(), useTemplate(),
                    sheetOptions, partCount, view, modelSizeMm(), &scaleTooLarge);
            }
            else
            {
                drawn = PrintPage::paintSheets(*device, device->resolution(), useTemplate(), sheetOptions, partCount, sheets);
            }
            previewError->setText(scaleTooLarge
                ? tr("The current view is too large at this scale. Choose a smaller scale or a larger paper size.")
                : tr("The sheet could not be previewed at this paper size."));
            previewError->setVisible(!drawn);
            for (auto* button : {print, savePdf, saveSvg, save3dPdf})
                if (button)
                    button->setEnabled(drawn);
        });

    rebuildFields();
    if (!sheets.isEmpty())
    {
        // AllPagesView fits once when selected. Fit again after the dialog lays out the preview.
        QTimer::singleShot(0, preview, [preview] {
            if (preview->viewMode() == QPrintPreviewWidget::AllPagesView)
                preview->setAllPagesViewMode();
        });
    }
}

const Pdf3D::SheetTemplate& PrintDialog::primaryTemplate() const
{
    const int index = _template->currentIndex();
    static const Pdf3D::SheetTemplate empty;
    return index >= 0 && index < _templates.size() ? _templates.at(index) : empty;
}

void PrintDialog::updateEnabled()
{
    const bool on = _useTemplate->isChecked();
    for (QWidget* w : { static_cast<QWidget*>(_template), static_cast<QWidget*>(_title), static_cast<QWidget*>(_drawingNumber),
                        static_cast<QWidget*>(_revision), static_cast<QWidget*>(_author), static_cast<QWidget*>(_company),
                        static_cast<QWidget*>(_notes) })
        w->setEnabled(on);
}

void PrintDialog::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("Print/useTemplate"), _useTemplate->isChecked());
    settings.setValue(QStringLiteral("Print/templateId"), _template->currentData().toString());
    settings.setValue(QStringLiteral("Print/author"), _author->text());
    settings.setValue(QStringLiteral("Print/company"), _company->text());
    settings.setValue(QStringLiteral("Print/dpi"), imageDpi());
    _customFields.save();
    settings.remove(QStringLiteral("Print/drawingNumber"));   // an earlier build remembered it
}

bool PrintDialog::useTemplate() const
{
    return _useTemplate->isChecked();
}

int PrintDialog::imageDpi() const
{
    return _imageDpi->currentData().toInt();
}

QSizeF PrintDialog::modelSizeMm() const
{
    const double ratio = _drawingScale->currentData().toDouble();
    return ratio > 0.0 ? _orthographicSizeMm * ratio : QSizeF();
}

bool PrintDialog::openAfterSaving() const
{
    return _openAfterSaving->isChecked();
}

Pdf3D::Pdf3DOptions PrintDialog::options(QPageLayout::Orientation orientation) const
{
    Pdf3D::Pdf3DOptions o;
    const auto& effective = Pdf3D::printTemplateForOrientation(primaryTemplate(), orientation == QPageLayout::Landscape, _templates);
    o.templateId = effective.id;
    o.title = _title->text();
    o.drawingNumber = _drawingNumber->text();
    o.revision = _revision->text();
    o.author = _author->text();
    o.company = _company->text();
    o.scale = _drawingScale->currentIndex() == 0 ? tr("NTS") : _drawingScale->currentText();
    o.notes = _notes->text();
    o.sceneMaterial = _sceneMaterial;
    o.sceneMass = _sceneMass;
    o.fieldValues = _customFields.values();
    return o;
}
