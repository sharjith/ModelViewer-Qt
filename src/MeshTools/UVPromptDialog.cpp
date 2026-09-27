#include "UVPromptDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QPushButton>
#include <QStyle>
#include <QButtonGroup>
#include <QGroupBox>

UVPromptDialog::UVPromptDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("UV Auto Generation Options");    
    setWindowIcon(QIcon(":/icons/res/logo.png"));
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* iconLabel = new QLabel(this);
    QIcon infoIcon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
    iconLabel->setPixmap(infoIcon.pixmap(48, 48));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    layout->addWidget(iconLabel);

    QLabel* label = new QLabel(
        QString("<b>Note:</b> Auto generation of UVs may take longer to load the model depending on the size and complexity."
			"<br>Click Ok only if you need to apply textures, or choose Cancel<br>"
			"<b>Tip:</b> If the model has more than 3000 triangles, consider using the Hybrid method for faster UV generation.</b><br><br>"
            "Choose a UV generation method:"));
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    label->setWordWrap(true);
    label->setMinimumWidth(500);
    label->setMaximumWidth(700);
    layout->addWidget(label);

    QGroupBox* uvMethodGroup = new QGroupBox("UV Generation Methods");
    QVBoxLayout* radiolayout = new QVBoxLayout(uvMethodGroup);
        
    _planarButton = new QRadioButton("Planar (Fast, Good for flat surfaces)");
    _planarButton->setToolTip(
        "Uses planar shape detection.\nFast, but less accurate.");
    _cylindricalButton = new QRadioButton("Cylindrical (Fast, only cylinders)");
    _cylindricalButton->setToolTip(
        "Uses cylindrical shape detection.\nFast, but less accurate.");
    _sphericalButton = new QRadioButton("Spherical (Fast, only spheres)");
    _sphericalButton->setToolTip(
        "Uses spherical shape detection.\nFast, but less accurate.");
    _angleBasedButton = new QRadioButton("Angle Based (Fast, detects geometry crudely)");
    _angleBasedButton->setToolTip(
        "Uses basic shape detection based on angular deflection\n(planar, cylindrical, spherical).\nFast, but less accurate.");
    _hybridButton = new QRadioButton("Hybrid (Fast)");
    _hybridButton->setToolTip(
        "Uses basic shape detection (planar, cylindrical, spherical).\nFast, but less accurate.");
    _smartButton = new QRadioButton("Smart (Accurate)");
    _smartButton->setToolTip(
        "Performs angle-based segmentation and PCA projection.\nMore accurate, seam/connectivity-based unwrapping.");
    _smartProjectButton = new QRadioButton("Smart Project (Blender-style)");
    _smartProjectButton->setToolTip(
        "Ports Blender's Smart UV Project: clusters faces by normal similarity\n"
        "(independent of mesh connectivity) and projects each cluster.\nGood for hard-surface/mechanical models.");

    _buttonGroup = new QButtonGroup(this);
    _buttonGroup->addButton(_planarButton, Planar);
    _buttonGroup->addButton(_cylindricalButton, Cylindrical);
    _buttonGroup->addButton(_sphericalButton, Spherical);
    _buttonGroup->addButton(_angleBasedButton, Angular);
    _buttonGroup->addButton(_hybridButton, Hybrid);
    _buttonGroup->addButton(_smartButton, Smart);
    _buttonGroup->addButton(_smartProjectButton, SmartProject);

    radiolayout->addWidget(_planarButton);
    radiolayout->addWidget(_cylindricalButton);
    radiolayout->addWidget(_sphericalButton);
    radiolayout->addWidget(_angleBasedButton);
    radiolayout->addWidget(_hybridButton);
    radiolayout->addWidget(_smartButton);
    radiolayout->addWidget(_smartProjectButton);

    layout->addWidget(uvMethodGroup);

    _smartButton->setChecked(true);

    _rememberChoice = new QCheckBox("Remember Choice");
    layout->addWidget(_rememberChoice);

    QHBoxLayout* buttonsLayout = new QHBoxLayout;
    _okButton = new QPushButton("Generate");
    _cancelButton = new QPushButton("Don't");
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(_okButton);
    buttonsLayout->addWidget(_cancelButton);
    layout->addLayout(buttonsLayout);

    connect(_okButton, &QPushButton::clicked, this, [=] {
        _choice = static_cast<Choice>(_buttonGroup->checkedId());
        accept();
        });
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

UVPromptDialog::Choice UVPromptDialog::selectedChoice() const
{
    return _choice;
}

