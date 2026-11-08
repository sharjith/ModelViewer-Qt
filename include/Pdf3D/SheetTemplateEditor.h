#pragma once

#include "SheetTemplate.h"

#include <QDialog>
#include <QLocale>
#include <QMap>
#include <QString>
#include <QTemporaryDir>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace Pdf3D
{

class SheetPreviewWidget;

// One coordinate of a cell or of the logo: a spin box and a unit switch, millimetres or a percentage of the block. A negative number is
// measured from the opposite edge (the right or bottom of the title block).
class SheetEdgeBox : public QWidget
{
    Q_OBJECT

public:
    explicit SheetEdgeBox(QWidget* parent = nullptr);
    void setEdge(const SheetEdge& edge);   // shows the edge without reporting a change
    SheetEdge edge() const;

signals:
    void edgeChanged();

private:
    QDoubleSpinBox* _value = nullptr;
    QComboBox* _unit = nullptr;
};

// Edits the cells of a sheet template with a live preview of the sheet (the same drawing the exporter uses): a list of the title block
// cells, the properties of the selected one (label, text with {placeholders}, font size, bold, colour, alignment, wrapping, position)
// and Add / Duplicate / Delete / Move. Clicking a cell in the preview selects it in the list. The template is checked after every
// edit the way saving checks it; while it is not valid the reason is shown and Save is off. A built-in template can only be saved
// under a new name. Pure code, no .ui file. Design: research/3dpdf/3D_PDF_EXPORT_RESEARCH.md, section 11.
class SheetTemplateEditor : public QDialog
{
    Q_OBJECT

public:
    // `sampleValues` fills the placeholders in the preview (what the export dialog would print now).
    SheetTemplateEditor(const SheetTemplate& tpl, const QMap<QString, QString>& sampleValues, const QString& folder, const QLocale& locale,
                        QWidget* parent = nullptr);

    // The id of the template the user saved (the edited one, or the new copy); empty when the dialog was cancelled.
    QString savedTemplateId() const { return _savedId; }

private:
    void buildUi();
    void fillList(int selectRow);
    QString listText(const SheetCell& cell) const;
    void loadCell(int row);
    void applyCellEdits();
    void revalidate(const QString& extraError = QString());
    void addCell();
    void moveCellTo(int index, const QRectF& blockRect, const QSizeF& blockSize);
    void duplicateCell();
    void deleteCell();
    void moveCell(int delta);
    void chooseColour();
    void insertPlaceholder(int comboIndex);
    void updateColourButton();
    void save();
    void saveAs();

    // the Page, Logo and Fields tabs
    void buildPageTab();
    void buildLogoTab();
    void buildFieldsTab();
    void loadPage();
    void applyPage();
    void adaptCellsToPage(const SheetTemplate& before);
    void refreshCellViews();
    void loadLogo();
    void applyLogoEdits();
    void chooseLogo();
    void removeLogo();
    bool ensureLogoInFolder(QString* error);
    void fillFieldList(int selectRow);
    void loadField(int row);
    void applyFieldEdits();
    void addField();
    void deleteField();
    void moveField(int delta);
    void refreshInsertCombo();
    QMap<QString, QString> previewValues() const;

    SheetTemplate _tpl;     // the template being edited
    SheetTemplate _valid;   // the last state of it that passed the check; what the preview draws
    QMap<QString, QString> _values;
    QString _folder;
    QLocale _locale;        // the language the numbers of the preview and of the number settings are written in
    QString _savedId;
    bool _isUser = false;
    bool _loading = false;  // the property widgets are being filled: their change signals are ignored
    bool _cellsEdited = false;   // the user changed a cell position (or added or deleted a cell) since the editor opened or the cells were last adapted

    SheetPreviewWidget* _preview = nullptr;
    QLineEdit* _nameEdit = nullptr;
    QListWidget* _cellList = nullptr;
    QPushButton* _addButton = nullptr;
    QPushButton* _duplicateButton = nullptr;
    QPushButton* _deleteButton = nullptr;
    QPushButton* _upButton = nullptr;
    QPushButton* _downButton = nullptr;
    QLineEdit* _labelEdit = nullptr;
    QLineEdit* _textEdit = nullptr;
    QComboBox* _insertCombo = nullptr;
    QDoubleSpinBox* _fontSize = nullptr;
    QCheckBox* _bold = nullptr;
    QPushButton* _colourButton = nullptr;
    QComboBox* _align = nullptr;
    QCheckBox* _wrap = nullptr;
    QCheckBox* _scroll = nullptr;
    QCheckBox* _grow = nullptr;
    SheetEdgeBox* _leftEdit = nullptr;
    SheetEdgeBox* _topEdit = nullptr;
    SheetEdgeBox* _rightEdit = nullptr;
    SheetEdgeBox* _bottomEdit = nullptr;
    QLabel* _errorLabel = nullptr;
    QPushButton* _saveButton = nullptr;
    QPushButton* _saveAsButton = nullptr;

    QTabWidget* _tabs = nullptr;
    QTemporaryDir _scratch;   // a logo chosen in the editor is copied here first, into the template's folder only when saved

    // Page tab (lengths in millimetres, converted from and to points)
    QComboBox* _pageSize = nullptr;
    QComboBox* _orientation = nullptr;
    QDoubleSpinBox* _customWidth = nullptr;
    QDoubleSpinBox* _customHeight = nullptr;
    QDoubleSpinBox* _borderInset = nullptr;
    QDoubleSpinBox* _margin = nullptr;
    QDoubleSpinBox* _areaGap = nullptr;
    QDoubleSpinBox* _lineWidth = nullptr;
    QComboBox* _anchor = nullptr;
    QDoubleSpinBox* _blockWidth = nullptr;
    QDoubleSpinBox* _blockHeight = nullptr;
    QDoubleSpinBox* _blockMaxHeight = nullptr;
    QDoubleSpinBox* _growPadding = nullptr;

    // Logo tab
    QLabel* _logoFileLabel = nullptr;
    QPushButton* _logoChoose = nullptr;
    QPushButton* _logoRemove = nullptr;
    SheetEdgeBox* _logoLeft = nullptr;
    SheetEdgeBox* _logoTop = nullptr;
    SheetEdgeBox* _logoRight = nullptr;
    SheetEdgeBox* _logoBottom = nullptr;
    QCheckBox* _logoContain = nullptr;

    // Fields tab
    QListWidget* _fieldList = nullptr;
    QPushButton* _fieldAdd = nullptr;
    QPushButton* _fieldDelete = nullptr;
    QPushButton* _fieldUp = nullptr;
    QPushButton* _fieldDown = nullptr;
    QLineEdit* _fieldId = nullptr;
    QLineEdit* _fieldLabel = nullptr;
    QComboBox* _fieldType = nullptr;
    QLineEdit* _fieldDefault = nullptr;
    QPlainTextEdit* _fieldOptions = nullptr;
    QSpinBox* _fieldDecimals = nullptr;
    QLineEdit* _fieldUnit = nullptr;
    QLineEdit* _fieldMin = nullptr;
    QLineEdit* _fieldMax = nullptr;
};

} // namespace Pdf3D
