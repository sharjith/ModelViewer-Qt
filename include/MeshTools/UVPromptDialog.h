#pragma once

#include <QDialog>
#include <QCheckBox>

#include "SceneMesh.h"

class QLabel;
class QRadioButton;
class QPushButton;
class QCheckBox;
class QButtonGroup;

struct SceneUVPromptInfo
{
    QString fileName;
    int meshCount;
    int totalVertices;
    int totalTriangles;
    QString largestMeshName;
    int largestTriangleCount;
};

class UVPromptDialog : public QDialog
{
    Q_OBJECT

public:
    enum Choice { None, Planar, Cylindrical, Spherical, Angular, Hybrid, Smart, SmartProject };

    UVPromptDialog(QWidget* parent = nullptr);
    Choice selectedChoice() const;
    bool rememberChoiceChecked() const { return _rememberChoice->isChecked(); }

private:
    QButtonGroup* _buttonGroup;
    QRadioButton* _planarButton;
    QRadioButton* _cylindricalButton;
    QRadioButton* _sphericalButton;
    QRadioButton* _angleBasedButton;
    QRadioButton* _hybridButton;
    QRadioButton* _smartButton;
    QRadioButton* _smartProjectButton;
    QCheckBox* _rememberChoice;
    QPushButton* _okButton;
    QPushButton* _cancelButton;
    Choice _choice = None;
};

