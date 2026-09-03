#include "UVGenerationDialog.h"
#include "ui_UVGenerationDialog.h"
#include "LanguageManager.h"
#include <glm/gtc/constants.hpp>

UVGenerationDialog::UVGenerationDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::UVGenerationDialog)
{
    ui->setupUi(this);

    // See ExplodedViewPanel's/ClippingPlanesEditor's identical connection -
    // without this, a live language switch in Settings left this dialog
    // showing whatever language was active at construction until the next
    // app restart.
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        ui->retranslateUi(this);
        });

    setupConnections();

    // Set initial page to Planar (index 0)
    ui->stackedWidget_Options->setCurrentIndex(0);

    // Disable relaxation iterations spinbox initially
    ui->spinBox_RelaxationIterations->setEnabled(false);
    ui->spinBox_RelaxationIterations_Smart->setEnabled(false);

    // Enable automatic resizing
    setSizeGripEnabled(true);

    // Load last used settings
    loadLastUsedSettings();

    // Initial size adjustment
    adjustDialogSize();
}

UVGenerationDialog::~UVGenerationDialog()
{
    delete ui;
}

void UVGenerationDialog::setupConnections()
{
    // Connect method combo box to stacked widget
    connect(ui->comboBox_Method, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &UVGenerationDialog::onMethodChanged);

    // Connect relaxation checkboxes to enable/disable iterations spinbox
    connect(ui->checkBox_EnableRelaxation, &QCheckBox::toggled,
        this, &UVGenerationDialog::onRelaxationToggled);

    connect(ui->checkBox_EnableRelaxation_Smart, &QCheckBox::toggled,
        this, &UVGenerationDialog::onRelaxationToggled_Smart);

    connect(ui->checkBox_CylAutoDetectAxis, &QCheckBox::toggled,
        this, &UVGenerationDialog::onCylAutoDetectAxisToggled);
}

void UVGenerationDialog::onMethodChanged(int index)
{
    updateOptionsPage(index);
    adjustDialogSize();  // Resize after switching pages
}

void UVGenerationDialog::onRelaxationToggled(bool enabled)
{
    ui->spinBox_RelaxationIterations->setEnabled(enabled);
}

void UVGenerationDialog::onCylAutoDetectAxisToggled(bool autoDetect)
{
    // Manual axis fields are meaningless (and stay at their last-entered value) while auto-detect
    // is on - grey out the whole group box rather than let the user edit a value that isn't used.
    ui->groupBox_CylAxis->setEnabled(!autoDetect);
}

void UVGenerationDialog::onRelaxationToggled_Smart(bool enabled)
{
    ui->spinBox_RelaxationIterations_Smart->setEnabled(enabled);
}

void UVGenerationDialog::updateOptionsPage(int methodIndex)
{
    // Map combo box index to stacked widget page
    // 0: Planar, 1: Cylindrical, 2: Spherical, 3: AngleBased, 4: Hybrid, 5: SmartUV, 6: SmartProject, 7: ARAP
    ui->stackedWidget_Options->setCurrentIndex(methodIndex);
}

void UVGenerationDialog::adjustDialogSize()
{
    // Force layout update
    ui->stackedWidget_Options->currentWidget()->updateGeometry();
    layout()->activate();

    // Calculate the ideal size
    QSize idealSize = sizeHint();

    // Get current page's size hint
    int currentIndex = ui->stackedWidget_Options->currentIndex();
    QWidget* currentPage = ui->stackedWidget_Options->widget(currentIndex);
    QSize pageSize = currentPage->sizeHint();

    // Calculate required height
    // Base height includes: method groupbox + button box + margins
    int baseHeight = ui->groupBox_Method->sizeHint().height()
        + ui->buttonBox->sizeHint().height()
        + 80; // Margins and spacing

    // Add page content height (capped at 400px for scroll)
    int contentHeight = qMin(pageSize.height() + 40, 400);
    int totalHeight = baseHeight + contentHeight;

    // Set fixed width, adjustable height
    int width = 500;

    // Animate the resize for smooth transition
    QPropertyAnimation* animation = new QPropertyAnimation(this, "size");
    animation->setDuration(150);
    animation->setStartValue(size());
    animation->setEndValue(QSize(width, totalHeight));
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    // Alternative: Immediate resize (no animation)
    // resize(width, totalHeight);
}

