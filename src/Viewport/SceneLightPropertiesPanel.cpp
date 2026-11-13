#include "SceneLightPropertiesPanel.h"

#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265f;

QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, int decimals, double step)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setKeyboardTracking(false);   // report an edit when it is finished (Enter, focus out, arrows), not per keystroke
    box->setAlignment(Qt::AlignRight);
    return box;
}

// Set a spin box without reporting a change, and without disturbing the text when the value is already right.
void showValue(QDoubleSpinBox* box, double value)
{
    if (std::abs(box->value() - value) < std::pow(10.0, -box->decimals()) * 0.5)
        return;
    const QSignalBlocker blocker(box);
    box->setValue(value);
}

QWidget* makeTriple(QWidget* parent, QDoubleSpinBox* (&boxes)[3], double min, double max, int decimals, double step)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    for (int i = 0; i < 3; ++i)
    {
        boxes[i] = makeSpin(row, min, max, decimals, step);
        layout->addWidget(boxes[i]);
    }
    return row;
}
}

SceneLightPropertiesPanel::SceneLightPropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    _form = new QFormLayout(this);
    _form->setContentsMargins(0, 4, 0, 0);

    _titleLabel = new QLabel(this);
    QFont titleFont = _titleLabel->font();
    titleFont.setBold(true);
    _titleLabel->setFont(titleFont);
    _form->addRow(_titleLabel);

    _nameEdit = new QLineEdit(this);
    _typeLabel = new QLabel(this);
    _positionRow = makeTriple(this, _pos, -1.0e7, 1.0e7, 4, 0.1);
    _directionRow = new QWidget(this);
    {
        auto* column = new QHBoxLayout(_directionRow);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        for (int i = 0; i < 3; ++i)
        {
            _dir[i] = makeSpin(_directionRow, -1.0e3, 1.0e3, 4, 0.05);
            column->addWidget(_dir[i]);
        }
    }
    _aimButton = new QPushButton(this);
    _colorButton = new QPushButton(this);
    _colorButton->setMinimumHeight(22);
    _intensity = makeSpin(this, 0.0, 1.0e9, 3, 0.1);
    _intensity->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    _range = makeSpin(this, 0.0, 1.0e9, 3, 1.0);
    _range->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    _innerCone = makeSpin(this, 0.0, 89.9, 1, 1.0);
    _outerCone = makeSpin(this, 0.1, 90.0, 1, 1.0);
    _innerCone->setSuffix(QStringLiteral(" °"));
    _outerCone->setSuffix(QStringLiteral(" °"));

    _form->addRow(tr("Name"), _nameEdit);
    _form->addRow(tr("Type"), _typeLabel);
    _form->addRow(tr("Position"), _positionRow);
    _form->addRow(tr("Direction"), _directionRow);
    _form->addRow(QString(), _aimButton);
    _form->addRow(tr("Colour"), _colorButton);
    _form->addRow(tr("Intensity"), _intensity);
    _form->addRow(tr("Range"), _range);
    _form->addRow(tr("Inner cone"), _innerCone);
    _form->addRow(tr("Outer cone"), _outerCone);

    retranslateUI();

    connect(_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (_updating || !_editable || _nameEdit->text() == _entry.name)
            return;
        _entry.name = _nameEdit->text();
        emit edited(_entry, EditUserLightCommand::Field::Name);
    });
    for (int i = 0; i < 3; ++i)
    {
        connect(_pos[i], &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(EditUserLightCommand::Field::Position); });
        connect(_dir[i], &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(EditUserLightCommand::Field::Direction); });
    }
    connect(_aimButton, &QPushButton::clicked, this, &SceneLightPropertiesPanel::aimAtCentreRequested);
    connect(_colorButton, &QPushButton::clicked, this, &SceneLightPropertiesPanel::onColorClicked);
    connect(_intensity, &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(EditUserLightCommand::Field::Intensity); });
    connect(_range, &QDoubleSpinBox::valueChanged, this, [this] { emitEdit(EditUserLightCommand::Field::Range); });
    connect(_innerCone, &QDoubleSpinBox::valueChanged, this, [this] { _innerConeEdited = true; emitEdit(EditUserLightCommand::Field::Cone); });
    connect(_outerCone, &QDoubleSpinBox::valueChanged, this, [this] { _innerConeEdited = false; emitEdit(EditUserLightCommand::Field::Cone); });
}

void SceneLightPropertiesPanel::retranslateUI()
{
    _aimButton->setText(tr("Aim at Scene Centre"));
    _aimButton->setToolTip(tr("Point the light at the centre of the scene"));
    _intensity->setToolTip(tr("Intensity as stored in glTF (candela for point and spot lights, lux for a sun)"));
    _range->setSpecialValueText(tr("Unlimited"));
    _range->setToolTip(tr("Distance at which the light fades out completely; 0 means unlimited"));
    _titleLabel->setText(_editable ? tr("Scene Light") : tr("Imported Light (read-only)"));
}

