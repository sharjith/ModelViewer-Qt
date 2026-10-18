#pragma once
#include <QDialog>
#include <QString>

class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;

#include "Material.h"

class ChannelPackingEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ChannelPackingEditorDialog(QWidget* parent = nullptr);

    // Load current state into the dialog
    void setCurrentPacking(const Material::ChannelPacking& p, const QString& mapDisplayName);

    // Read back the edited state (call after exec()==Accepted)
    Material::ChannelPacking packing() const;

private:
    QLabel* _titleLabel = nullptr;
    QComboBox* _channelCombo = nullptr;
    QCheckBox* _invertCheck = nullptr;
    QDoubleSpinBox* _scaleSpin = nullptr;
    QDoubleSpinBox* _biasSpin = nullptr;
    QPushButton* _ok = nullptr;
    QPushButton* _cancel = nullptr;
};