void UVGenerationDialog::loadLastUsedSettings()
{
    QSettings settings("YourCompany", "YourApp");
    settings.beginGroup("UVGenerationDialog");

    // Load window geometry
    if (settings.contains("geometry"))
    {
        restoreGeometry(settings.value("geometry").toByteArray());
    }

    // Load method
    int methodIndex = settings.value("lastMethod", 0).toInt();
    ui->comboBox_Method->setCurrentIndex(methodIndex);

    // Load all config values
    UVConfig config;

    // Spherical
    config.sphericalScale = settings.value("sphericalScale", 1.0f).toFloat();
    config.sphericalUVRotation = settings.value("sphericalUVRotation", 0.0f).toFloat();
    config.duplicatePoleVertices = settings.value("duplicatePoleVertices", true).toBool();
    config.seamlessSpherical = settings.value("seamlessSpherical", true).toBool();

    // Cylindrical
    config.cylindricalScale = settings.value("cylindricalScale", 1.0f).toFloat();
    config.cylindricalOffset = settings.value("cylindricalOffset", 0.0f).toFloat();
    config.cylindricalSeamRotation = settings.value("cylindricalSeamRotation", 0.0f).toFloat();
    config.cylindricalAutoDetectAxis = settings.value("cylindricalAutoDetectAxis", true).toBool();
    config.cylindricalAxis = glm::vec3(
        settings.value("cylindricalAxisX", 0.0f).toFloat(),
        settings.value("cylindricalAxisY", 1.0f).toFloat(),
        settings.value("cylindricalAxisZ", 0.0f).toFloat()
    );

    // Planar
    config.planarScale.x = settings.value("planarScaleX", 1.0f).toFloat();
    config.planarScale.y = settings.value("planarScaleY", 1.0f).toFloat();

    // Common
    config.flipV = settings.value("flipV", false).toBool();

    // Angle-based
    config.angleThreshold = settings.value("angleThreshold", 60.0f).toFloat();
    config.distortionWeight = settings.value("distortionWeight", 0.5f).toFloat();
    config.preserveAspectRatio = settings.value("preserveAspectRatio", true).toBool();
    config.seamPadding = settings.value("seamPadding", 0.02f).toFloat();
    config.enableRelaxation = settings.value("enableRelaxation", false).toBool();
    config.relaxationIterations = settings.value("relaxationIterations", 10).toInt();
    config.enablePacking = settings.value("enablePacking", true).toBool();

    // Smart Project
    config.smartProjectAngleLimit = settings.value("smartProjectAngleLimit", 66.0f).toFloat();
    config.smartProjectAreaWeight = settings.value("smartProjectAreaWeight", 0.0f).toFloat();

    // ARAP
    config.arapLambda = settings.value("arapLambda", 1000.0f).toFloat();

    settings.endGroup();

    // Apply config to UI
    setConfig(config);
}

void UVGenerationDialog::saveLastUsedSettings()
{
    QSettings settings("YourCompany", "YourApp");
    settings.beginGroup("UVGenerationDialog");

    // Save window geometry
    settings.setValue("geometry", saveGeometry());

    // Save method
    settings.setValue("lastMethod", ui->comboBox_Method->currentIndex());

    // Get current config
    UVConfig config = getUVConfig();

    // Save all values
    // Spherical
    settings.setValue("sphericalScale", config.sphericalScale);
    settings.setValue("sphericalUVRotation", config.sphericalUVRotation);
    settings.setValue("duplicatePoleVertices", config.duplicatePoleVertices);
    settings.setValue("seamlessSpherical", config.seamlessSpherical);

    // Cylindrical
    settings.setValue("cylindricalScale", config.cylindricalScale);
    settings.setValue("cylindricalOffset", config.cylindricalOffset);
    settings.setValue("cylindricalSeamRotation", config.cylindricalSeamRotation);
    settings.setValue("cylindricalAutoDetectAxis", config.cylindricalAutoDetectAxis);
    settings.setValue("cylindricalAxisX", config.cylindricalAxis.x);
    settings.setValue("cylindricalAxisY", config.cylindricalAxis.y);
    settings.setValue("cylindricalAxisZ", config.cylindricalAxis.z);

    // Planar
    settings.setValue("planarScaleX", config.planarScale.x);
    settings.setValue("planarScaleY", config.planarScale.y);

    // Common
    settings.setValue("flipV", config.flipV);

    // Angle-based
    settings.setValue("angleThreshold", config.angleThreshold);
    settings.setValue("distortionWeight", config.distortionWeight);
    settings.setValue("preserveAspectRatio", config.preserveAspectRatio);
    settings.setValue("seamPadding", config.seamPadding);
    settings.setValue("enableRelaxation", config.enableRelaxation);
    settings.setValue("relaxationIterations", config.relaxationIterations);
    settings.setValue("enablePacking", config.enablePacking);

    // Smart Project
    settings.setValue("smartProjectAngleLimit", config.smartProjectAngleLimit);
    settings.setValue("smartProjectAreaWeight", config.smartProjectAreaWeight);

    // ARAP
    settings.setValue("arapLambda", config.arapLambda);

    settings.endGroup();
}


