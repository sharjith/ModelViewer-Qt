#pragma once

#include "SheetTemplate.h"

#include <QDialog>
#include <QLocale>
#include <QMap>
#include <QString>
#include <QTemporaryDir>

#include <memory>

class QComboBox;
class QDoubleSpinBox;

namespace Ui
{
class SheetTemplateEditor;
}

namespace Pdf3D
{

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
// under a new name. Layout: ui/Pdf3D/SheetTemplateEditor.ui. Design: research/3dpdf/3D_PDF_EXPORT_RESEARCH.md, section 11.
class SheetTemplateEditor : public QDialog
{
    Q_OBJECT

public:
    // `sampleValues` fills the placeholders in the preview (what the export dialog would print now).
    SheetTemplateEditor(const SheetTemplate& tpl, const QMap<QString, QString>& sampleValues, const QString& folder, const QLocale& locale,
                        QWidget* parent = nullptr);
    ~SheetTemplateEditor() override;

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

    // the other tabs
    void setupCellsTab();
    void setupPageTab();
    void setupLogoTab();
    void setupFieldsTab();
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
    void addCellForField();   // a cell that prints the selected field, below the selected cell, for the owner to drag into place
    void deleteField();
    void moveField(int delta);
    void refreshInsertCombo();
    QMap<QString, QString> previewValues() const;

    std::unique_ptr<Ui::SheetTemplateEditor> ui;

    SheetTemplate _tpl;     // the template being edited
    SheetTemplate _valid;   // the last state of it that passed the check; what the preview draws
    QMap<QString, QString> _values;
    QString _folder;
    QLocale _locale;        // the language the numbers of the preview and of the number settings are written in
    QString _savedId;
    bool _isUser = false;
    bool _loading = false;  // the property widgets are being filled: their change signals are ignored
    bool _cellsEdited = false;   // the user changed a cell position (or added or deleted a cell) since the editor opened or the cells were last adapted
    QTemporaryDir _scratch;   // a logo chosen in the editor is copied here first, into the template's folder only when saved



};

} // namespace Pdf3D
