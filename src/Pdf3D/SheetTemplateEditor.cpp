#include "SheetTemplateEditor.h"

#include "SheetPreviewWidget.h"
#include "ui_SheetTemplateEditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Pdf3D
{

namespace
{

// A new cell: a 10 pt inset from the top left of the block, a little under a third of its width and half its height, showing the title.
SheetCell newCell()
{
    SheetCell c;
    c.text = QStringLiteral("{title}");
    c.rect.left = { 10.0, false };
    c.rect.top = { 10.0, false };
    c.rect.right = { 0.3, true };
    c.rect.bottom = { 0.5, true };
    return c;
}

QString elided(const QString& s, int n)
{
    const QString oneLine = s.simplified();
    return oneLine.size() > n ? oneLine.left(n - 1) + QChar(0x2026) : oneLine;
}

} // namespace

SheetTemplateEditor::SheetTemplateEditor(const SheetTemplate& tpl, const QMap<QString, QString>& sampleValues, const QString& folder, const QLocale& locale, QWidget* parent)
    : QDialog(parent)
    , ui(std::make_unique<Ui::SheetTemplateEditor>())
    , _tpl(tpl)
    , _valid(tpl)
    , _values(sampleValues)
    , _folder(folder)
    , _locale(locale)
    , _isUser(tpl.id.startsWith(QLatin1String("user:")))
{
    buildUi();

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("pdf3d/templateEditorGeometry2")).toByteArray());
    connect(this, &QDialog::finished, this, [this]() {
        QSettings s;
        s.setValue(QStringLiteral("pdf3d/templateEditorGeometry2"), saveGeometry());
    });

    loadPage();
    loadLogo();
    fillFieldList(_tpl.fields.isEmpty() ? -1 : 0);
    refreshInsertCombo();
    fillList(_tpl.cells.isEmpty() ? -1 : 0);
    revalidate();
}

SheetTemplateEditor::~SheetTemplateEditor() = default;

void SheetTemplateEditor::buildUi()
{
    ui->setupUi(this);

    // left: the preview
    ui->preview->setTemplate(_tpl);
    ui->preview->setValues(_values);
    ui->preview->setEditable(true);
    connect(ui->preview, &SheetPreviewWidget::cellGeometryChanged, this, [this](int index, const QRectF& rect, const QSizeF& size) { moveCellTo(index, rect, size); });
    connect(ui->preview, &SheetPreviewWidget::cellClicked, this, [this](int index) {
        if (index >= 0 && index < ui->cellList->count())
            ui->cellList->setCurrentRow(index);
    });

    // right: name, tabs, error line, buttons
    ui->nameEdit->setText(_tpl.name);
    connect(ui->nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        _tpl.name = text;
        revalidate();
    });
    setupCellsTab();
    setupPageTab();
    setupLogoTab();
    setupFieldsTab();

    QPalette errorPalette = ui->errorLabel->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(220, 80, 80));
    ui->errorLabel->setPalette(errorPalette);

    ui->saveButton->setToolTip(_isUser ? tr("Writes the changes into your template.") : tr("A built-in template cannot be changed: use Save as."));
    connect(ui->saveButton, &QPushButton::clicked, this, [this]() { save(); });
    connect(ui->saveAsButton, &QPushButton::clicked, this, [this]() { saveAs(); });
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>())
        b->setAutoDefault(false);   // Enter in a text box must not press Add or Save

    // a roomy default for the preview, but never more than about 85% of the screen it opens on
    QSize wanted(1380, 800);
    if (const QScreen* screen = QGuiApplication::primaryScreen())
    {
        const QRect available = screen->availableGeometry();
        wanted = QSize(std::min(wanted.width(), available.width() * 85 / 100), std::min(wanted.height(), available.height() * 85 / 100));
    }
    resize(wanted);
}

// ---- the Cells tab ----------------------------------------------------------------------------------------------------------------

void SheetTemplateEditor::setupCellsTab()
{
    connect(ui->cellList, &QListWidget::currentRowChanged, this, [this](int row) { loadCell(row); });
    connect(ui->addButton, &QPushButton::clicked, this, [this]() { addCell(); });
    connect(ui->duplicateButton, &QPushButton::clicked, this, [this]() { duplicateCell(); });
    connect(ui->deleteButton, &QPushButton::clicked, this, [this]() { deleteCell(); });
    connect(ui->upButton, &QPushButton::clicked, this, [this]() { moveCell(-1); });
    connect(ui->downButton, &QPushButton::clicked, this, [this]() { moveCell(1); });

    // the entries of the "Insert field" menu are made by refreshInsertCombo() (the template's own fields change)
    ui->insertCombo->addItem(tr("Insert field..."));
    QStringList names = standardSheetPlaceholders();
    names += sceneSheetPlaceholders();
    for (const SheetField& field : _tpl.fields)
        if (!names.contains(field.id))
            names << field.id;
    for (const QString& n : names)
        ui->insertCombo->addItem(QStringLiteral("{%1}").arg(n), n);
    connect(ui->insertCombo, &QComboBox::activated, this, [this](int index) { insertPlaceholder(index); });

    ui->align->addItem(tr("Left"), static_cast<int>(Qt::AlignLeft));
    ui->align->addItem(tr("Centre"), static_cast<int>(Qt::AlignHCenter));
    ui->align->addItem(tr("Right"), static_cast<int>(Qt::AlignRight));

    const QString edgeTip = tr("Distance from the left or top edge of the title block, in millimetres or as a percentage of the block.\n"
                               "A negative number is measured from the right or bottom edge instead.");
    for (SheetEdgeBox* e : { ui->leftEdit, ui->topEdit, ui->rightEdit, ui->bottomEdit })
        e->setToolTip(edgeTip);

    for (QLineEdit* e : { ui->labelEdit, ui->textEdit })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyCellEdits(); });
    for (SheetEdgeBox* e : { ui->leftEdit, ui->topEdit, ui->rightEdit, ui->bottomEdit })
        connect(e, &SheetEdgeBox::edgeChanged, this, [this]() { applyCellEdits(); });
    connect(ui->fontSize, &QDoubleSpinBox::valueChanged, this, [this]() { applyCellEdits(); });
    connect(ui->align, &QComboBox::currentIndexChanged, this, [this]() { applyCellEdits(); });
    for (QCheckBox* c : { ui->bold, ui->wrap, ui->scroll, ui->grow })
        connect(c, &QCheckBox::toggled, this, [this]() { applyCellEdits(); });
    connect(ui->colourButton, &QPushButton::clicked, this, [this]() { chooseColour(); });
}

