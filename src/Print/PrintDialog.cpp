#include "PrintDialog.h"
#include "PartData.h"
#include "PrintPage.h"
#include "PrintPreset.h"
#include "SheetTemplate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

namespace
{

const auto kPresetKey = QStringLiteral("Print/presets");
const auto kPublishOutputKey = QStringLiteral("Print/lastPublishedOutput");

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
                         bool openAfterSaving, QWidget* parent, PublishSteps publish)
    : QDialog(parent), _sceneMaterial(parts.material), _sceneMass(parts.mass), _orthographicSizeMm(orthographicSizeMm)
{
    const bool publishing = publish.views != nullptr;
    setWindowTitle(publishing ? tr("Publish Sheets") : tr("Print"));
    resize(940, 560);
    QSettings settings;

    auto* layout = new QVBoxLayout(this);
    auto* sheetPage = publishing ? new QWidget(this) : this;
    auto* sheetLayout = publishing ? new QVBoxLayout(sheetPage) : layout;
    auto* intro = new QLabel(tr("Use Page Setup to change the paper, orientation, or margins. The sheet adapts to the selected paper."), this);
    intro->setWordWrap(true);
    sheetLayout->addWidget(intro);

    auto* body = new QHBoxLayout;
    auto* settingsPanel = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(settingsPanel);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(settingsPanel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body->addWidget(scroll, 1);
    sheetLayout->addLayout(body, 1);

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
    if (publishing)
    {
        auto* quality = new QLabel(tr("Captured view resolution"), this);
        quality->setToolTip(tr("Each published sheet uses the image captured from its saved view."));
        form->addRow(tr("Model image quality:"), quality);
        _imageDpi->hide();
    }
    else
    {
        form->addRow(tr("Model image quality:"), _imageDpi);
    }

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
    QComboBox* page = nullptr;
    if (publishing)
    {
        page = new QComboBox(sheetPage);
        page->addItem(tr("All sheets"));
        previewLayout->insertWidget(1, page);
        preview->setViewMode(QPrintPreviewWidget::AllPagesView);
        connect(page, &QComboBox::currentIndexChanged, preview, [preview](int index) {
            preview->setViewMode(index == 0 ? QPrintPreviewWidget::AllPagesView : QPrintPreviewWidget::SinglePageView);
            if (index > 0)
                preview->setCurrentPage(index);
        });
    }
    auto* previewError = new QLabel(tr("The sheet could not be previewed at this paper size."), this);
    previewError->setWordWrap(true);
    previewError->hide();
    previewLayout->addWidget(previewError);
    body->addLayout(previewLayout, 1);

    const auto configurePaper = [this, &printer, viewSize = view.size()] {
        const Pdf3D::SheetTemplate& tpl = primaryTemplate();
        QPageSize pageSize(QPageSize::A4);
        const QSize sourceSize = _sheets.isEmpty() ? viewSize : _sheets.first().image.size();
        bool landscape = sourceSize.width() >= sourceSize.height();
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
    QPushButton* print = nullptr;
    QPushButton* savePdf = nullptr;
    QPushButton* saveSvg = nullptr;
    QPushButton* next = nullptr;
    _openAfterSaving = new QCheckBox(tr("Open PDF or SVG after saving"), this);
    _openAfterSaving->setChecked(openAfterSaving);

    if (!publishing)
    {
        print = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
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
        savePdf = addSaveButton(tr("Save as PDF..."), Output::Pdf);
        saveSvg = addSaveButton(tr("Save as SVG..."), Output::Svg);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            saveSettings();
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        sheetLayout->addWidget(_openAfterSaving);
        sheetLayout->addWidget(buttons);
    }
    else
    {
        delete buttons->button(QDialogButtonBox::Cancel);
        sheetLayout->addWidget(buttons);
        auto* step = new QLabel(this);
        layout->addWidget(step);
        auto* pages = new QStackedWidget(this);
        pages->addWidget(publish.views);
        pages->addWidget(sheetPage);
        auto* outputPage = new QWidget(pages);
        auto* outputLayout = new QVBoxLayout(outputPage);
        auto* outputForm = new QFormLayout;
        outputLayout->addLayout(outputForm);
        auto* format = new QComboBox(outputPage);
        format->addItem(tr("PDF"), static_cast<int>(Output::Pdf));
        format->addItem(tr("SVG files"), static_cast<int>(Output::Svg));
        format->addItem(tr("3D PDF"), static_cast<int>(Output::Pdf3D));
        format->addItem(tr("Printer"), static_cast<int>(Output::Printer));
        const int lastOutput = settings.value(kPublishOutputKey, static_cast<int>(Output::Pdf)).toInt();
        format->setCurrentIndex(qMax(0, format->findData(lastOutput)));
        outputForm->addRow(tr("Format:"), format);
        auto* destinationLabel = new QLabel(outputPage);
        auto* destinationRow = new QWidget(outputPage);
        auto* destinationLayout = new QHBoxLayout(destinationRow);
        destinationLayout->setContentsMargins(0, 0, 0, 0);
        auto* destination = new QLineEdit(QDir::toNativeSeparators(publish.suggestedPath), destinationRow);
        destinationLabel->setBuddy(destination);
        auto* browse = new QPushButton(tr("Browse..."), destinationRow);
        browse->setAutoDefault(false);
        destinationLayout->addWidget(destination, 1);
        destinationLayout->addWidget(browse);
        outputForm->addRow(destinationLabel, destinationRow);
        outputLayout->addWidget(_openAfterSaving);
        auto* outputHint = new QLabel(outputPage);
        outputHint->setWordWrap(true);
        outputLayout->addWidget(outputHint);
        outputLayout->addStretch();
        pages->addWidget(outputPage);
        layout->addWidget(pages, 1);

        auto* navigation = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        auto* back = navigation->addButton(tr("Back"), QDialogButtonBox::ActionRole);
        next = navigation->addButton(tr("Next"), QDialogButtonBox::ActionRole);
        next->setDefault(true);
        connect(navigation, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(navigation);

        const QString baseFolder = QFileInfo(publish.suggestedPath).absolutePath();
        auto* svgFolder = new QLineEdit(QDir::toNativeSeparators(baseFolder), destinationRow);
        destinationLayout->insertWidget(1, svgFolder, 1);
        destinationLabel->setText(tr("Save to:"));
        const auto selectedOutput = [format] { return static_cast<Output>(format->currentData().toInt()); };
        const auto updateOutput = [=] {
            const auto choice = selectedOutput();
            const bool pdf = choice == Output::Pdf;
            const bool svg = choice == Output::Svg;
            destinationLabel->setText(svg ? tr("Destination folder:") : tr("Save to:"));
            destinationLabel->setBuddy(svg ? svgFolder : destination);
            destinationLabel->setVisible(pdf || svg);
            destinationRow->setVisible(pdf || svg);
            destination->setVisible(pdf);
            svgFolder->setVisible(svg);
            _openAfterSaving->setVisible(pdf || svg);
            _openAfterSaving->setText(svg ? tr("Open SVG folder after saving") : tr("Open PDF after saving"));
            outputHint->setText(choice == Output::Pdf3D
                ? tr("The interactive cover comes first, followed by the selected sheets. Continue to configure and preview the cover.")
                : svg ? tr("Each sheet is saved as a numbered SVG in a new folder.")
                      : choice == Output::Printer ? tr("Choose your printer and page range in the system print dialog.") : QString());
            next->setText(choice == Output::Printer ? tr("Print...") : choice == Output::Pdf3D ? tr("Configure 3D PDF...") : tr("Save"));
        };
        connect(format, &QComboBox::currentIndexChanged, this, updateOutput);
        connect(browse, &QPushButton::clicked, this, [=] {
            const bool svg = selectedOutput() == Output::Svg;
            auto* field = svg ? svgFolder : destination;
            const QString path = svg ? QFileDialog::getExistingDirectory(this, tr("Choose SVG Destination"), field->text())
                : QFileDialog::getSaveFileName(this, tr("Choose PDF Destination"), field->text(),
                    tr("PDF files (*.pdf)"), nullptr, QFileDialog::DontConfirmOverwrite);
            if (!path.isEmpty())
                field->setText(QDir::toNativeSeparators(path));
        });
        auto* viewsList = publish.viewList;
        const auto updateViewStep = [=] {
            bool checked = false;
            for (int row = 0; row < viewsList->count() && !checked; ++row)
                checked = viewsList->item(row)->checkState() == Qt::Checked;
            if (pages->currentIndex() == 0)
                next->setEnabled(checked);
        };
        connect(viewsList, &QListWidget::itemChanged, this, updateViewStep);
        connect(pages, &QStackedWidget::currentChanged, this, [=](int index) {
            const QStringList labels{tr("1 of 3 · Choose Views"), tr("2 of 3 · Set Up Sheets"), tr("3 of 3 · Choose Output")};
            step->setText(labels.at(index));
            back->setEnabled(index > 0);
            next->setText(tr("Next"));
            if (index == 0)
                updateViewStep();
            else if (index == 1)
            {
                preview->updatePreview();
                if (page->currentIndex() == 0)
                    QTimer::singleShot(0, preview, [preview, page] {
                        if (page->currentIndex() == 0)
                            preview->setAllPagesViewMode();
                    });
            }
            else
            {
                next->setEnabled(true);
                updateOutput();
            }
        });
        connect(back, &QPushButton::clicked, this, [pages] { pages->setCurrentIndex(pages->currentIndex() - 1); });
        connect(next, &QPushButton::clicked, this, [=, capture = std::move(publish.capture)] {
            if (pages->currentIndex() == 0)
            {
                auto captured = capture();
                if (captured.isEmpty())
                    return;
                _sheets = std::move(captured);
                {
                    const QSignalBlocker blocker(page);
                    page->clear();
                    page->addItem(tr("All sheets"));
                    for (int i = 0; i < _sheets.size(); ++i)
                        page->addItem(tr("%1. %2").arg(i + 1).arg(_sheets.at(i).name));
                }
                if (!_paperOverridden && !useTemplate())
                    configurePaper();
                preview->setAllPagesViewMode();
                pages->setCurrentIndex(1);
                return;
            }
            if (pages->currentIndex() == 1)
            {
                pages->setCurrentIndex(2);
                return;
            }

            const auto choice = selectedOutput();
            QString path;
            if (choice == Output::Pdf || choice == Output::Svg)
            {
                const bool svg = choice == Output::Svg;
                auto* field = svg ? svgFolder : destination;
                const QString entered = QDir::fromNativeSeparators(field->text().trimmed());
                if (!entered.isEmpty())
                {
                    path = QDir(baseFolder).absoluteFilePath(entered);
                    if (!svg && !entered.endsWith(QLatin1Char('/')) && !QFileInfo(path).isDir() &&
                        !path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
                        path += QStringLiteral(".pdf");
                }
                const QFileInfo info(path);
                if (path.isEmpty() || (svg ? !info.isDir() : !QDir(info.absolutePath()).exists() ||
                    (info.exists() && !info.isFile()) || entered.endsWith(QLatin1Char('/'))))
                {
                    QMessageBox::warning(this, windowTitle(), svg ? tr("Choose an existing destination folder.")
                        : tr("Enter a PDF filename in an existing folder."));
                    field->setFocus();
                    return;
                }
                if (!svg && (info.exists() || info.isSymLink()) &&
                    QMessageBox::question(this, tr("Replace PDF?"), tr("Replace the existing file “%1”?").arg(info.fileName()),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                    return;
            }
            _output = choice;
            _outputPath = path;
            saveSettings();
            accept();
        });
        step->setText(tr("1 of 3 · Choose Views"));
        back->setEnabled(false);
        updateViewStep();
    }

    connect(preview, &QPrintPreviewWidget::paintRequested, this,
        [this, view, publishing, partCount = parts.parts, preview, previewError, print, savePdf, saveSvg, next](QPrinter* device) {
            if (publishing && _sheets.isEmpty())
                return;
            const QPageLayout paper = device->pageLayout();
            const QSizeF pagePt = paper.paintRect(QPageLayout::Point).size();
            bool scaleTooLarge = false;
            const auto sheetOptions = options(paper.orientation());
            bool drawn;
            if (!publishing)
            {
                QPainter painter(device);
                drawn = PrintPage::paint(painter, pagePt, device->resolution(), useTemplate(),
                    sheetOptions, partCount, view, modelSizeMm(), &scaleTooLarge);
            }
            else
            {
                drawn = PrintPage::paintSheets(*device, device->resolution(), useTemplate(), sheetOptions, partCount, _sheets);
            }
            previewError->setText(scaleTooLarge
                ? tr("The current view is too large at this scale. Choose a smaller scale or a larger paper size.")
                : tr("The sheet could not be previewed at this paper size."));
            previewError->setVisible(!drawn);
            for (auto* button : {print, savePdf, saveSvg})
                if (button)
                    button->setEnabled(drawn);
            if (next && preview->isVisible())
                next->setEnabled(drawn);
        });

    rebuildFields();
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

void PrintDialog::rememberPublishedOutput() const
{
    QSettings().setValue(kPublishOutputKey, static_cast<int>(_output));
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
