#pragma once

#include "EditUserLightCommand.h"
#include "GltfLightData.h"

#include <QWidget>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

// ---------------------------------------------------------------------------
// SceneLightPropertiesPanel
//
// The numeric editor shown under the lights tree for the selected light: name, position, aim direction, colour, intensity, range and
// the cone angles of a spot. Rows that do not apply to the light's type are hidden (a point light has no aim or cone, a sun has no
// position or range). Scene lights are editable; imported lights are shown read-only, with the values stored for them.
//
// The panel only reports edits (edited()): it holds no scene state. The owner applies them through an undo command and then shows the
// light again with setLight().
// ---------------------------------------------------------------------------
class SceneLightPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SceneLightPropertiesPanel(QWidget* parent = nullptr);

    // Show `entry`; `editable` is false for imported lights. Does not emit edited().
    void setLight(const GltfLightEntry& entry, bool editable);

    // Re-applies every translated string after a UI language change.
    void retranslateUI();

signals:
    // The user changed `field`; `entry` is the whole light with the change applied.
    void edited(const GltfLightEntry& entry, EditUserLightCommand::Field field);
    // The user asked for the light to be aimed at the centre of the scene.
    void aimAtCentreRequested();

private:
    void emitEdit(EditUserLightCommand::Field field);
    void onColorClicked();
    void updateColorButton();
    void updateRowVisibility();

    QFormLayout*    _form = nullptr;
    QLabel*         _titleLabel = nullptr;
    QLineEdit*      _nameEdit = nullptr;
    QLabel*         _typeLabel = nullptr;
    QDoubleSpinBox* _pos[3] = {};
    QDoubleSpinBox* _dir[3] = {};
    QPushButton*    _aimButton = nullptr;
    QPushButton*    _colorButton = nullptr;
    QDoubleSpinBox* _intensity = nullptr;
    QDoubleSpinBox* _range = nullptr;
    QDoubleSpinBox* _innerCone = nullptr;
    QDoubleSpinBox* _outerCone = nullptr;

    // Rows, kept to show or hide per light type.
    QWidget* _positionRow = nullptr;
    QWidget* _directionRow = nullptr;

    GltfLightEntry _entry;      // the light as last shown or edited
    bool           _editable = false;
    bool           _updating = false;   // true while setLight() fills the widgets: no edits are reported
    bool           _innerConeEdited = false;   // which cone box triggered the current cone edit
};