QString SheetTemplateEditor::listText(const SheetCell& cell) const
{
    const QString caption = cell.label.isEmpty() ? QString() : cell.label + QStringLiteral(": ");
    return elided(caption + (cell.text.isEmpty() ? tr("(empty)") : cell.text), 48);
}

void SheetTemplateEditor::fillList(int selectRow)
{
    const QSignalBlocker blocker(ui->cellList);
    ui->cellList->clear();
    for (const SheetCell& c : std::as_const(_tpl.cells))
        ui->cellList->addItem(listText(c));
    const int row = (selectRow >= 0 && selectRow < _tpl.cells.size()) ? selectRow : -1;
    ui->cellList->setCurrentRow(row);
    loadCell(row);
}

void SheetTemplateEditor::loadCell(int row)
{
    const bool have = row >= 0 && row < _tpl.cells.size();
    _loading = true;
    for (QWidget* w : { static_cast<QWidget*>(ui->labelEdit), static_cast<QWidget*>(ui->textEdit), static_cast<QWidget*>(ui->insertCombo),
                        static_cast<QWidget*>(ui->fontSize), static_cast<QWidget*>(ui->bold), static_cast<QWidget*>(ui->colourButton),
                        static_cast<QWidget*>(ui->align), static_cast<QWidget*>(ui->wrap), static_cast<QWidget*>(ui->scroll),
                        static_cast<QWidget*>(ui->grow), static_cast<QWidget*>(ui->leftEdit), static_cast<QWidget*>(ui->topEdit),
                        static_cast<QWidget*>(ui->rightEdit), static_cast<QWidget*>(ui->bottomEdit) })
        w->setEnabled(have);
    if (have)
    {
        const SheetCell& c = _tpl.cells[row];
        ui->labelEdit->setText(c.label);
        ui->textEdit->setText(c.text);
        ui->fontSize->setValue(c.fontSize);
        ui->bold->setChecked(c.bold);
        const Qt::Alignment h = c.align & Qt::AlignHorizontal_Mask;
        ui->align->setCurrentIndex(h == Qt::AlignHCenter ? 1 : h == Qt::AlignRight ? 2 : 0);
        ui->wrap->setChecked(c.wrap);
        ui->scroll->setChecked(c.scrollWhenOverflow);
        ui->grow->setChecked(!c.id.isEmpty() && _tpl.growWithCell == c.id);
        ui->leftEdit->setEdge(c.rect.left);
        ui->topEdit->setEdge(c.rect.top);
        ui->rightEdit->setEdge(c.rect.right);
        ui->bottomEdit->setEdge(c.rect.bottom);
    }
    else
    {
        for (QLineEdit* e : { ui->labelEdit, ui->textEdit })
            e->clear();
        for (SheetEdgeBox* e : { ui->leftEdit, ui->topEdit, ui->rightEdit, ui->bottomEdit })
            e->setEdge(SheetEdge());
    }
    _loading = false;
    updateColourButton();
    ui->deleteButton->setEnabled(have);
    ui->duplicateButton->setEnabled(have);
    ui->upButton->setEnabled(have && row > 0);
    ui->downButton->setEnabled(have && row + 1 < _tpl.cells.size());
    ui->preview->setSelectedCell(have ? row : -1);
}

void SheetTemplateEditor::updateColourButton()
{
    const int row = ui->cellList->currentRow();
    const QColor colour = (row >= 0 && row < _tpl.cells.size()) ? _tpl.cells[row].color : QColor(Qt::black);
    ui->colourButton->setText(colour.name());
    const QColor text = colour.lightness() > 140 ? QColor(Qt::black) : QColor(Qt::white);
    ui->colourButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(colour.name(), text.name()));
}

// Reads the property widgets into the selected cell, then checks the template. A position that cannot be read keeps the old value of
// that edge, is marked red and is reported.
void SheetTemplateEditor::applyCellEdits()
{
    const int row = ui->cellList->currentRow();
    if (_loading || row < 0 || row >= _tpl.cells.size())
        return;
    SheetCell& c = _tpl.cells[row];
    c.label = ui->labelEdit->text();
    c.text = ui->textEdit->text();
    c.fontSize = ui->fontSize->value();
    c.bold = ui->bold->isChecked();
    c.align = Qt::Alignment(static_cast<Qt::AlignmentFlag>(ui->align->currentData().toInt()));
    c.wrap = ui->wrap->isChecked();
    c.scrollWhenOverflow = ui->scroll->isChecked();

    // one cell per template may scroll and one may grow the block: choosing it here releases the others
    if (c.scrollWhenOverflow)
        for (int i = 0; i < _tpl.cells.size(); ++i)
            if (i != row)
                _tpl.cells[i].scrollWhenOverflow = false;
    if (ui->grow->isChecked())
    {
        if (c.id.isEmpty())
        {
            QString id;
            for (int n = 1; id.isEmpty(); ++n)
            {
                const QString candidate = QStringLiteral("cell%1").arg(n);
                const bool used = std::any_of(_tpl.cells.cbegin(), _tpl.cells.cend(), [&](const SheetCell& o) { return o.id == candidate; });
                if (!used)
                    id = candidate;
            }
            c.id = id;
        }
        _tpl.growWithCell = c.id;
    }
    else if (!c.id.isEmpty() && _tpl.growWithCell == c.id)
        _tpl.growWithCell.clear();

    struct EdgeBox
    {
        SheetEdgeBox* box;
        SheetEdge* target;
    };
    const EdgeBox edges[] = { { ui->leftEdit, &c.rect.left }, { ui->topEdit, &c.rect.top }, { ui->rightEdit, &c.rect.right }, { ui->bottomEdit, &c.rect.bottom } };
    for (const EdgeBox& e : edges)
    {
        const SheetEdge value = e.box->edge();
        const double tolerance = value.fraction ? 0.0005 : 0.02;   // rounding of the spin box is not an edit
        if (value.fraction != e.target->fraction || std::fabs(value.value - e.target->value) > tolerance)
            _cellsEdited = true;
        *e.target = value;
    }

    if (QListWidgetItem* item = ui->cellList->item(row))
        item->setText(listText(c));
    revalidate();
}