void SceneLightPropertiesPanel::setLight(const GltfLightEntry& entry, bool editable)
{
    _updating = true;
    _entry = entry;
    _editable = editable;

    const GPULight& light = entry.gpuLight;
    const int type = light.type;

    if (!_nameEdit->hasFocus() || _nameEdit->text() != entry.name)
        _nameEdit->setText(entry.name);
    _typeLabel->setText(type == static_cast<int>(LightType::Directional) ? tr("Sun (directional)")
                      : type == static_cast<int>(LightType::Point)       ? tr("Point")
                                                                         : tr("Spot"));
    showValue(_pos[0], light.position.x);
    showValue(_pos[1], light.position.y);
    showValue(_pos[2], light.position.z);
    showValue(_dir[0], light.direction.x);
    showValue(_dir[1], light.direction.y);
    showValue(_dir[2], light.direction.z);
    showValue(_intensity, light.intensity);
    showValue(_range, light.range);
    showValue(_innerCone, std::acos(std::clamp(light.innerConeCos, -1.0f, 1.0f)) * kRadToDeg);
    showValue(_outerCone, std::acos(std::clamp(light.outerConeCos, -1.0f, 1.0f)) * kRadToDeg);
    updateColorButton();

    const QList<QWidget*> inputs = { _nameEdit, _positionRow, _directionRow, _aimButton, _colorButton, _intensity, _range, _innerCone, _outerCone };
    for (QWidget* input : inputs)
        input->setEnabled(editable);

    _titleLabel->setText(editable ? tr("Scene Light") : tr("Imported Light (read-only)"));
    updateRowVisibility();
    _updating = false;
}

void SceneLightPropertiesPanel::updateRowVisibility()
{
    const int type = _entry.gpuLight.type;
    const bool isSun = type == static_cast<int>(LightType::Directional);
    const bool isSpot = type == static_cast<int>(LightType::Spot);
    _form->setRowVisible(_positionRow, !isSun);
    _form->setRowVisible(_directionRow, isSun || isSpot);
    _form->setRowVisible(_aimButton, isSun || isSpot);
    _form->setRowVisible(_range, !isSun);
    _form->setRowVisible(_innerCone, isSpot);
    _form->setRowVisible(_outerCone, isSpot);
}

void SceneLightPropertiesPanel::updateColorButton()
{
    const GPULight& light = _entry.gpuLight;
    const QColor color = QColor::fromRgbF(std::clamp(light.color.x, 0.0f, 1.0f), std::clamp(light.color.y, 0.0f, 1.0f),
                                          std::clamp(light.color.z, 0.0f, 1.0f));
    _colorButton->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid gray;").arg(color.name()));
    _colorButton->setText(QString());
    _colorButton->setToolTip(tr("Light colour"));
}

void SceneLightPropertiesPanel::onColorClicked()
{
    if (!_editable)
        return;
    const GPULight& light = _entry.gpuLight;
    const QColor start = QColor::fromRgbF(std::clamp(light.color.x, 0.0f, 1.0f), std::clamp(light.color.y, 0.0f, 1.0f),
                                          std::clamp(light.color.z, 0.0f, 1.0f));
    const QColor chosen = QColorDialog::getColor(start, this, tr("Light Colour"));
    if (!chosen.isValid())
        return;
    _entry.gpuLight.color = glm::vec3(static_cast<float>(chosen.redF()), static_cast<float>(chosen.greenF()),
                                      static_cast<float>(chosen.blueF()));
    updateColorButton();
    emit edited(_entry, EditUserLightCommand::Field::Color);
}

void SceneLightPropertiesPanel::emitEdit(EditUserLightCommand::Field field)
{
    if (_updating || !_editable)
        return;

    GPULight& light = _entry.gpuLight;
    switch (field)
    {
    case EditUserLightCommand::Field::Position:
        light.position = glm::vec3(static_cast<float>(_pos[0]->value()), static_cast<float>(_pos[1]->value()),
                                   static_cast<float>(_pos[2]->value()));
        break;
    case EditUserLightCommand::Field::Direction:
    {
        glm::vec3 direction(static_cast<float>(_dir[0]->value()), static_cast<float>(_dir[1]->value()),
                            static_cast<float>(_dir[2]->value()));
        const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length < 1e-6f)
            return;   // a zero vector has no direction: keep the old aim
        light.direction = direction / length;
        break;
    }
    case EditUserLightCommand::Field::Intensity:
        light.intensity = static_cast<float>(_intensity->value());
        break;
    case EditUserLightCommand::Field::Range:
        light.range = static_cast<float>(_range->value());
        break;
    case EditUserLightCommand::Field::Cone:
    {
        // The inner angle never exceeds the outer one: whichever box was changed last wins.
        double inner = _innerCone->value();
        double outer = _outerCone->value();
        if (inner > outer)
        {
            if (_innerConeEdited)
                inner = outer;
            else
                outer = inner;
        }
        light.innerConeCos = std::cos(static_cast<float>(inner) * kDegToRad);
        light.outerConeCos = std::cos(static_cast<float>(outer) * kDegToRad);
        break;
    }
    default:
        return;
    }
    emit edited(_entry, field);
}
