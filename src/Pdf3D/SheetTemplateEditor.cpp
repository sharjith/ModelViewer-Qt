#include "SheetTemplateEditor.h"

#include "SheetPreviewWidget.h"

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
    , _tpl(tpl)
    , _valid(tpl)
    , _values(sampleValues)
    , _folder(folder)
    , _locale(locale)
    , _isUser(tpl.id.startsWith(QLatin1String("user:")))
{
    setWindowTitle(tr("Edit sheet template"));
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

void SheetTemplateEditor::buildUi()
{
    auto* outer = new QHBoxLayout(this);

    // left: the preview
    auto* left = new QVBoxLayout;
    _preview = new SheetPreviewWidget(this);
    _preview->setMinimumSize(420, 300);
    _preview->setTemplate(_tpl);
    _preview->setValues(_values);
    _preview->setEditable(true);
    left->addWidget(_preview, 1);
    auto* hint = new QLabel(tr("Click a cell in the sheet to select it. Drag it to move it, drag its edges or corners to resize it, or nudge it with the "
                               "arrow keys (Shift for larger steps). Edges snap to the title block and to the other cells. The 3D area is shown grey."), this);
    hint->setWordWrap(true);
    left->addWidget(hint);
    outer->addLayout(left, 5);
    connect(_preview, &SheetPreviewWidget::cellGeometryChanged, this, [this](int index, const QRectF& rect, const QSizeF& size) { moveCellTo(index, rect, size); });
    connect(_preview, &SheetPreviewWidget::cellClicked, this, [this](int index) {
        if (index >= 0 && index < _cellList->count())
            _cellList->setCurrentRow(index);
    });

    // right: name, cell list, properties, buttons
    auto* right = new QVBoxLayout;
    auto* nameRow = new QFormLayout;
    _nameEdit = new QLineEdit(_tpl.name, this);
    _nameEdit->setToolTip(tr("The name of the template, as listed in the Save as 3D PDF dialog."));
    nameRow->addRow(tr("Template name:"), _nameEdit);
    right->addLayout(nameRow);
    connect(_nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        _tpl.name = text;
        revalidate();
    });

    _tabs = new QTabWidget(this);
    auto* cellsTab = new QWidget(_tabs);
    auto* cellsTabLayout = new QVBoxLayout(cellsTab);
    auto* cellsBox = new QGroupBox(tr("Title block cells"), this);
    auto* cellsLayout = new QVBoxLayout(cellsBox);
    _cellList = new QListWidget(cellsBox);
    _cellList->setMinimumHeight(110);
    cellsLayout->addWidget(_cellList, 1);
    auto* listButtons = new QHBoxLayout;
    _addButton = new QPushButton(tr("Add"), cellsBox);
    _duplicateButton = new QPushButton(tr("Duplicate"), cellsBox);
    _deleteButton = new QPushButton(tr("Delete"), cellsBox);
    _upButton = new QPushButton(tr("Up"), cellsBox);
    _downButton = new QPushButton(tr("Down"), cellsBox);
    _upButton->setToolTip(tr("Move the cell up in the list. Cells drawn later are on top of earlier ones."));
    _downButton->setToolTip(tr("Move the cell down in the list."));
    for (QPushButton* b : { _addButton, _duplicateButton, _deleteButton, _upButton, _downButton })
        listButtons->addWidget(b);
    cellsLayout->addLayout(listButtons);
    cellsTabLayout->addWidget(cellsBox, 1);
    connect(_cellList, &QListWidget::currentRowChanged, this, [this](int row) { loadCell(row); });
    connect(_addButton, &QPushButton::clicked, this, [this]() { addCell(); });
    connect(_duplicateButton, &QPushButton::clicked, this, [this]() { duplicateCell(); });
    connect(_deleteButton, &QPushButton::clicked, this, [this]() { deleteCell(); });
    connect(_upButton, &QPushButton::clicked, this, [this]() { moveCell(-1); });
    connect(_downButton, &QPushButton::clicked, this, [this]() { moveCell(1); });

    auto* propsBox = new QGroupBox(tr("Selected cell"), this);
    auto* form = new QFormLayout(propsBox);
    _labelEdit = new QLineEdit(propsBox);
    _labelEdit->setToolTip(tr("A small caption in the corner of the cell. A cell with a caption is drawn as a field of the title block."));
    form->addRow(tr("Caption:"), _labelEdit);

    _textEdit = new QLineEdit(propsBox);
    _textEdit->setToolTip(tr("The text of the cell. {title}, {author} ... are replaced by the values of the dialog;\n"
                             "{name|-} prints a dash when the value is empty."));
    _insertCombo = new QComboBox(propsBox);
    _insertCombo->setToolTip(tr("Insert a field at the cursor of the text."));
    _insertCombo->addItem(tr("Insert field..."));
    QStringList names = standardSheetPlaceholders();
    names += sceneSheetPlaceholders();
    for (const SheetField& field : _tpl.fields)
        if (!names.contains(field.id))
            names << field.id;
    for (const QString& n : names)
        _insertCombo->addItem(QStringLiteral("{%1}").arg(n), n);
    auto* textRow = new QHBoxLayout;
    textRow->addWidget(_textEdit, 1);
    textRow->addWidget(_insertCombo);
    form->addRow(tr("Text:"), textRow);
    connect(_insertCombo, &QComboBox::activated, this, [this](int index) { insertPlaceholder(index); });

    _fontSize = new QDoubleSpinBox(propsBox);
    _fontSize->setRange(4.0, 72.0);
    _fontSize->setDecimals(1);
    _fontSize->setSuffix(tr(" pt"));
    _bold = new QCheckBox(tr("Bold"), propsBox);
    _colourButton = new QPushButton(propsBox);
    _colourButton->setToolTip(tr("The text colour."));
    _align = new QComboBox(propsBox);
    _align->addItem(tr("Left"), static_cast<int>(Qt::AlignLeft));
    _align->addItem(tr("Centre"), static_cast<int>(Qt::AlignHCenter));
    _align->addItem(tr("Right"), static_cast<int>(Qt::AlignRight));
    auto* fontRow = new QHBoxLayout;
    fontRow->addWidget(_fontSize);
    fontRow->addWidget(_bold);
    fontRow->addWidget(_colourButton);
    fontRow->addWidget(_align);
    fontRow->addStretch(1);
    form->addRow(tr("Font:"), fontRow);

    _wrap = new QCheckBox(tr("Wrap long text"), propsBox);
    _scroll = new QCheckBox(tr("Scrollable when it does not fit"), propsBox);
    _scroll->setToolTip(tr("Text that does not fit becomes a scrollable form field over the cell. One cell per template."));
    _grow = new QCheckBox(tr("The title block grows with this cell"), propsBox);
    _grow->setToolTip(tr("A long text enlarges the title block (up to its maximum height) and takes room from the 3D area. One cell per template."));
    form->addRow(QString(), _wrap);
    form->addRow(QString(), _scroll);
    form->addRow(QString(), _grow);

    const QString edgeTip = tr("Distance from the left or top edge of the title block, in millimetres or as a percentage of the block.\n"
                               "A negative number is measured from the right or bottom edge instead.");
    _leftEdit = new SheetEdgeBox(propsBox);
    _topEdit = new SheetEdgeBox(propsBox);
    _rightEdit = new SheetEdgeBox(propsBox);
    _bottomEdit = new SheetEdgeBox(propsBox);
    for (SheetEdgeBox* e : { _leftEdit, _topEdit, _rightEdit, _bottomEdit })
        e->setToolTip(edgeTip);
    auto* positionGrid = new QGridLayout;
    positionGrid->addWidget(new QLabel(tr("Left"), propsBox), 0, 0);
    positionGrid->addWidget(_leftEdit, 0, 1);
    positionGrid->addWidget(new QLabel(tr("Top"), propsBox), 0, 2);
    positionGrid->addWidget(_topEdit, 0, 3);
    positionGrid->addWidget(new QLabel(tr("Right"), propsBox), 1, 0);
    positionGrid->addWidget(_rightEdit, 1, 1);
    positionGrid->addWidget(new QLabel(tr("Bottom"), propsBox), 1, 2);
    positionGrid->addWidget(_bottomEdit, 1, 3);
    positionGrid->setColumnStretch(4, 1);
    form->addRow(tr("Position:"), positionGrid);
    cellsTabLayout->addWidget(propsBox);
    _tabs->addTab(cellsTab, tr("Cells"));
    buildPageTab();
    buildLogoTab();
    buildFieldsTab();
    right->addWidget(_tabs, 1);

    for (QLineEdit* e : { _labelEdit, _textEdit })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyCellEdits(); });
    for (SheetEdgeBox* e : { _leftEdit, _topEdit, _rightEdit, _bottomEdit })
        connect(e, &SheetEdgeBox::edgeChanged, this, [this]() { applyCellEdits(); });
    connect(_fontSize, &QDoubleSpinBox::valueChanged, this, [this]() { applyCellEdits(); });
    connect(_align, &QComboBox::currentIndexChanged, this, [this]() { applyCellEdits(); });
    for (QCheckBox* c : { _bold, _wrap, _scroll, _grow })
        connect(c, &QCheckBox::toggled, this, [this]() { applyCellEdits(); });
    connect(_colourButton, &QPushButton::clicked, this, [this]() { chooseColour(); });

    _errorLabel = new QLabel(this);
    _errorLabel->setWordWrap(true);
    QPalette errorPalette = _errorLabel->palette();
    errorPalette.setColor(QPalette::WindowText, QColor(220, 80, 80));
    _errorLabel->setPalette(errorPalette);
    right->addWidget(_errorLabel);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    _saveButton = new QPushButton(tr("Save"), this);
    _saveButton->setToolTip(_isUser ? tr("Writes the changes into your template.") : tr("A built-in template cannot be changed: use Save as."));
    _saveAsButton = new QPushButton(tr("Save as..."), this);
    _saveAsButton->setToolTip(tr("Saves the edited template as a new template of yours."));
    auto* cancel = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(_saveButton);
    buttons->addWidget(_saveAsButton);
    buttons->addWidget(cancel);
    right->addLayout(buttons);
    connect(_saveButton, &QPushButton::clicked, this, [this]() { save(); });
    connect(_saveAsButton, &QPushButton::clicked, this, [this]() { saveAs(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    outer->addLayout(right, 3);
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

QString SheetTemplateEditor::listText(const SheetCell& cell) const
{
    const QString caption = cell.label.isEmpty() ? QString() : cell.label + QStringLiteral(": ");
    return elided(caption + (cell.text.isEmpty() ? tr("(empty)") : cell.text), 48);
}

void SheetTemplateEditor::fillList(int selectRow)
{
    const QSignalBlocker blocker(_cellList);
    _cellList->clear();
    for (const SheetCell& c : std::as_const(_tpl.cells))
        _cellList->addItem(listText(c));
    const int row = (selectRow >= 0 && selectRow < _tpl.cells.size()) ? selectRow : -1;
    _cellList->setCurrentRow(row);
    loadCell(row);
}

void SheetTemplateEditor::loadCell(int row)
{
    const bool have = row >= 0 && row < _tpl.cells.size();
    _loading = true;
    for (QWidget* w : { static_cast<QWidget*>(_labelEdit), static_cast<QWidget*>(_textEdit), static_cast<QWidget*>(_insertCombo),
                        static_cast<QWidget*>(_fontSize), static_cast<QWidget*>(_bold), static_cast<QWidget*>(_colourButton),
                        static_cast<QWidget*>(_align), static_cast<QWidget*>(_wrap), static_cast<QWidget*>(_scroll),
                        static_cast<QWidget*>(_grow), static_cast<QWidget*>(_leftEdit), static_cast<QWidget*>(_topEdit),
                        static_cast<QWidget*>(_rightEdit), static_cast<QWidget*>(_bottomEdit) })
        w->setEnabled(have);
    if (have)
    {
        const SheetCell& c = _tpl.cells[row];
        _labelEdit->setText(c.label);
        _textEdit->setText(c.text);
        _fontSize->setValue(c.fontSize);
        _bold->setChecked(c.bold);
        const Qt::Alignment h = c.align & Qt::AlignHorizontal_Mask;
        _align->setCurrentIndex(h == Qt::AlignHCenter ? 1 : h == Qt::AlignRight ? 2 : 0);
        _wrap->setChecked(c.wrap);
        _scroll->setChecked(c.scrollWhenOverflow);
        _grow->setChecked(!c.id.isEmpty() && _tpl.growWithCell == c.id);
        _leftEdit->setEdge(c.rect.left);
        _topEdit->setEdge(c.rect.top);
        _rightEdit->setEdge(c.rect.right);
        _bottomEdit->setEdge(c.rect.bottom);
    }
    else
    {
        for (QLineEdit* e : { _labelEdit, _textEdit })
            e->clear();
        for (SheetEdgeBox* e : { _leftEdit, _topEdit, _rightEdit, _bottomEdit })
            e->setEdge(SheetEdge());
    }
    _loading = false;
    updateColourButton();
    _deleteButton->setEnabled(have);
    _duplicateButton->setEnabled(have);
    _upButton->setEnabled(have && row > 0);
    _downButton->setEnabled(have && row + 1 < _tpl.cells.size());
    _preview->setSelectedCell(have ? row : -1);
}

void SheetTemplateEditor::updateColourButton()
{
    const int row = _cellList->currentRow();
    const QColor colour = (row >= 0 && row < _tpl.cells.size()) ? _tpl.cells[row].color : QColor(Qt::black);
    _colourButton->setText(colour.name());
    const QColor text = colour.lightness() > 140 ? QColor(Qt::black) : QColor(Qt::white);
    _colourButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(colour.name(), text.name()));
}

// Reads the property widgets into the selected cell, then checks the template. A position that cannot be read keeps the old value of
// that edge, is marked red and is reported.
void SheetTemplateEditor::applyCellEdits()
{
    const int row = _cellList->currentRow();
    if (_loading || row < 0 || row >= _tpl.cells.size())
        return;
    SheetCell& c = _tpl.cells[row];
    c.label = _labelEdit->text();
    c.text = _textEdit->text();
    c.fontSize = _fontSize->value();
    c.bold = _bold->isChecked();
    c.align = Qt::Alignment(static_cast<Qt::AlignmentFlag>(_align->currentData().toInt()));
    c.wrap = _wrap->isChecked();
    c.scrollWhenOverflow = _scroll->isChecked();

    // one cell per template may scroll and one may grow the block: choosing it here releases the others
    if (c.scrollWhenOverflow)
        for (int i = 0; i < _tpl.cells.size(); ++i)
            if (i != row)
                _tpl.cells[i].scrollWhenOverflow = false;
    if (_grow->isChecked())
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
    const EdgeBox edges[] = { { _leftEdit, &c.rect.left }, { _topEdit, &c.rect.top }, { _rightEdit, &c.rect.right }, { _bottomEdit, &c.rect.bottom } };
    for (const EdgeBox& e : edges)
    {
        const SheetEdge value = e.box->edge();
        const double tolerance = value.fraction ? 0.0005 : 0.02;   // rounding of the spin box is not an edit
        if (value.fraction != e.target->fraction || std::fabs(value.value - e.target->value) > tolerance)
            _cellsEdited = true;
        *e.target = value;
    }

    if (QListWidgetItem* item = _cellList->item(row))
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
        _preview->setTemplate(_valid);
        _preview->setValues(previewValues());
        _preview->setSelectedCell(_cellList->currentRow());
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
                if (_cellList->currentRow() != row)
                    _cellList->setCurrentRow(row);
            }
        }
    }
    QString shown = !extraError.isEmpty() ? extraError : error;
    if (!shown.isEmpty())
        shown[0] = shown.at(0).toUpper();
    _errorLabel->setText(shown);
    const bool ok = valid && extraError.isEmpty();
    _saveButton->setEnabled(ok && _isUser);
    _saveAsButton->setEnabled(ok);
}