// Checks the template; when it is valid the preview shows it and Save is available.
void SheetTemplateEditor::revalidate(const QString& extraError)
{
    QString error;
    const bool valid = validateSheetTemplate(_tpl, &error);
    if (valid)
    {
        _valid = _tpl;
        ui->preview->setTemplate(_valid);
        ui->preview->setValues(previewValues());
        ui->preview->setSelectedCell(ui->cellList->currentRow());
    }
    // An error about a cell names it by number: say which cell that is, and select it so its position is at hand on the Cells tab (the
    // error stays on screen while another tab is edited, so it must say where the problem is).
    if (!valid && extraError.isEmpty())
    {
        static const QRegularExpression cellPattern(QStringLiteral("^cell (\\d+)"));
        const QRegularExpressionMatch match = cellPattern.match(error);
        if (match.hasMatch())
        {
            const int row = match.captured(1).toInt() - 1;
            if (row >= 0 && row < _tpl.cells.size())
            {
                error += tr("  (%1 on the Cells tab)").arg(listText(_tpl.cells[row]));
                if (ui->cellList->currentRow() != row)
                    ui->cellList->setCurrentRow(row);
            }
        }
    }
    QString shown = !extraError.isEmpty() ? extraError : error;
    if (!shown.isEmpty())
        shown[0] = shown.at(0).toUpper();
    ui->errorLabel->setText(shown);
    const bool ok = valid && extraError.isEmpty();
    ui->saveButton->setEnabled(ok && _isUser);
    ui->saveAsButton->setEnabled(ok);
}

// A new cell goes directly under the selected one, with the same left and right edges, so it only has to be dragged into place; with no
// cell selected it starts at the top left of the block.
void SheetTemplateEditor::addCell()
{
    const int row = ui->cellList->currentRow();
    const int at = (row >= 0 && row < _tpl.cells.size()) ? row + 1 : _tpl.cells.size();
    SheetCell cell = newCell();
    if (row >= 0 && row < _valid.cells.size() && _valid.cells.size() == _tpl.cells.size())
    {
        const ResolvedSheet sheet = resolveSheet(_valid, sheetPageSizePt(_valid), _valid.blockHeight);
        if (row < sheet.cells.size())
        {
            const QRectF selected = sheet.cells[row].translated(-sheet.titleBlock.topLeft());
            const QSizeF block = sheet.titleBlock.size();
            const double height = std::min(16.0, block.height() / 3.0);
            const double top = std::min(selected.bottom() + 1.0, std::max(0.0, block.height() - height));
            cell.rect = sheetRectFromBlockRect(QRectF(selected.left(), top, selected.width(), height), SheetRect(), block);
        }
    }
    _tpl.cells.insert(at, cell);
    _cellsEdited = true;
    fillList(at);
    revalidate();
}

// The preview reports a cell dragged, resized or nudged: its edges are written the way the cell wrote them before (from the left, from the
// right, as a fraction), the position boxes follow, and the template is checked again.
void SheetTemplateEditor::moveCellTo(int index, const QRectF& blockRect, const QSizeF& blockSize)
{
    if (index < 0 || index >= _tpl.cells.size() || blockSize.isEmpty())
        return;
    _tpl.cells[index].rect = sheetRectFromBlockRect(blockRect, _tpl.cells[index].rect, blockSize);
    _cellsEdited = true;
    if (ui->cellList->currentRow() != index)
        ui->cellList->setCurrentRow(index);   // loads the cell, positions included
    const SheetCell& c = _tpl.cells[index];
    _loading = true;
    ui->leftEdit->setEdge(c.rect.left);
    ui->topEdit->setEdge(c.rect.top);
    ui->rightEdit->setEdge(c.rect.right);
    ui->bottomEdit->setEdge(c.rect.bottom);
    _loading = false;
    revalidate();
}

void SheetTemplateEditor::duplicateCell()
{
    const int row = ui->cellList->currentRow();
    if (row < 0 || row >= _tpl.cells.size())
        return;
    SheetCell copy = _tpl.cells[row];
    copy.id.clear();   // an id names one cell
    copy.scrollWhenOverflow = false;
    copy.rect.left.value += copy.rect.left.fraction ? 0.02 : 8.0;   // offset a little so the copy can be seen
    copy.rect.top.value += copy.rect.top.fraction ? 0.02 : 8.0;
    _tpl.cells.insert(row + 1, copy);
    _cellsEdited = true;
    fillList(row + 1);
    revalidate();
}

void SheetTemplateEditor::deleteCell()
{
    const int row = ui->cellList->currentRow();
    if (row < 0 || row >= _tpl.cells.size())
        return;
    if (!_tpl.cells[row].id.isEmpty() && _tpl.growWithCell == _tpl.cells[row].id)
        _tpl.growWithCell.clear();
    _tpl.cells.removeAt(row);
    _cellsEdited = true;
    fillList(std::min(row, static_cast<int>(_tpl.cells.size()) - 1));
    revalidate();
}

void SheetTemplateEditor::moveCell(int delta)
{
    const int row = ui->cellList->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || row >= _tpl.cells.size() || to >= _tpl.cells.size())
        return;
    std::swap(_tpl.cells[row], _tpl.cells[to]);
    fillList(to);
    revalidate();
}