UVMethod UVGenerationDialog::getSelectedMethod() const
{
    int index = ui->comboBox_Method->currentIndex();

    switch (index)
    {
    case 0: return UVMethod::Planar;
    case 1: return UVMethod::Cylindrical;
    case 2: return UVMethod::Spherical;
    case 3: return UVMethod::AngleBased;
    case 4: return UVMethod::Hybrid;
    case 5: return UVMethod::AngleBasedSmartUV;
    case 6: return UVMethod::SmartProject;
    case 7: return UVMethod::ARAP;
    default: return UVMethod::Planar;
    }
}

UVConfig UVGenerationDialog::getUVConfig() const
{
    UVConfig config;

    UVMethod method = getSelectedMethod();

    switch (method)
    {
    case UVMethod::Planar:
        config.planarScale = glm::vec2(
            ui->spinBox_PlanarScaleX->value(),
            ui->spinBox_PlanarScaleY->value()
        );
        config.flipV = ui->checkBox_FlipV_Planar->isChecked();
        break;

    case UVMethod::Cylindrical:
        config.cylindricalScale = ui->spinBox_CylScale->value();
        config.cylindricalOffset = ui->spinBox_CylOffset->value();
        config.cylindricalSeamRotation = glm::radians(
            static_cast<float>(ui->spinBox_CylSeamRotation->value())
        );
        config.cylindricalAutoDetectAxis = ui->checkBox_CylAutoDetectAxis->isChecked();
        config.cylindricalAxis = glm::vec3(
            ui->spinBox_CylAxisX->value(),
            ui->spinBox_CylAxisY->value(),
            ui->spinBox_CylAxisZ->value()
        );
        config.flipV = ui->checkBox_FlipV_Cyl->isChecked();
        break;

    case UVMethod::Spherical:
        config.sphericalScale = ui->spinBox_SphereScale->value();
        config.sphericalUVRotation = glm::radians(
            static_cast<float>(ui->spinBox_SphereRotation->value())
        );
        config.duplicatePoleVertices = ui->checkBox_DuplicatePoles->isChecked();
        config.seamlessSpherical = ui->checkBox_SeamlessSpherical->isChecked();
        config.flipV = ui->checkBox_FlipV_Sphere->isChecked();
        break;

    case UVMethod::AngleBased:
        config.angleThreshold = ui->spinBox_AngleThreshold->value();
        config.distortionWeight = ui->spinBox_DistortionWeight->value();
        config.preserveAspectRatio = ui->checkBox_PreserveAspect->isChecked();
        config.seamPadding = ui->spinBox_SeamPadding->value();
        config.enableRelaxation = ui->checkBox_EnableRelaxation->isChecked();
        config.relaxationIterations = ui->spinBox_RelaxationIterations->value();
        config.enablePacking = ui->checkBox_EnablePacking->isChecked();
        break;

    case UVMethod::Hybrid:
        // Hybrid uses default config values
        break;

    case UVMethod::AngleBasedSmartUV:
        config.angleThreshold = ui->spinBox_AngleThreshold_Smart->value();
        config.enableRelaxation = ui->checkBox_EnableRelaxation_Smart->isChecked();
        config.relaxationIterations = ui->spinBox_RelaxationIterations_Smart->value();
        break;

    case UVMethod::SmartProject:
        config.smartProjectAngleLimit = ui->spinBox_AngleLimit_SmartProject->value();
        config.smartProjectAreaWeight = ui->spinBox_AreaWeight_SmartProject->value();
        config.enablePacking = ui->checkBox_EnablePacking_SmartProject->isChecked();
        config.flipV = ui->checkBox_FlipV_SmartProject->isChecked();
        break;

    case UVMethod::ARAP:
        config.angleThreshold = ui->spinBox_AngleThreshold_ARAP->value();
        config.arapLambda = ui->spinBox_Rigidity_ARAP->value();
        config.seamPadding = ui->spinBox_SeamPadding_ARAP->value();
        config.enablePacking = ui->checkBox_EnablePacking_ARAP->isChecked();
        config.flipV = ui->checkBox_FlipV_ARAP->isChecked();
        break;

    default:
        break;
    }

    return config;
}

void UVGenerationDialog::setMethod(UVMethod method)
{
    int index = 0;

    switch (method)
    {
    case UVMethod::Planar: index = 0; break;
    case UVMethod::Cylindrical: index = 1; break;
    case UVMethod::Spherical: index = 2; break;
    case UVMethod::AngleBased: index = 3; break;
    case UVMethod::Hybrid: index = 4; break;
    case UVMethod::AngleBasedSmartUV: index = 5; break;
    case UVMethod::SmartProject: index = 6; break;
    case UVMethod::ARAP: index = 7; break;
    default: index = 0; break;
    }

    ui->comboBox_Method->setCurrentIndex(index);
}