// A new cell goes directly under the selected one, with the same left and right edges, so it only has to be dragged into place; with no
// cell selected it starts at the top left of the block.
void SheetTemplateEditor::addCell()
{
    const int row = _cellList->currentRow();
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
    if (_cellList->currentRow() != index)
        _cellList->setCurrentRow(index);   // loads the cell, positions included
    const SheetCell& c = _tpl.cells[index];
    _loading = true;
    _leftEdit->setEdge(c.rect.left);
    _topEdit->setEdge(c.rect.top);
    _rightEdit->setEdge(c.rect.right);
    _bottomEdit->setEdge(c.rect.bottom);
    _loading = false;
    revalidate();
}

void SheetTemplateEditor::duplicateCell()
{
    const int row = _cellList->currentRow();
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
    const int row = _cellList->currentRow();
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
    const int row = _cellList->currentRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || row >= _tpl.cells.size() || to >= _tpl.cells.size())
        return;
    std::swap(_tpl.cells[row], _tpl.cells[to]);
    fillList(to);
    revalidate();
}

void SheetTemplateEditor::chooseColour()
{
    const int row = _cellList->currentRow();
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
    const QString name = _insertCombo->itemData(comboIndex).toString();
    _insertCombo->setCurrentIndex(0);
    if (!name.isEmpty())
    {
        _textEdit->insert(QStringLiteral("{%1}").arg(name));
        _textEdit->setFocus();
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

QDoubleSpinBox* mmSpin(QWidget* parent, double minimum, double maximum)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(minimum, maximum);
    s->setDecimals(1);
    s->setSingleStep(1.0);
    s->setSuffix(QStringLiteral(" mm"));
    s->setKeyboardTracking(false);   // report a typed value when it is finished, not on every digit
    return s;
}
}