void SheetTemplateEditor::chooseColour()
{
    const int row = ui->cellList->currentRow();
    if (row < 0 || row >= _tpl.cells.size())
        return;
    const QColor picked = QColorDialog::getColor(_tpl.cells[row].color, this, tr("Text colour"));
    if (!picked.isValid())
        return;
    _tpl.cells[row].color = picked;
    updateColourButton();
    revalidate();
}

void SheetTemplateEditor::insertPlaceholder(int comboIndex)
{
    if (comboIndex <= 0)
        return;
    const QString name = ui->insertCombo->itemData(comboIndex).toString();
    ui->insertCombo->setCurrentIndex(0);
    if (!name.isEmpty())
    {
        ui->textEdit->insert(QStringLiteral("{%1}").arg(name));
        ui->textEdit->setFocus();
    }
}

void SheetTemplateEditor::save()
{
    if (!_isUser)
        return;
    QString error;
    if (!ensureLogoInFolder(&error))
    {
        QMessageBox::warning(this, tr("Sheet template"), error);
        return;
    }
    const QString id = writeUserSheetTemplate(_tpl, _folder, &error);
    if (id.isEmpty())
    {
        QMessageBox::warning(this, tr("Sheet template"), error);
        return;
    }
    _savedId = id;
    accept();
}

void SheetTemplateEditor::saveAs()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save as a template"), tr("Name of the new template:"), QLineEdit::Normal,
                                               _isUser ? tr("%1 copy").arg(_tpl.name) : tr("My %1").arg(_tpl.name), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    // saveSheetTemplateAs never overwrites a template of the user's: it would hand back the existing one and drop these edits
    for (const SheetTemplate& existing : loadUserSheetTemplates(_folder))
    {
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            QMessageBox::warning(this, tr("Sheet template"),
                                 tr("You already have a template named \"%1\". Choose another name, or open that template and use Save.").arg(existing.name));
            return;
        }
    }
    QString error;
    const QString id = saveSheetTemplateAs(_tpl, name, _folder, &error);
    if (id.isEmpty())
    {
        QMessageBox::warning(this, tr("Sheet template"), error);
        return;
    }
    _savedId = id;
    accept();
}

// ---- one coordinate as a spin box -------------------------------------------------------------------------------------------------

SheetEdgeBox::SheetEdgeBox(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    _value = new QDoubleSpinBox(this);
    _value->setRange(-5000.0, 5000.0);
    _value->setDecimals(2);
    _value->setSingleStep(0.5);
    _value->setMinimumWidth(80);
    _unit = new QComboBox(this);
    _unit->addItem(QStringLiteral("mm"), false);
    _unit->addItem(QStringLiteral("%"), true);
    layout->addWidget(_value, 1);
    layout->addWidget(_unit);
    connect(_value, &QDoubleSpinBox::valueChanged, this, [this]() { emit edgeChanged(); });
    connect(_unit, &QComboBox::currentIndexChanged, this, [this]() {
        _value->setSingleStep(_unit->currentData().toBool() ? 1.0 : 0.5);
        emit edgeChanged();
    });
}

void SheetEdgeBox::setEdge(const SheetEdge& edge)
{
    const QSignalBlocker valueBlocker(_value);
    const QSignalBlocker unitBlocker(_unit);
    _unit->setCurrentIndex(edge.fraction ? 1 : 0);
    _value->setSingleStep(edge.fraction ? 1.0 : 0.5);
    _value->setValue(edge.fraction ? edge.value * 100.0 : edge.value * 25.4 / 72.0);
}

SheetEdge SheetEdgeBox::edge() const
{
    SheetEdge e;
    e.fraction = _unit->currentData().toBool();
    e.value = e.fraction ? _value->value() / 100.0 : _value->value() * 72.0 / 25.4;
    return e;
}

// ---- the Page tab -----------------------------------------------------------------------------------------------------------------

namespace
{
constexpr double kPtPerMm = 72.0 / 25.4;

}

void SheetTemplateEditor::setupPageTab()
{
    for (const auto& choice : sheetPageSizeChoices())
        ui->pageSize->addItem(choice.first, static_cast<int>(choice.second));
    ui->pageSize->addItem(tr("Custom"), static_cast<int>(QPageSize::Custom));
    ui->orientation->addItem(tr("Landscape"), true);
    ui->orientation->addItem(tr("Portrait"), false);
    ui->anchor->addItem(tr("Full width"), static_cast<int>(TitleBlockAnchor::Bottom));
    ui->anchor->addItem(tr("Corner, left"), static_cast<int>(TitleBlockAnchor::BottomLeft));
    ui->anchor->addItem(tr("Corner, right"), static_cast<int>(TitleBlockAnchor::BottomRight));

    _printCompanion = new QComboBox(ui->pageTab);
    _printCompanion->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    _printCompanion->setMinimumContentsLength(20);
    _printCompanion->setToolTip(tr("Use a separately authored template when printing in the opposite orientation. "
                                  "The paper size and margins still come from Page Setup."));
    ui->pageTabLayout->insertRow(ui->pageTabLayout->rowCount() - 1, tr("Opposite orientation for print:"), _printCompanion);
    connect(_printCompanion, &QComboBox::currentIndexChanged, this, [this] {
        _tpl.printCompanionId = _printCompanion->currentData().toString();
        revalidate();
    });

    for (QDoubleSpinBox* s : { ui->customWidth, ui->customHeight, ui->borderInset, ui->margin, ui->areaGap, ui->lineWidth, ui->blockWidth, ui->blockHeight, ui->blockMaxHeight, ui->growPadding })
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]() { applyPage(); });
    for (QComboBox* c : { ui->pageSize, ui->orientation, ui->anchor })
        connect(c, &QComboBox::currentIndexChanged, this, [this]() { applyPage(); });
}

