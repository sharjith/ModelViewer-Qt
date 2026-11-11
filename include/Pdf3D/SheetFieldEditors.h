#pragma once

#include <QLocale>
#include <QMap>
#include <QString>

#include <functional>

class QFormLayout;
class QWidget;

namespace Pdf3D
{

struct SheetTemplate;

QLocale sheetFieldLocale();

// The typed custom fields and shared per-template defaults for the print and 3D PDF dialogs.
class SheetFieldEditors
{
public:
    void rebuild(QFormLayout& form, const SheetTemplate& tpl, QWidget* context, const std::function<void()>& changed);
    QMap<QString, QString> values() const;
    void save() const;

private:
    QString _templateId;
    QMap<QString, QWidget*> _editors;   // owned by the form
};

} // namespace Pdf3D