void SheetTemplateEditor::buildPageTab()
{
    auto* page = new QWidget(_tabs);
    auto* form = new QFormLayout(page);

    _pageSize = new QComboBox(page);
    for (const auto& choice : sheetPageSizeChoices())
        _pageSize->addItem(choice.first, static_cast<int>(choice.second));
    _pageSize->addItem(tr("Custom"), static_cast<int>(QPageSize::Custom));
    _orientation = new QComboBox(page);
    _orientation->addItem(tr("Landscape"), true);
    _orientation->addItem(tr("Portrait"), false);
    _customWidth = mmSpin(page, 100.0, 5000.0);
    _customHeight = mmSpin(page, 100.0, 5000.0);
    _customWidth->setToolTip(tr("Width of the page in millimetres, before the orientation is applied. Editable for a custom page (100 to 5000 mm);\n"
                                "for a named size it shows that size for reference."));
    _customHeight->setToolTip(tr("Height of the page in millimetres, before the orientation is applied. Editable for a custom page (100 to 5000 mm);\n"
                                 "for a named size it shows that size for reference."));
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(_pageSize);
    sizeRow->addWidget(_orientation);
    sizeRow->addWidget(_customWidth);
    sizeRow->addWidget(new QLabel(QStringLiteral("x"), page));
    sizeRow->addWidget(_customHeight);
    sizeRow->addStretch(1);
    form->addRow(tr("Page:"), sizeRow);

    _borderInset = mmSpin(page, 0.0, 70.0);
    _borderInset->setToolTip(tr("Distance from the page edge to the border line."));
    _margin = mmSpin(page, 0.0, 140.0);
    _margin->setToolTip(tr("Distance from the page edge to the 3D area and the title block."));
    _areaGap = mmSpin(page, 0.0, 70.0);
    _areaGap->setToolTip(tr("Space between the 3D area and the title block."));
    _lineWidth = new QDoubleSpinBox(page);
    _lineWidth->setRange(0.05, 5.0);
    _lineWidth->setDecimals(2);
    _lineWidth->setSingleStep(0.1);
    _lineWidth->setSuffix(tr(" pt"));
    _lineWidth->setKeyboardTracking(false);
    _lineWidth->setToolTip(tr("Thickness of the border, the 3D area frame, the title block and its lines."));
    form->addRow(tr("Border inset:"), _borderInset);
    form->addRow(tr("Margin:"), _margin);
    form->addRow(tr("Gap above the title block:"), _areaGap);
    form->addRow(tr("Line width:"), _lineWidth);

    auto* block = new QGroupBox(tr("Title block"), page);
    auto* blockForm = new QFormLayout(block);
    _anchor = new QComboBox(block);
    _anchor->addItem(tr("Full width"), static_cast<int>(TitleBlockAnchor::Bottom));
    _anchor->addItem(tr("Corner, left"), static_cast<int>(TitleBlockAnchor::BottomLeft));
    _anchor->addItem(tr("Corner, right"), static_cast<int>(TitleBlockAnchor::BottomRight));
    _blockWidth = mmSpin(block, 17.0, 5000.0);
    _blockWidth->setToolTip(tr("Width of a title block in a corner of the page (a full width block spans the margins)."));
    _blockHeight = mmSpin(block, 7.0, 2500.0);
    _blockHeight->setToolTip(tr("Height of the title block. At most half the page height."));
    _blockMaxHeight = mmSpin(block, 7.0, 2500.0);
    _blockMaxHeight->setToolTip(tr("The title block grows up to this height when the cell chosen on the Cells tab needs more room."));
    _growPadding = mmSpin(block, 0.0, 35.0);
    _growPadding->setToolTip(tr("Space left below the text of the growing cell."));
    blockForm->addRow(tr("Position:"), _anchor);
    blockForm->addRow(tr("Width:"), _blockWidth);
    blockForm->addRow(tr("Height:"), _blockHeight);
    blockForm->addRow(tr("Largest height:"), _blockMaxHeight);
    blockForm->addRow(tr("Space under growing text:"), _growPadding);
    form->addRow(block);

    auto* note = new QLabel(tr("Cell positions are measured from the title block, so a larger or smaller block or page may need the cells moved."), page);
    note->setWordWrap(true);
    form->addRow(note);

    for (QDoubleSpinBox* s : { _customWidth, _customHeight, _borderInset, _margin, _areaGap, _lineWidth, _blockWidth, _blockHeight, _blockMaxHeight, _growPadding })
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]() { applyPage(); });
    for (QComboBox* c : { _pageSize, _orientation, _anchor })
        connect(c, &QComboBox::currentIndexChanged, this, [this]() { applyPage(); });
    _tabs->addTab(page, tr("Page"));
}