void SheetTemplateEditor::loadPage()
{
    _loading = true;
    const int sizeIndex = ui->pageSize->findData(static_cast<int>(_tpl.pageSizeId));
    ui->pageSize->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : ui->pageSize->count() - 1);
    ui->orientation->setCurrentIndex(_tpl.landscape ? 0 : 1);
    // a custom page shows its own size; a named page shows its size for reference (the boxes are read-only then)
    const QSizeF shownSize = _tpl.pageSizeId == QPageSize::Custom ? _tpl.customSizeMm : QPageSize(_tpl.pageSizeId).size(QPageSize::Millimeter);
    ui->customWidth->setValue(shownSize.width());
    ui->customHeight->setValue(shownSize.height());
    ui->borderInset->setValue(_tpl.borderInset / kPtPerMm);
    ui->margin->setValue(_tpl.margin / kPtPerMm);
    ui->areaGap->setValue(_tpl.areaGap / kPtPerMm);
    ui->lineWidth->setValue(_tpl.lineWidth);
    ui->anchor->setCurrentIndex(std::max(0, ui->anchor->findData(static_cast<int>(_tpl.anchor))));
    ui->blockWidth->setValue(_tpl.blockWidth / kPtPerMm);
    ui->blockHeight->setValue(_tpl.blockHeight / kPtPerMm);
    ui->blockMaxHeight->setValue(_tpl.blockMaxHeight / kPtPerMm);
    ui->growPadding->setValue(_tpl.growPadding / kPtPerMm);
    const bool custom = ui->pageSize->currentData().toInt() == static_cast<int>(QPageSize::Custom);
    ui->customWidth->setEnabled(custom);
    ui->customHeight->setEnabled(custom);
    ui->blockWidth->setEnabled(_tpl.anchor != TitleBlockAnchor::Bottom);
    loadPrintCompanions();
    _loading = false;
}

void SheetTemplateEditor::loadPrintCompanions()
{
    const QSignalBlocker blocker(_printCompanion);
    _printCompanion->clear();
    _printCompanion->addItem(tr("Reflow this template"), QString());
    QVector<SheetTemplate> templates = builtInSheetTemplates();
    templates += loadUserSheetTemplates(_folder);
    for (const SheetTemplate& candidate : templates)
        if (candidate.id != _tpl.id && candidate.landscape != _tpl.landscape)
            _printCompanion->addItem(candidate.name, candidate.id);

    int index = _printCompanion->findData(_tpl.printCompanionId);
    if (index < 0)
    {
        _printCompanion->addItem(tr("Unavailable (%1)").arg(_tpl.printCompanionId), _tpl.printCompanionId);
        index = _printCompanion->count() - 1;
    }
    _printCompanion->setCurrentIndex(index);
}

void SheetTemplateEditor::applyPage()
{
    if (_loading)
        return;
    const SheetTemplate before = _tpl;
    _tpl.pageSizeId = static_cast<QPageSize::PageSizeId>(ui->pageSize->currentData().toInt());
    if (_tpl.pageSizeId == QPageSize::Custom && before.pageSizeId != QPageSize::Custom)
    {
        // switching to Custom: start from the size the page had, so the page does not jump to the smallest custom size
        const QSizeF current = QPageSize(before.pageSizeId).size(QPageSize::Millimeter);
        const QSignalBlocker widthBlocker(ui->customWidth);
        const QSignalBlocker heightBlocker(ui->customHeight);
        ui->customWidth->setValue(current.width());
        ui->customHeight->setValue(current.height());
    }
    if (_tpl.pageSizeId == QPageSize::Custom)
    {
        _tpl.customSizeMm = QSizeF(ui->customWidth->value(), ui->customHeight->value());
    }
    else
    {
        // the boxes follow the chosen size, for reference
        const QSizeF named = QPageSize(_tpl.pageSizeId).size(QPageSize::Millimeter);
        const QSignalBlocker widthBlocker(ui->customWidth);
        const QSignalBlocker heightBlocker(ui->customHeight);
        ui->customWidth->setValue(named.width());
        ui->customHeight->setValue(named.height());
    }
    _tpl.landscape = ui->orientation->currentData().toBool();
    _tpl.borderInset = ui->borderInset->value() * kPtPerMm;
    _tpl.margin = ui->margin->value() * kPtPerMm;
    _tpl.areaGap = ui->areaGap->value() * kPtPerMm;
    _tpl.lineWidth = ui->lineWidth->value();
    _tpl.anchor = static_cast<TitleBlockAnchor>(ui->anchor->currentData().toInt());
    _tpl.blockWidth = _tpl.anchor == TitleBlockAnchor::Bottom ? 0.0 : ui->blockWidth->value() * kPtPerMm;
    if (_tpl.anchor != TitleBlockAnchor::Bottom && _tpl.blockWidth <= 0.0)
        _tpl.blockWidth = 100.0 * kPtPerMm;   // a corner block needs a width of its own
    _tpl.blockHeight = ui->blockHeight->value() * kPtPerMm;
    _tpl.blockMaxHeight = std::max(ui->blockMaxHeight->value() * kPtPerMm, _tpl.blockHeight);   // the largest height is never below the height
    _tpl.growPadding = ui->growPadding->value() * kPtPerMm;
    {
        const bool custom = _tpl.pageSizeId == QPageSize::Custom;
        ui->customWidth->setEnabled(custom);
        ui->customHeight->setEnabled(custom);
        ui->blockWidth->setEnabled(_tpl.anchor != TitleBlockAnchor::Bottom);
        if (ui->blockMaxHeight->value() * kPtPerMm < _tpl.blockHeight)
        {
            const QSignalBlocker blocker(ui->blockMaxHeight);
            ui->blockMaxHeight->setValue(_tpl.blockHeight / kPtPerMm);
        }
    }
    adaptCellsToPage(before);
    if (_tpl.landscape != before.landscape)
        loadPrintCompanions();
    revalidate();
}

