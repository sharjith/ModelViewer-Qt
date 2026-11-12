#pragma once

#include "PrintPage.h"
#include "SheetFieldEditors.h"
#include "SheetTemplate.h"

#include <QDialog>
#include <QPageLayout>

#include <functional>

class QCheckBox;
class QComboBox;
class QImage;
class QLineEdit;
class QListWidget;
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
    enum class Output { Printer, Pdf, Svg, Pdf3D };

    struct PublishSteps
    {
        QWidget* views = nullptr;
        QListWidget* viewList = nullptr;
        std::function<QVector<Pdf3D::StaticSheet>()> capture;
        QString suggestedPath;
    };

    // `declaredNumber`: the part number the model declares; it fills Drawing no. when there is one; otherwise the field is empty.
    PrintDialog(QPrinter& printer, const QImage& view, const Pdf3D::PartSummary& parts,
                const QString& defaultTitle, const QString& declaredNumber, const QSizeF& orthographicSizeMm,
                bool openAfterSaving, QWidget* parent, PublishSteps publish);

    bool useTemplate() const;
    int imageDpi() const;
    QSizeF modelSizeMm() const;
    bool openAfterSaving() const;
    Output output() const { return _output; }
    void rememberPublishedOutput() const;
    const QString& outputPath() const { return _outputPath; }
    const QVector<Pdf3D::StaticSheet>& sheets() const { return _sheets; }
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
    QString _outputPath;
    QVector<Pdf3D::StaticSheet> _sheets;
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