void SheetTemplateEditor::loadPage()
{
    _loading = true;
    const int sizeIndex = _pageSize->findData(static_cast<int>(_tpl.pageSizeId));
    _pageSize->setCurrentIndex(sizeIndex >= 0 ? sizeIndex : _pageSize->count() - 1);
    _orientation->setCurrentIndex(_tpl.landscape ? 0 : 1);
    // a custom page shows its own size; a named page shows its size for reference (the boxes are read-only then)
    const QSizeF shownSize = _tpl.pageSizeId == QPageSize::Custom ? _tpl.customSizeMm : QPageSize(_tpl.pageSizeId).size(QPageSize::Millimeter);
    _customWidth->setValue(shownSize.width());
    _customHeight->setValue(shownSize.height());
    _borderInset->setValue(_tpl.borderInset / kPtPerMm);
    _margin->setValue(_tpl.margin / kPtPerMm);
    _areaGap->setValue(_tpl.areaGap / kPtPerMm);
    _lineWidth->setValue(_tpl.lineWidth);
    _anchor->setCurrentIndex(std::max(0, _anchor->findData(static_cast<int>(_tpl.anchor))));
    _blockWidth->setValue(_tpl.blockWidth / kPtPerMm);
    _blockHeight->setValue(_tpl.blockHeight / kPtPerMm);
    _blockMaxHeight->setValue(_tpl.blockMaxHeight / kPtPerMm);
    _growPadding->setValue(_tpl.growPadding / kPtPerMm);
    const bool custom = _pageSize->currentData().toInt() == static_cast<int>(QPageSize::Custom);
    _customWidth->setEnabled(custom);
    _customHeight->setEnabled(custom);
    _blockWidth->setEnabled(_tpl.anchor != TitleBlockAnchor::Bottom);
    _loading = false;
}