// The page, its orientation, the margins or the block changed the width of the title block: carry the layout of the cells over to the new
// width. Cells the user has not moved follow without asking. Cells the user has edited are only touched when they no longer fit: the user
// chooses to scale them, to keep them and fix them by hand, or to cancel the change of page.
void SheetTemplateEditor::adaptCellsToPage(const SheetTemplate& before)
{
    const double oldWidth = sheetBlockWidthPt(before);
    const double newWidth = sheetBlockWidthPt(_tpl);
    if (oldWidth <= 0.0 || newWidth <= 0.0 || std::fabs(newWidth - oldWidth) < 0.01)
        return;
    const SheetTemplate adapted = scaleSheetTemplateWidth(_tpl, newWidth / oldWidth);
    if (!_cellsEdited)
    {
        _tpl = adapted;
        refreshCellViews();
        return;
    }
    QString problem;
    if (!validateSheetTemplate(before))
        return;   // it was already invalid: the page is not what broke it
    if (validateSheetTemplate(_tpl, &problem))
        return;   // the edited cells still fit
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Sheet template"));
    box.setText(tr("Your edited cells do not fit the new page."));
    box.setInformativeText(tr("%1\n\nScale all cell positions to the new width, keep them as they are (and fix them by hand), or cancel the change of the page?").arg(problem));
    QPushButton* scale = box.addButton(tr("Scale the cells"), QMessageBox::AcceptRole);
    QPushButton* keep = box.addButton(tr("Keep my cells"), QMessageBox::ActionRole);
    QPushButton* cancel = box.addButton(tr("Cancel the change"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == scale)
    {
        _tpl = adapted;
        refreshCellViews();
    }
    else if (box.clickedButton() == cancel)
    {
        _tpl = before;
        loadPage();
        refreshCellViews();
    }
    Q_UNUSED(keep);
}

// The cell list, the cell properties and the logo position follow the template after it was changed as a whole.
void SheetTemplateEditor::refreshCellViews()
{
    fillList(_tpl.cells.isEmpty() ? -1 : std::max(0, ui->cellList->currentRow()));
    loadLogo();
}

// ---- the Logo tab -----------------------------------------------------------------------------------------------------------------

void SheetTemplateEditor::setupLogoTab()
{
    const QString edgeTip = tr("Distance from the left or top edge of the title block, in millimetres or as a percentage of the block.\n"
                               "A negative number is measured from the right or bottom edge instead.");
    for (SheetEdgeBox* e : { ui->logoLeft, ui->logoTop, ui->logoRight, ui->logoBottom })
        e->setToolTip(edgeTip);

    connect(ui->logoChoose, &QPushButton::clicked, this, [this]() { chooseLogo(); });
    connect(ui->logoRemove, &QPushButton::clicked, this, [this]() { removeLogo(); });
    for (SheetEdgeBox* e : { ui->logoLeft, ui->logoTop, ui->logoRight, ui->logoBottom })
        connect(e, &SheetEdgeBox::edgeChanged, this, [this]() { applyLogoEdits(); });
    connect(ui->logoContain, &QCheckBox::toggled, this, [this]() { applyLogoEdits(); });
}

void SheetTemplateEditor::loadLogo()
{
    _loading = true;
    const bool has = _tpl.hasLogo;
    ui->logoFileLabel->setText(has ? QFileInfo(_tpl.logo.file).fileName() : tr("none"));
    ui->logoRemove->setEnabled(has);
    for (QWidget* w : { static_cast<QWidget*>(ui->logoLeft), static_cast<QWidget*>(ui->logoTop), static_cast<QWidget*>(ui->logoRight),
                        static_cast<QWidget*>(ui->logoBottom), static_cast<QWidget*>(ui->logoContain) })
        w->setEnabled(has);
    if (has)
    {
        ui->logoLeft->setEdge(_tpl.logo.rect.left);
        ui->logoTop->setEdge(_tpl.logo.rect.top);
        ui->logoRight->setEdge(_tpl.logo.rect.right);
        ui->logoBottom->setEdge(_tpl.logo.rect.bottom);
        ui->logoContain->setChecked(_tpl.logo.contain);
    }
    else
    {
        for (SheetEdgeBox* e : { ui->logoLeft, ui->logoTop, ui->logoRight, ui->logoBottom })
            e->setEdge(SheetEdge());
    }
    _loading = false;
}

void SheetTemplateEditor::applyLogoEdits()
{
    if (_loading || !_tpl.hasLogo)
        return;
    _tpl.logo.contain = ui->logoContain->isChecked();
    struct EdgeBox
    {
        SheetEdgeBox* box;
        SheetEdge* target;
    };
    const EdgeBox edges[] = { { ui->logoLeft, &_tpl.logo.rect.left }, { ui->logoTop, &_tpl.logo.rect.top },
                              { ui->logoRight, &_tpl.logo.rect.right }, { ui->logoBottom, &_tpl.logo.rect.bottom } };
    for (const EdgeBox& e : edges)
    {
        const SheetEdge value = e.box->edge();
        const double tolerance = value.fraction ? 0.0005 : 0.02;
        if (value.fraction != e.target->fraction || std::fabs(value.value - e.target->value) > tolerance)
            _cellsEdited = true;
        *e.target = value;
    }
    revalidate();
}

void SheetTemplateEditor::chooseLogo()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Logo"), QString(), tr("Images (*.png *.jpg *.jpeg *.svg)"));
    if (file.isEmpty())
        return;
    if (!_scratch.isValid())
    {
        QMessageBox::warning(this, tr("Sheet template"), tr("A temporary folder for the logo could not be created."));
        return;
    }
    SheetTemplate trial = _tpl;
    QString error;
    // the picture goes to the scratch folder; it reaches the template's folder when the template is saved
    if (!setSheetTemplateLogo(&trial, file, _scratch.path(), &error))
    {
        QMessageBox::warning(this, tr("Sheet template"), tr("The logo could not be added: %1").arg(error));
        return;
    }
    trial.folder = _scratch.path();
    if (!validateSheetTemplate(trial, &error))
    {
        QMessageBox::warning(this, tr("Sheet template"), tr("The logo could not be added: %1").arg(error));
        return;
    }
    _tpl = trial;
    _cellsEdited = true;   // adding a logo made room by moving the cells
    fillList(std::max(0, ui->cellList->currentRow()));
    loadPage();
    loadLogo();
    revalidate();
}

