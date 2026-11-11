#pragma once

#include "PrintPage.h"
#include "SheetFieldEditors.h"
#include "SheetTemplate.h"

#include <QDialog>
#include <QPageLayout>

class QCheckBox;
class QComboBox;
class QImage;
class QLineEdit;
class QPrinter;

namespace Pdf3D
{
struct PartSummary;
}

// File > Print...: what goes on the page. The sheet template (the same list as Save as 3D PDF: built-in and the user's), the title block
// fields it prints, and the paper layout. A physical print job then opens the system printer dialog.
class PrintDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Output { Printer, Pdf, Svg };

    // `declaredNumber`: the part number the model declares; it fills Drawing no. when there is one; otherwise the field is empty.
    PrintDialog(QPrinter& printer, const QImage& view, const Pdf3D::PartSummary& parts,
                const QString& defaultTitle, const QString& declaredNumber, const QSizeF& orthographicSizeMm,
                bool openAfterSaving, QWidget* parent = nullptr, const QVector<PrintPage::Sheet>& sheets = {});

    bool useTemplate() const;
    int imageDpi() const;
    QSizeF modelSizeMm() const;
    bool openAfterSaving() const;
    Output output() const { return _output; }
    Pdf3D::Pdf3DOptions options(QPageLayout::Orientation orientation) const;

private:
    const Pdf3D::SheetTemplate& primaryTemplate() const;
    void updateEnabled();
    void saveSettings() const;

    QVector<Pdf3D::SheetTemplate> _templates;
    Pdf3D::SheetFieldEditors _customFields;
    QString _sceneMaterial;
    QString _sceneMass;
    QSizeF _orthographicSizeMm;
    bool _paperOverridden = false;
    Output _output = Output::Printer;
    QCheckBox* _useTemplate = nullptr;
    QCheckBox* _openAfterSaving = nullptr;
    QComboBox* _template = nullptr;
    QComboBox* _imageDpi = nullptr;
    QLineEdit* _title = nullptr;
    QLineEdit* _drawingNumber = nullptr;
    QLineEdit* _revision = nullptr;
    QLineEdit* _author = nullptr;
    QLineEdit* _company = nullptr;
    QComboBox* _drawingScale = nullptr;
    QLineEdit* _notes = nullptr;
};