void SheetTemplateEditor::applyPage()
{
    if (_loading)
        return;
    const SheetTemplate before = _tpl;
    _tpl.pageSizeId = static_cast<QPageSize::PageSizeId>(_pageSize->currentData().toInt());
    if (_tpl.pageSizeId == QPageSize::Custom && before.pageSizeId != QPageSize::Custom)
    {
        // switching to Custom: start from the size the page had, so the page does not jump to the smallest custom size
        const QSizeF current = QPageSize(before.pageSizeId).size(QPageSize::Millimeter);
        const QSignalBlocker widthBlocker(_customWidth);
        const QSignalBlocker heightBlocker(_customHeight);
        _customWidth->setValue(current.width());
        _customHeight->setValue(current.height());
    }
    if (_tpl.pageSizeId == QPageSize::Custom)
    {
        _tpl.customSizeMm = QSizeF(_customWidth->value(), _customHeight->value());
    }
    else
    {
        // the boxes follow the chosen size, for reference
        const QSizeF named = QPageSize(_tpl.pageSizeId).size(QPageSize::Millimeter);
        const QSignalBlocker widthBlocker(_customWidth);
        const QSignalBlocker heightBlocker(_customHeight);
        _customWidth->setValue(named.width());
        _customHeight->setValue(named.height());
    }
    _tpl.landscape = _orientation->currentData().toBool();
    _tpl.borderInset = _borderInset->value() * kPtPerMm;
    _tpl.margin = _margin->value() * kPtPerMm;
    _tpl.areaGap = _areaGap->value() * kPtPerMm;
    _tpl.lineWidth = _lineWidth->value();
    _tpl.anchor = static_cast<TitleBlockAnchor>(_anchor->currentData().toInt());
    _tpl.blockWidth = _tpl.anchor == TitleBlockAnchor::Bottom ? 0.0 : _blockWidth->value() * kPtPerMm;
    if (_tpl.anchor != TitleBlockAnchor::Bottom && _tpl.blockWidth <= 0.0)
        _tpl.blockWidth = 100.0 * kPtPerMm;   // a corner block needs a width of its own
    _tpl.blockHeight = _blockHeight->value() * kPtPerMm;
    _tpl.blockMaxHeight = std::max(_blockMaxHeight->value() * kPtPerMm, _tpl.blockHeight);   // the largest height is never below the height
    _tpl.growPadding = _growPadding->value() * kPtPerMm;
    {
        const bool custom = _tpl.pageSizeId == QPageSize::Custom;
        _customWidth->setEnabled(custom);
        _customHeight->setEnabled(custom);
        _blockWidth->setEnabled(_tpl.anchor != TitleBlockAnchor::Bottom);
        if (_blockMaxHeight->value() * kPtPerMm < _tpl.blockHeight)
        {
            const QSignalBlocker blocker(_blockMaxHeight);
            _blockMaxHeight->setValue(_tpl.blockHeight / kPtPerMm);
        }
    }
    adaptCellsToPage(before);
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
    fillList(_tpl.cells.isEmpty() ? -1 : std::max(0, _cellList->currentRow()));
    loadLogo();
}