void SheetTemplateEditor::removeLogo()
{
    if (!_tpl.hasLogo)
        return;
    _tpl.hasLogo = false;
    _tpl.logo = SheetLogo();
    loadLogo();
    revalidate();
}

// A logo that was chosen in this editor sits in the scratch folder: before a plain Save it is copied next to the template.
bool SheetTemplateEditor::ensureLogoInFolder(QString* error)
{
    if (!_tpl.hasLogo)
        return true;
    const QString path = sheetLogoPath(_tpl);
    const auto folderOf = [](const QString& file) { return QDir::cleanPath(QFileInfo(file).absolutePath()); };
    if (path.isEmpty() || folderOf(path) == QDir::cleanPath(QDir(_folder).absolutePath()))
        return true;
    if (folderOf(path) != QDir::cleanPath(QDir(_scratch.path()).absolutePath()))
        return true;   // a logo that is already part of the template's own folder or another place the file names
    if (!setSheetTemplateLogo(&_tpl, path, _folder, error))
        return false;
    _tpl.folder = _folder;
    return true;
}

// ---- the Fields tab ---------------------------------------------------------------------------------------------------------------

void SheetTemplateEditor::setupFieldsTab()
{
    ui->fieldType->addItem(tr("Text"), QStringLiteral("text"));
    ui->fieldType->addItem(tr("Several lines"), QStringLiteral("multiline"));
    ui->fieldType->addItem(tr("Date"), QStringLiteral("date"));
    ui->fieldType->addItem(tr("Choice from a list"), QStringLiteral("choice"));
    ui->fieldType->addItem(tr("Number"), QStringLiteral("number"));

    connect(ui->fieldList, &QListWidget::currentRowChanged, this, [this](int row) { loadField(row); });
    connect(ui->fieldAdd, &QPushButton::clicked, this, [this]() { addField(); });
    connect(ui->fieldAddCell, &QPushButton::clicked, this, [this]() { addCellForField(); });
    connect(ui->fieldDelete, &QPushButton::clicked, this, [this]() { deleteField(); });
    connect(ui->fieldUp, &QPushButton::clicked, this, [this]() { moveField(-1); });
    connect(ui->fieldDown, &QPushButton::clicked, this, [this]() { moveField(1); });
    for (QLineEdit* e : { ui->fieldId, ui->fieldLabel, ui->fieldDefault })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyFieldEdits(); });
    connect(ui->fieldType, &QComboBox::currentIndexChanged, this, [this]() { applyFieldEdits(); });
    connect(ui->fieldOptions, &QPlainTextEdit::textChanged, this, [this]() { applyFieldEdits(); });
    connect(ui->fieldDecimals, &QSpinBox::valueChanged, this, [this]() { applyFieldEdits(); });
    for (QLineEdit* e : { ui->fieldUnit, ui->fieldMin, ui->fieldMax })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyFieldEdits(); });
}

void SheetTemplateEditor::fillFieldList(int selectRow)
{
    const QSignalBlocker blocker(ui->fieldList);
    ui->fieldList->clear();
    for (const SheetField& f : std::as_const(_tpl.fields))
        ui->fieldList->addItem(QStringLiteral("{%1}  %2").arg(f.id, f.label));
    const int row = (selectRow >= 0 && selectRow < _tpl.fields.size()) ? selectRow : -1;
    ui->fieldList->setCurrentRow(row);
    loadField(row);
}

void SheetTemplateEditor::loadField(int row)
{
    const bool have = row >= 0 && row < _tpl.fields.size();
    _loading = true;
    for (QWidget* w : { static_cast<QWidget*>(ui->fieldId), static_cast<QWidget*>(ui->fieldLabel), static_cast<QWidget*>(ui->fieldType),
                        static_cast<QWidget*>(ui->fieldDefault), static_cast<QWidget*>(ui->fieldOptions) })
        w->setEnabled(have);
    ui->fieldAddCell->setEnabled(have);
    if (have)
    {
        const SheetField& f = _tpl.fields[row];
        ui->fieldId->setText(f.id);
        ui->fieldLabel->setText(f.label);
        ui->fieldType->setCurrentIndex(std::max(0, ui->fieldType->findData(f.type)));
        ui->fieldDefault->setText(f.defaultValue);
        ui->fieldOptions->setPlainText(f.options.join(QLatin1Char('\n')));
        ui->fieldDecimals->setValue(f.decimals);
        ui->fieldUnit->setText(f.unit);
        ui->fieldMin->setText(f.minimum == -1e9 ? QString() : _locale.toString(f.minimum, 'g', 10));
        ui->fieldMax->setText(f.maximum == 1e9 ? QString() : _locale.toString(f.maximum, 'g', 10));
    }
    else
    {
        ui->fieldId->clear();
        ui->fieldLabel->clear();
        ui->fieldDefault->clear();
        ui->fieldOptions->clear();
        ui->fieldUnit->clear();
        ui->fieldMin->clear();
        ui->fieldMax->clear();
    }
    const QString loadedType = ui->fieldType->currentData().toString();
    ui->fieldOptions->setEnabled(have && loadedType == QLatin1String("choice"));
    for (QWidget* w : { static_cast<QWidget*>(ui->fieldDecimals), static_cast<QWidget*>(ui->fieldUnit), static_cast<QWidget*>(ui->fieldMin), static_cast<QWidget*>(ui->fieldMax) })
        w->setEnabled(have && loadedType == QLatin1String("number"));
    _loading = false;
    ui->fieldAdd->setEnabled(_tpl.fields.size() < 16);
    ui->fieldDelete->setEnabled(have);
    ui->fieldUp->setEnabled(have && row > 0);
    ui->fieldDown->setEnabled(have && row + 1 < _tpl.fields.size());
}