void UVGenerationDialog::setConfig(const UVConfig& config)
{
    // Set Planar values
    ui->spinBox_PlanarScaleX->setValue(config.planarScale.x);
    ui->spinBox_PlanarScaleY->setValue(config.planarScale.y);
    ui->checkBox_FlipV_Planar->setChecked(config.flipV);

    // Set Cylindrical values
    ui->spinBox_CylScale->setValue(config.cylindricalScale);
    ui->spinBox_CylOffset->setValue(config.cylindricalOffset);
    ui->spinBox_CylSeamRotation->setValue(glm::degrees(config.cylindricalSeamRotation));
    ui->checkBox_CylAutoDetectAxis->setChecked(config.cylindricalAutoDetectAxis);
    ui->spinBox_CylAxisX->setValue(config.cylindricalAxis.x);
    ui->spinBox_CylAxisY->setValue(config.cylindricalAxis.y);
    ui->spinBox_CylAxisZ->setValue(config.cylindricalAxis.z);
    // checkBox_CylAutoDetectAxis::setChecked above only emits toggled() (and so only runs
    // onCylAutoDetectAxisToggled's enable/disable) when the checked state actually CHANGES - so a
    // config with the checkbox already in the right state would otherwise leave the axis group
    // box's enabled state stale relative to it. Set it explicitly instead of relying on the signal.
    ui->groupBox_CylAxis->setEnabled(!config.cylindricalAutoDetectAxis);
    ui->checkBox_FlipV_Cyl->setChecked(config.flipV);

    // Set Spherical values
    ui->spinBox_SphereScale->setValue(config.sphericalScale);
    ui->spinBox_SphereRotation->setValue(glm::degrees(config.sphericalUVRotation));
    ui->checkBox_DuplicatePoles->setChecked(config.duplicatePoleVertices);
    ui->checkBox_SeamlessSpherical->setChecked(config.seamlessSpherical);
    ui->checkBox_FlipV_Sphere->setChecked(config.flipV);

    // Set Angle-Based values
    ui->spinBox_AngleThreshold->setValue(config.angleThreshold);
    ui->spinBox_DistortionWeight->setValue(config.distortionWeight);
    ui->checkBox_PreserveAspect->setChecked(config.preserveAspectRatio);
    ui->spinBox_SeamPadding->setValue(config.seamPadding);
    ui->checkBox_EnableRelaxation->setChecked(config.enableRelaxation);
    ui->spinBox_RelaxationIterations->setValue(config.relaxationIterations);
    ui->checkBox_EnablePacking->setChecked(config.enablePacking);

    // Set Smart UV values
    ui->spinBox_AngleThreshold_Smart->setValue(config.angleThreshold);
    ui->checkBox_EnableRelaxation_Smart->setChecked(config.enableRelaxation);
    ui->spinBox_RelaxationIterations_Smart->setValue(config.relaxationIterations);

    // Set Smart Project values
    ui->spinBox_AngleLimit_SmartProject->setValue(config.smartProjectAngleLimit);
    ui->spinBox_AreaWeight_SmartProject->setValue(config.smartProjectAreaWeight);
    ui->checkBox_EnablePacking_SmartProject->setChecked(config.enablePacking);
    ui->checkBox_FlipV_SmartProject->setChecked(config.flipV);

    // Set ARAP values
    ui->spinBox_AngleThreshold_ARAP->setValue(config.angleThreshold);
    ui->spinBox_Rigidity_ARAP->setValue(config.arapLambda);
    ui->spinBox_SeamPadding_ARAP->setValue(config.seamPadding);
    ui->checkBox_EnablePacking_ARAP->setChecked(config.enablePacking);
    ui->checkBox_FlipV_ARAP->setChecked(config.flipV);

    // Update enabled states
    ui->spinBox_RelaxationIterations->setEnabled(config.enableRelaxation);
    ui->spinBox_RelaxationIterations_Smart->setEnabled(config.enableRelaxation);
}

QString UVGenerationDialog::getMethodName(UVMethod method) const
{
	switch (method)
	{
	case UVMethod::Planar: return "Planar";
	case UVMethod::Cylindrical: return "Cylindrical";
	case UVMethod::Spherical: return "Spherical";
	case UVMethod::AngleBased: return "Angle-Based";
	case UVMethod::Hybrid: return "Hybrid";
	case UVMethod::AngleBasedSmartUV: return "Smart UV";
	case UVMethod::SmartProject: return "Smart Project (Blender-style)";
	case UVMethod::ARAP: return "ARAP (As-Rigid-As-Possible)";
	default: return "Unknown";
	}
}

// Instead of using closeEvent, override accept():
void UVGenerationDialog::accept()
{
    // Save settings only when OK is clicked
    saveLastUsedSettings();

    // Call base class accept (closes dialog with Accepted result)
    QDialog::accept();
}