// ---- the Logo tab -----------------------------------------------------------------------------------------------------------------

void SheetTemplateEditor::buildLogoTab()
{
    auto* page = new QWidget(_tabs);
    auto* form = new QFormLayout(page);
    _logoFileLabel = new QLabel(page);
    _logoFileLabel->setWordWrap(true);
    _logoChoose = new QPushButton(tr("Choose logo..."), page);
    _logoChoose->setToolTip(tr("A PNG, JPEG or SVG file. A template without a logo gets a logo cell at the left of the title block."));
    _logoRemove = new QPushButton(tr("Remove logo"), page);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(_logoChoose);
    buttonRow->addWidget(_logoRemove);
    buttonRow->addStretch(1);
    form->addRow(tr("Logo:"), _logoFileLabel);
    form->addRow(QString(), buttonRow);

    const QString edgeTip = tr("Distance from the left or top edge of the title block, in millimetres or as a percentage of the block.\n"
                               "A negative number is measured from the right or bottom edge instead.");
    _logoLeft = new SheetEdgeBox(page);
    _logoTop = new SheetEdgeBox(page);
    _logoRight = new SheetEdgeBox(page);
    _logoBottom = new SheetEdgeBox(page);
    for (SheetEdgeBox* e : { _logoLeft, _logoTop, _logoRight, _logoBottom })
        e->setToolTip(edgeTip);
    auto* positionGrid = new QGridLayout;
    positionGrid->addWidget(new QLabel(tr("Left"), page), 0, 0);
    positionGrid->addWidget(_logoLeft, 0, 1);
    positionGrid->addWidget(new QLabel(tr("Top"), page), 0, 2);
    positionGrid->addWidget(_logoTop, 0, 3);
    positionGrid->addWidget(new QLabel(tr("Right"), page), 1, 0);
    positionGrid->addWidget(_logoRight, 1, 1);
    positionGrid->addWidget(new QLabel(tr("Bottom"), page), 1, 2);
    positionGrid->addWidget(_logoBottom, 1, 3);
    positionGrid->setColumnStretch(4, 1);
    form->addRow(tr("Position:"), positionGrid);
    _logoContain = new QCheckBox(tr("Keep the proportions of the picture"), page);
    form->addRow(QString(), _logoContain);
    auto* note = new QLabel(tr("The picture is copied next to the template when it is saved."), page);
    note->setWordWrap(true);
    form->addRow(note);

    connect(_logoChoose, &QPushButton::clicked, this, [this]() { chooseLogo(); });
    connect(_logoRemove, &QPushButton::clicked, this, [this]() { removeLogo(); });
    for (SheetEdgeBox* e : { _logoLeft, _logoTop, _logoRight, _logoBottom })
        connect(e, &SheetEdgeBox::edgeChanged, this, [this]() { applyLogoEdits(); });
    connect(_logoContain, &QCheckBox::toggled, this, [this]() { applyLogoEdits(); });
    _tabs->addTab(page, tr("Logo"));
}

void SheetTemplateEditor::loadLogo()
{
    _loading = true;
    const bool has = _tpl.hasLogo;
    _logoFileLabel->setText(has ? QFileInfo(_tpl.logo.file).fileName() : tr("none"));
    _logoRemove->setEnabled(has);
    for (QWidget* w : { static_cast<QWidget*>(_logoLeft), static_cast<QWidget*>(_logoTop), static_cast<QWidget*>(_logoRight),
                        static_cast<QWidget*>(_logoBottom), static_cast<QWidget*>(_logoContain) })
        w->setEnabled(has);
    if (has)
    {
        _logoLeft->setEdge(_tpl.logo.rect.left);
        _logoTop->setEdge(_tpl.logo.rect.top);
        _logoRight->setEdge(_tpl.logo.rect.right);
        _logoBottom->setEdge(_tpl.logo.rect.bottom);
        _logoContain->setChecked(_tpl.logo.contain);
    }
    else
    {
        for (SheetEdgeBox* e : { _logoLeft, _logoTop, _logoRight, _logoBottom })
            e->setEdge(SheetEdge());
    }
    _loading = false;
}