void SheetTemplateEditor::applyFieldEdits()
{
    const int row = ui->fieldList->currentRow();
    if (_loading || row < 0 || row >= _tpl.fields.size())
        return;
    SheetField& f = _tpl.fields[row];
    f.id = ui->fieldId->text().trimmed();
    f.label = ui->fieldLabel->text();
    f.type = ui->fieldType->currentData().toString();
    f.defaultValue = ui->fieldDefault->text();
    f.options.clear();
    if (f.type == QLatin1String("choice"))
    {
        for (const QString& line : ui->fieldOptions->toPlainText().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                f.options.append(line.trimmed());
    }
    ui->fieldOptions->setEnabled(f.type == QLatin1String("choice"));
    QString numberError;
    const bool isNumber = f.type == QLatin1String("number");
    for (QWidget* w : { static_cast<QWidget*>(ui->fieldDecimals), static_cast<QWidget*>(ui->fieldUnit), static_cast<QWidget*>(ui->fieldMin), static_cast<QWidget*>(ui->fieldMax) })
        w->setEnabled(isNumber);
    if (isNumber)
    {
        f.decimals = ui->fieldDecimals->value();
        f.unit = ui->fieldUnit->text().trimmed();
        const auto limit = [&](QLineEdit* edit, double* target, double none, const QString& name) {
            const QString text = edit->text().trimmed();
            double value = 0.0;
            if (text.isEmpty())
                *target = none;
            else if (parseSheetNumber(text, _locale, &value))
                *target = value;
            else if (numberError.isEmpty())
                numberError = tr("%1 is not a number").arg(name);
        };
        limit(ui->fieldMin, &f.minimum, -1e9, tr("Minimum"));
        limit(ui->fieldMax, &f.maximum, 1e9, tr("Maximum"));
    }
    else
    {
        f.decimals = 2;
        f.unit.clear();
        f.minimum = -1e9;
        f.maximum = 1e9;
    }
    if (QListWidgetItem* item = ui->fieldList->item(row))
        item->setText(QStringLiteral("{%1}  %2").arg(f.id, f.label));
    refreshInsertCombo();
    revalidate(numberError);
}

void SheetTemplateEditor::addField()
{
    if (_tpl.fields.size() >= 16)
        return;
    SheetField f;
    for (int n = 1; f.id.isEmpty(); ++n)
    {
        const QString candidate = QStringLiteral("field%1").arg(n);
        const bool used = std::any_of(_tpl.fields.cbegin(), _tpl.fields.cend(), [&](const SheetField& o) { return o.id == candidate; });
        if (!used)
            f.id = candidate;
    }
    f.label = tr("New field");
    const int row = ui->fieldList->currentRow();
    const int at = (row >= 0 && row < _tpl.fields.size()) ? row + 1 : _tpl.fields.size();
    _tpl.fields.insert(at, f);
    fillFieldList(at);
    refreshInsertCombo();
    revalidate();
}

void SheetTemplateEditor::addCellForField()
{
    const int fieldRow = ui->fieldList->currentRow();
    if (fieldRow < 0 || fieldRow >= _tpl.fields.size())
        return;
    const SheetField field = _tpl.fields[fieldRow];
    ui->tabs->setCurrentIndex(0);   // the Cells tab
    addCell();                   // inserts below the selected cell and selects the new one
    const int cellRow = ui->cellList->currentRow();
    if (cellRow < 0 || cellRow >= _tpl.cells.size())
        return;
    _tpl.cells[cellRow].label = field.label;
    _tpl.cells[cellRow].text = QStringLiteral("{%1}").arg(field.id);
    fillList(cellRow);
    revalidate();
}

void SheetTemplateEditor::deleteField()
{
    const int row = ui->fieldList->currentRow();
    if (row < 0 || row >= _tpl.fields.size())
        return;
    _tpl.fields.removeAt(row);
    fillFieldList(std::min(row, static_cast<int>(_tpl.fields.size()) - 1));
    refreshInsertCombo();
    revalidate();
}

void SheetTemplateEditor::moveField(int delta)
{
    const int row = ui->fieldList->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || row >= _tpl.fields.size() || to >= _tpl.fields.size())
        return;
    std::swap(_tpl.fields[row], _tpl.fields[to]);
    fillFieldList(to);
    revalidate();
}

// The "Insert field" menu of the Cells tab: the standard and scene placeholders, then the template's own fields.
void SheetTemplateEditor::refreshInsertCombo()
{
    const QSignalBlocker blocker(ui->insertCombo);
    ui->insertCombo->clear();
    ui->insertCombo->addItem(tr("Insert field..."));
    QStringList names = standardSheetPlaceholders();
    names += sceneSheetPlaceholders();
    for (const SheetField& field : std::as_const(_tpl.fields))
        if (!field.id.isEmpty() && !names.contains(field.id))
            names << field.id;
    for (const QString& n : names)
        ui->insertCombo->addItem(QStringLiteral("{%1}").arg(n), n);
}

// The values the preview prints: the export dialog's, and a field's default (or first choice) where the dialog has nothing for it yet.
QMap<QString, QString> SheetTemplateEditor::previewValues() const
{
    QMap<QString, QString> shown = _values;
    for (const SheetField& f : _tpl.fields)
    {
        if (!shown.value(f.id).isEmpty())
            continue;
        QString text = !f.defaultValue.isEmpty() ? f.defaultValue : (f.type == QLatin1String("choice") && !f.options.isEmpty() ? f.options.first() : QString());
        double number = 0.0;
        if (f.type == QLatin1String("number") && parseSheetNumber(text, QLocale::c(), &number))
            text = formatSheetNumber(f, number, _locale);   // a number is shown the way the sheet will print it
        shown.insert(f.id, text);
    }
    return shown;
}

} // namespace Pdf3D
