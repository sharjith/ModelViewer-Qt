#pragma once

#include "Pdf3DExporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

// File > Print...: what goes on the page. The sheet template (the same list as Save as 3D PDF: built-in and the user's), the title block
// fields it prints, and a resolution. The printer itself is chosen next, in the system's print dialog. The choices are remembered.
class PrintDialog : public QDialog
{
    Q_OBJECT
public:
    // `declaredNumber`: the part number the model declares; it fills Drawing no. when there is one; otherwise the field is empty.
    explicit PrintDialog(const QString& defaultTitle, const QString& declaredNumber, QWidget* parent = nullptr);

    bool useTemplate() const;
    int dpi() const;
    Pdf3D::Pdf3DOptions options() const;   // template id and the title block fields

private:
    void updateEnabled();
    void saveSettings() const;

    QCheckBox* _useTemplate = nullptr;
    QComboBox* _template = nullptr;
    QComboBox* _quality = nullptr;
    QLineEdit* _title = nullptr;
    QLineEdit* _drawingNumber = nullptr;
    QLineEdit* _revision = nullptr;
    QLineEdit* _author = nullptr;
    QLineEdit* _company = nullptr;
    QLineEdit* _scale = nullptr;
    QLineEdit* _notes = nullptr;
};