void SheetTemplateEditor::applyLogoEdits()
{
    if (_loading || !_tpl.hasLogo)
        return;
    _tpl.logo.contain = _logoContain->isChecked();
    struct EdgeBox
    {
        SheetEdgeBox* box;
        SheetEdge* target;
    };
    const EdgeBox edges[] = { { _logoLeft, &_tpl.logo.rect.left }, { _logoTop, &_tpl.logo.rect.top },
                              { _logoRight, &_tpl.logo.rect.right }, { _logoBottom, &_tpl.logo.rect.bottom } };
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
    fillList(std::max(0, _cellList->currentRow()));
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

void SheetTemplateEditor::buildFieldsTab()
{
    auto* page = new QWidget(_tabs);
    auto* layout = new QVBoxLayout(page);
    auto* intro = new QLabel(tr("Extra values the Save as 3D PDF dialog asks for. Use {id} in the text of a cell to print one."), page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    _fieldList = new QListWidget(page);
    _fieldList->setMinimumHeight(90);
    layout->addWidget(_fieldList, 1);
    auto* listButtons = new QHBoxLayout;
    _fieldAdd = new QPushButton(tr("Add"), page);
    _fieldDelete = new QPushButton(tr("Delete"), page);
    _fieldUp = new QPushButton(tr("Up"), page);
    _fieldDown = new QPushButton(tr("Down"), page);
    for (QPushButton* b : { _fieldAdd, _fieldDelete, _fieldUp, _fieldDown })
        listButtons->addWidget(b);
    layout->addLayout(listButtons);

    auto* form = new QFormLayout;
    _fieldId = new QLineEdit(page);
    _fieldId->setToolTip(tr("Letters, digits and underscores, starting with a letter. The names title, author, date ... are taken.\n"
                            "\"material\" and \"mass\" are filled from the model; a field with one of these names overrides them when it is not empty."));
    _fieldLabel = new QLineEdit(page);
    _fieldType = new QComboBox(page);
    _fieldType->addItem(tr("Text"), QStringLiteral("text"));
    _fieldType->addItem(tr("Several lines"), QStringLiteral("multiline"));
    _fieldType->addItem(tr("Date"), QStringLiteral("date"));
    _fieldType->addItem(tr("Choice from a list"), QStringLiteral("choice"));
    _fieldType->addItem(tr("Number"), QStringLiteral("number"));
    _fieldDefault = new QLineEdit(page);
    _fieldOptions = new QPlainTextEdit(page);
    _fieldOptions->setPlaceholderText(tr("One choice per line"));
    _fieldOptions->setMaximumHeight(90);
    _fieldDecimals = new QSpinBox(page);
    _fieldDecimals->setRange(0, 6);
    _fieldDecimals->setToolTip(tr("Digits after the decimal separator when the number is printed."));
    _fieldUnit = new QLineEdit(page);
    _fieldUnit->setMaxLength(20);
    _fieldUnit->setToolTip(tr("Printed after the number, for example kg."));
    _fieldMin = new QLineEdit(page);
    _fieldMin->setPlaceholderText(tr("no limit"));
    _fieldMax = new QLineEdit(page);
    _fieldMax->setPlaceholderText(tr("no limit"));
    form->addRow(tr("Id:"), _fieldId);
    form->addRow(tr("Label:"), _fieldLabel);
    form->addRow(tr("Type:"), _fieldType);
    form->addRow(tr("Default value:"), _fieldDefault);
    form->addRow(tr("Choices:"), _fieldOptions);
    auto* numberRow = new QHBoxLayout;
    numberRow->addWidget(new QLabel(tr("Decimals"), page));
    numberRow->addWidget(_fieldDecimals);
    numberRow->addWidget(new QLabel(tr("Unit"), page));
    numberRow->addWidget(_fieldUnit);
    numberRow->addWidget(new QLabel(tr("Min"), page));
    numberRow->addWidget(_fieldMin);
    numberRow->addWidget(new QLabel(tr("Max"), page));
    numberRow->addWidget(_fieldMax);
    form->addRow(tr("Number:"), numberRow);
    layout->addLayout(form);

    connect(_fieldList, &QListWidget::currentRowChanged, this, [this](int row) { loadField(row); });
    connect(_fieldAdd, &QPushButton::clicked, this, [this]() { addField(); });
    connect(_fieldDelete, &QPushButton::clicked, this, [this]() { deleteField(); });
    connect(_fieldUp, &QPushButton::clicked, this, [this]() { moveField(-1); });
    connect(_fieldDown, &QPushButton::clicked, this, [this]() { moveField(1); });
    for (QLineEdit* e : { _fieldId, _fieldLabel, _fieldDefault })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyFieldEdits(); });
    connect(_fieldType, &QComboBox::currentIndexChanged, this, [this]() { applyFieldEdits(); });
    connect(_fieldOptions, &QPlainTextEdit::textChanged, this, [this]() { applyFieldEdits(); });
    connect(_fieldDecimals, &QSpinBox::valueChanged, this, [this]() { applyFieldEdits(); });
    for (QLineEdit* e : { _fieldUnit, _fieldMin, _fieldMax })
        connect(e, &QLineEdit::textChanged, this, [this]() { applyFieldEdits(); });
    _tabs->addTab(page, tr("Fields"));
}

void SheetTemplateEditor::fillFieldList(int selectRow)
{
    const QSignalBlocker blocker(_fieldList);
    _fieldList->clear();
    for (const SheetField& f : std::as_const(_tpl.fields))
        _fieldList->addItem(QStringLiteral("{%1}  %2").arg(f.id, f.label));
    const int row = (selectRow >= 0 && selectRow < _tpl.fields.size()) ? selectRow : -1;
    _fieldList->setCurrentRow(row);
    loadField(row);
}

void SheetTemplateEditor::loadField(int row)
{
    const bool have = row >= 0 && row < _tpl.fields.size();
    _loading = true;
    for (QWidget* w : { static_cast<QWidget*>(_fieldId), static_cast<QWidget*>(_fieldLabel), static_cast<QWidget*>(_fieldType),
                        static_cast<QWidget*>(_fieldDefault), static_cast<QWidget*>(_fieldOptions) })
        w->setEnabled(have);
    if (have)
    {
        const SheetField& f = _tpl.fields[row];
        _fieldId->setText(f.id);
        _fieldLabel->setText(f.label);
        _fieldType->setCurrentIndex(std::max(0, _fieldType->findData(f.type)));
        _fieldDefault->setText(f.defaultValue);
        _fieldOptions->setPlainText(f.options.join(QLatin1Char('\n')));
        _fieldDecimals->setValue(f.decimals);
        _fieldUnit->setText(f.unit);
        _fieldMin->setText(f.minimum == -1e9 ? QString() : _locale.toString(f.minimum, 'g', 10));
        _fieldMax->setText(f.maximum == 1e9 ? QString() : _locale.toString(f.maximum, 'g', 10));
    }
    else
    {
        _fieldId->clear();
        _fieldLabel->clear();
        _fieldDefault->clear();
        _fieldOptions->clear();
        _fieldUnit->clear();
        _fieldMin->clear();
        _fieldMax->clear();
    }
    const QString loadedType = _fieldType->currentData().toString();
    _fieldOptions->setEnabled(have && loadedType == QLatin1String("choice"));
    for (QWidget* w : { static_cast<QWidget*>(_fieldDecimals), static_cast<QWidget*>(_fieldUnit), static_cast<QWidget*>(_fieldMin), static_cast<QWidget*>(_fieldMax) })
        w->setEnabled(have && loadedType == QLatin1String("number"));
    _loading = false;
    _fieldAdd->setEnabled(_tpl.fields.size() < 16);
    _fieldDelete->setEnabled(have);
    _fieldUp->setEnabled(have && row > 0);
    _fieldDown->setEnabled(have && row + 1 < _tpl.fields.size());
}

void SheetTemplateEditor::applyFieldEdits()
{
    const int row = _fieldList->currentRow();
    if (_loading || row < 0 || row >= _tpl.fields.size())
        return;
    SheetField& f = _tpl.fields[row];
    f.id = _fieldId->text().trimmed();
    f.label = _fieldLabel->text();
    f.type = _fieldType->currentData().toString();
    f.defaultValue = _fieldDefault->text();
    f.options.clear();
    if (f.type == QLatin1String("choice"))
    {
        for (const QString& line : _fieldOptions->toPlainText().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                f.options.append(line.trimmed());
    }
    _fieldOptions->setEnabled(f.type == QLatin1String("choice"));
    QString numberError;
    const bool isNumber = f.type == QLatin1String("number");
    for (QWidget* w : { static_cast<QWidget*>(_fieldDecimals), static_cast<QWidget*>(_fieldUnit), static_cast<QWidget*>(_fieldMin), static_cast<QWidget*>(_fieldMax) })
        w->setEnabled(isNumber);
    if (isNumber)
    {
        f.decimals = _fieldDecimals->value();
        f.unit = _fieldUnit->text().trimmed();
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
        limit(_fieldMin, &f.minimum, -1e9, tr("Minimum"));
        limit(_fieldMax, &f.maximum, 1e9, tr("Maximum"));
    }
    else
    {
        f.decimals = 2;
        f.unit.clear();
        f.minimum = -1e9;
        f.maximum = 1e9;
    }
    if (QListWidgetItem* item = _fieldList->item(row))
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
    const int row = _fieldList->currentRow();
    const int at = (row >= 0 && row < _tpl.fields.size()) ? row + 1 : _tpl.fields.size();
    _tpl.fields.insert(at, f);
    fillFieldList(at);
    refreshInsertCombo();
    revalidate();
}

void SheetTemplateEditor::deleteField()
{
    const int row = _fieldList->currentRow();
    if (row < 0 || row >= _tpl.fields.size())
        return;
    _tpl.fields.removeAt(row);
    fillFieldList(std::min(row, static_cast<int>(_tpl.fields.size()) - 1));
    refreshInsertCombo();
    revalidate();
}

void SheetTemplateEditor::moveField(int delta)
{
    const int row = _fieldList->currentRow();
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
    const QSignalBlocker blocker(_insertCombo);
    _insertCombo->clear();
    _insertCombo->addItem(tr("Insert field..."));
    QStringList names = standardSheetPlaceholders();
    names += sceneSheetPlaceholders();
    for (const SheetField& field : std::as_const(_tpl.fields))
        if (!field.id.isEmpty() && !names.contains(field.id))
            names << field.id;
    for (const QString& n : names)
        _insertCombo->addItem(QStringLiteral("{%1}").arg(n), n);
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
