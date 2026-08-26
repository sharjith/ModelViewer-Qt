#include "LanguageManager.h"
#include "Logger.h"
#include "PathUtils.h"
#include "SettingsDialog.h"
#include "ModelViewerApplication.h"
#include "ui_SettingsDialog.h"
#include <algorithm>
#include <QColorDialog>
#include <QDir>
#include <QMessageBox>
#include <QTimer>

namespace
{
constexpr int kCornerTrihedronTopLeft = 0;
constexpr int kCornerTrihedronTopRight = 1;
constexpr int kCornerTrihedronBottomLeft = 2;
constexpr int kCornerTrihedronBottomRight = 3;

int normalizeCornerTrihedronPositionValue(int value)
{
    return std::clamp(value, kCornerTrihedronTopLeft, kCornerTrihedronBottomRight);
}

void configureCornerTrihedronPositionCombo(QComboBox* comboBox)
{
    if (!comboBox)
        return;

    comboBox->setItemData(0, kCornerTrihedronTopLeft);
    comboBox->setItemData(1, kCornerTrihedronTopRight);
    comboBox->setItemData(2, kCornerTrihedronBottomLeft);
    comboBox->setItemData(3, kCornerTrihedronBottomRight);
}

void setCornerTrihedronPositionSelection(QComboBox* comboBox, int positionValue)
{
    if (!comboBox)
        return;

    const int normalizedValue = normalizeCornerTrihedronPositionValue(positionValue);
    const int index = comboBox->findData(normalizedValue);
    comboBox->setCurrentIndex(index >= 0 ? index : 1);
}

int currentCornerTrihedronPositionValue(const QComboBox* comboBox)
{
    if (!comboBox)
        return kCornerTrihedronTopRight;

    const QVariant data = comboBox->currentData();
    if (data.isValid())
        return normalizeCornerTrihedronPositionValue(data.toInt());

    return normalizeCornerTrihedronPositionValue(comboBox->currentIndex());
}

bool isViewCubeAvailableForCornerPosition(int positionValue)
{
    return normalizeCornerTrihedronPositionValue(positionValue) != kCornerTrihedronBottomRight;
}
}

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),
    _themeManager(new ThemeManager(this))
{
    ui->setupUi(this);
    configureCornerTrihedronPositionCombo(ui->comboBoxCornerTrihedronPosition);

    // Attach the stable preset key to each Default Material entry (see
    // MaterialProcessor::setDefaultMaterial for the matching lookup).
    static const QString kDefaultMaterialKeys[] = {
        QString(),          // "Default (Neutral)"
        "WHITE_PLASTIC",     // "Plastic (White)"
        "METAL_ALUMINUM",    // "Metal (Aluminum)"
        "GLASS",             // "Glass"
        "BLACK_RUBBER",      // "Rubber (Black)"
        "WOOD"                // "Wood"
    };
    const int kDefaultMaterialKeyCount =
        static_cast<int>(sizeof(kDefaultMaterialKeys) / sizeof(kDefaultMaterialKeys[0]));
    for (int i = 0; i < ui->comboBoxDefaultMaterial->count() && i < kDefaultMaterialKeyCount; ++i)
        ui->comboBoxDefaultMaterial->setItemData(i, kDefaultMaterialKeys[i]);

    // Populate the Default Skybox combos by scanning the same preset folders
    // VisualizationEnvironmentPanel::reloadSkyBoxPresets() uses. Folder name is used
    // as both display text and itemData so the setting persists by name, not index.
    {
        const QString envmapRoot = PathUtils::getDataDirectory() + "/textures/envmap/skyboxes";
        QDir hdriDir(envmapRoot + "/HDRI");
        for (const QString& folderName : hdriDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            ui->comboBoxDefaultSkyboxHDRI->addItem(folderName, folderName);
        QDir ldriDir(envmapRoot + "/LDRI");
        for (const QString& folderName : ldriDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            ui->comboBoxDefaultSkyboxLDRI->addItem(folderName, folderName);
    }

    retranslateUI();

    // Connect to specific buttons
    QPushButton *okButton = ui->buttonBox->button(QDialogButtonBox::Ok); // Get the "OK" button
    if (okButton) {
        connect(okButton, &QPushButton::clicked, this, &SettingsDialog::onOkClicked);
    }

    QPushButton *cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel); // Get the "Cancel" button
    if (cancelButton) {
        connect(cancelButton, &QPushButton::clicked, this, &SettingsDialog::onCancelClicked);
    }

    QPushButton *applyButton = ui->buttonBox->button(QDialogButtonBox::Apply); // Get the "Cancel" button
    if (applyButton) {
        connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::onApplyClicked);
    }

    QPushButton* restoreButton = ui->buttonBox->button(QDialogButtonBox::RestoreDefaults);
    if (restoreButton) {
        connect(restoreButton, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    }

    loadSettings();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this]() {
        updateSettingsHint();
    });
    updateSettingsHint();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        ui->retranslateUi(this);
        retranslateUI();
        // Second pass: QDialogButtonBox handles QEvent::LanguageChange internally
        // and overwrites our button texts after this slot returns. A deferred call
        // ensures retranslateUI() runs after those events settle.
        // Safe: 'this' as context prevents firing if the dialog is destroyed.
        QTimer::singleShot(0, this, [this]() {
            retranslateUI();
        });
    });
}

SettingsDialog::~SettingsDialog()
{
	blockSignals(true);
    delete ui;
}

void SettingsDialog::retranslateUI()
{
    // Dialog buttons
    if (ui->buttonBox->button(QDialogButtonBox::Ok))
        ui->buttonBox->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("SettingsDialog", "OK"));
    if (ui->buttonBox->button(QDialogButtonBox::Cancel))
        ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("SettingsDialog", "Cancel"));
    if (ui->buttonBox->button(QDialogButtonBox::Apply))
        ui->buttonBox->button(QDialogButtonBox::Apply)->setText(QCoreApplication::translate("SettingsDialog", "Apply"));
    if (ui->buttonBox->button(QDialogButtonBox::RestoreDefaults))
        ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(QCoreApplication::translate("SettingsDialog", "Defaults"));
    updateSettingsHint();

    ui->buttonBox->updateGeometry();
    ui->buttonBox->update();
    ui->buttonBox->repaint();
    this->update();
    this->repaint();
    // MSAA ComboBox
    //setMaxMSAASamples(/* pass current maxSamples value here */);

    // Anisotropy ComboBox
    //setMaxAnisotropy(/* pass current maxAnisotropy value here */);

    // If there are other dynamically set texts, add them here.
}

void SettingsDialog::refreshViewCubeAvailability()
{
    const bool viewCubeAvailable = isViewCubeAvailableForCornerPosition(
        currentCornerTrihedronPositionValue(ui->comboBoxCornerTrihedronPosition));
    ui->showViewCubeCheckBox->setEnabled(viewCubeAvailable);
    if (!viewCubeAvailable)
        ui->showViewCubeCheckBox->setChecked(false);
}

void SettingsDialog::setMaxMSAASamples(int maxSamples)
{
    bool oldState = ui->msaaComboBox->blockSignals(true);
    ui->msaaComboBox->clear();
    ui->msaaComboBox->addItem("None", 0);
    if (maxSamples >= 2) ui->msaaComboBox->addItem("2x", 2);
    if (maxSamples >= 4) ui->msaaComboBox->addItem("4x", 4);
    if (maxSamples >= 8) ui->msaaComboBox->addItem("8x", 8);
    if (maxSamples >= 16) ui->msaaComboBox->addItem("16x", 16);
    if (maxSamples >= 32) ui->msaaComboBox->addItem("32x", 32);

	QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    int val = settings.value("msaaComboBox", ui->msaaComboBox->currentIndex()).toInt();
    ui->msaaComboBox->setCurrentIndex(val);
    ui->msaaComboBox->blockSignals(oldState);
}

void SettingsDialog::setMaxAnisotropy(int maxAnisotropy)
{
    bool oldState = ui->anisotropyComboBox->blockSignals(true);
    ui->anisotropyComboBox->clear();
    ui->anisotropyComboBox->addItem("1x (None)", 1.0f);
    if (maxAnisotropy >= 2) ui->anisotropyComboBox->addItem("2x", 2.0f);
    if (maxAnisotropy >= 4) ui->anisotropyComboBox->addItem("4x", 4.0f);
    if (maxAnisotropy >= 8) ui->anisotropyComboBox->addItem("8x", 8.0f);
    if (maxAnisotropy >= 16) ui->anisotropyComboBox->addItem("16x", 16.0f);
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    int val = settings.value("anisotropyComboBox", ui->anisotropyComboBox->currentIndex()).toInt();
    ui->anisotropyComboBox->setCurrentIndex(val);
    ui->anisotropyComboBox->blockSignals(oldState);
}


void SettingsDialog::onOkClicked()
{
	applySettings();
    QDialog::accept();
}

void SettingsDialog::onCancelClicked()
{
	QDialog::reject();
}

void SettingsDialog::onApplyClicked()
{
	applySettings();
}

void SettingsDialog::onRestoreDefaults()
{
    blockAllChildWidgetSignals(true);
	setDefaultValues();
    blockAllChildWidgetSignals(false);
    syncStateFromUi();
    QMessageBox::information(this, tr("Defaults Staged"), tr("Default settings are loaded in the dialog. Click Apply or OK to save them."));
}


void SettingsDialog::applySettings()
{
    syncStateFromUi();

    const bool themeChanged = (_appliedThemeIndex != general_themeIndex);
    const bool languageChanged = (_appliedLanguageIndex != general_languageIndex);
    const bool fileLoggingChanged = (_appliedDebugEnableLogging != debug_enableLogging);
    const bool consoleLoggingChanged = (_appliedDebugEnableConsoleOutput != debug_enableConsoleOutput);
    const bool logLevelChanged = (_appliedDebugLogLevelIndex != debug_logLevelIndex);
    const bool consoleBufferLinesChanged = (_appliedDebugConsoleBufferLines != debug_consoleBufferLines);

    // Apply the settings of the UI elements
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    // General tab
    settings.setValue("comboBoxTheme", general_themeIndex);
    settings.setValue("comboBoxLanguage", general_languageIndex);

    QString langCode = "en"; // Default to English 
    if (general_languageIndex == 0)
        langCode = "en"; // English
    else if (general_languageIndex == 1)
        langCode = "fr"; // French
    else if (general_languageIndex == 2)
        langCode = "de"; // German
    else if (general_languageIndex == 3)
        langCode = "es"; // Spanish
    else if (general_languageIndex == 4)
        langCode = "it"; // Italian    
    settings.setValue("App/Language", langCode);

    if (themeChanged)
    {
        _themeManager->setTheme(static_cast<ThemeManager::Theme>(general_themeIndex));
    }
    if (languageChanged)
    {
        // Defer past QDialog::accept() so the dialog is safely closed before
        // retranslation fires — prevents re-entrant UI modification and
        // timer callbacks racing with dialog destruction.
        QTimer::singleShot(0, [langCode]() {
            LanguageManager::instance().loadLanguage(langCode);
        });
    }

    settings.setValue("checkRestoreLastFile", general_restoreLastFile);
    settings.setValue("checkTooltips", general_showTooltips);
    settings.setValue("checkConfirmExit", general_confirmExit);
    settings.setValue("checkTutorialLaunch", general_showTutorialLauncher);
	settings.setValue("spinBoxUndoLimit", general_undoLimit);
    settings.setValue("checkProgressiveLoading", general_progressiveLoading);
    settings.setValue("checkAnimateProgressiveFit", general_animateProgressiveFit);

    // Camera tab
    settings.setValue("comboProjectionMode", camera_projectionModeIndex);
    settings.setValue("comboDefaultView", camera_defaultViewIndex);
    settings.setValue("comboDefaultProjection", camera_defaultProjectionIndex);
    settings.setValue("comboCameraUpAxis", camera_defaultUpAxisIndex);
    settings.setValue("checkInvertZoom", camera_invertZoom);

    // Background tab
    settings.setValue("Background/StyleIndex", background_styleIndex);
    settings.setValue("Background/GradientStyle", background_gradientStyleIndex);
    settings.setValue("Background/TopColor", background_topColor);
    settings.setValue("Background/BottomColor", background_bottomColor);

    // Display tab
    settings.setValue("showBoundingBoxCheckBox", display_showBoundingBox);
    settings.setValue("showCornerTrihedronCheckBox", display_showCornerTrihedron);
    settings.setValue("showViewCubeCheckBox", display_showViewCube);
    settings.setValue("comboBoxCornerTrihedronPosition", display_cornerTrihedronPosition);
    settings.setValue("showVertexNormalsCheckBox", display_showVertexNormals);
    settings.setValue("showFaceNormalsCheckBox", display_showFaceNormals);
    settings.setValue("showWireframeCheckBox", display_showWireframe);
    settings.setValue("fieldOfViewSpinBox", display_fieldOfView);
    settings.setValue("showCenterTrihedronCheckBox", display_showCenterTrihedron);
    settings.setValue("comboBoxHoverHighlightMode", static_cast<int>(display_hoverHighlightMode));
    settings.setValue("vsyncCheckBox", display_vsync);
    settings.setValue("comboBoxDefaultSkyboxHDRI", display_defaultSkyboxHDRI);
    settings.setValue("comboBoxDefaultSkyboxLDRI", display_defaultSkyboxLDRI);

    // Navigation group
    settings.setValue("navigationModeComboBox", navigation_modeIndex);
    settings.setValue("mouseSensitivitySlider", navigation_mouseSensitivity);
    settings.setValue("wheelSensitivitySlider", navigation_wheelSensitivity);
    settings.setValue("invertYAxisCheckBox", navigation_invertYAxis);
    settings.setValue("smoothNavigationCheckBox", navigation_smoothNavigation);

    // Rendering tab
    settings.setValue("comboShadingMode", rendering_shadingModeIndex);
    settings.setValue("checkBackfaceCulling", rendering_backfaceCulling);
    settings.setValue("shaderModelComboBox", rendering_shaderModelIndex);
    settings.setValue("shadingNormalComboBox", rendering_shadingNormalIndex);
    settings.setValue("msaaComboBox", rendering_msaaIndex);
    settings.setValue("anisotropyComboBox", rendering_anisotropyIndex);

    // Lighting
    settings.setValue("enableLightingCheckBox", lighting_enableLighting);
    settings.setValue("enableShadowsCheckBox", lighting_enableShadows);
    settings.setValue("ambientLightSlider", lighting_ambient);
    settings.setValue("diffuseLightSlider", lighting_diffuse);
    settings.setValue("specularLightSlider", lighting_specular);

    // Materials
    settings.setValue("comboBoxDefaultMaterial", materials_defaultMaterialKey);
    settings.setValue("lineEditTextureDir", materials_textureDir);

    // UV Generation Tab
    // angleThreshold/preserveAspectRatio/enableRelaxation/enablePacking are the exact
    // keys UVGenerationDialog itself reads/writes — this tab edits its defaults directly.
    settings.setValue("UVMethod", uv_methodIndex);
    settings.setValue("angleThreshold", static_cast<float>(uv_angleThreshold));
    settings.setValue("preserveAspectRatio", uv_preserveUVs);
    settings.setValue("enablePacking", uv_autoPackUVs);
    settings.setValue("enableRelaxation", uv_relaxUVs);
    settings.setValue("RememberUVMethod", uv_rememberUV);

    // Import/Export Tab - OpenCascade
    settings.setValue("linearDeflectionSpinBox", import_linearDeflection);
    settings.setValue("angularDeflectionSpinBox", import_angularDeflection);

    // Import/Export Tab - Assimp
    settings.setValue("assimpGenNormalsCheckBox", import_assimpGenNormals);
    settings.setValue("assimpSmoothNormalsCheckBox", import_assimpSmoothNormals);
    settings.setValue("assimpCalcTangentsCheckBox", import_assimpCalcTangents);
    settings.setValue("assimpOptimizeMeshCheckBox", import_assimpOptimizeMesh);
    settings.setValue("assimpRemoveDuplicatesCheckBox", import_assimpRemoveDuplicates);
    settings.setValue("assimpAutoOrientCheckBox", import_assimpAutoOrientModel);

	// Import/Export Tab - Export
	settings.setValue("radioButtonExportScene", export_exportScene);
	settings.setValue("radioButtonExportMeshes", export_exportMeshes);

    // Debug Tab
    settings.setValue("enableLoggingCheckBox", debug_enableLogging);
	settings.setValue("enableConsoleCheckBox", debug_enableConsoleOutput);
    settings.setValue("logLevelComboBox", debug_logLevelIndex);
    settings.setValue("consoleBufferLinesSpinBox", debug_consoleBufferLines);
    settings.setValue("profileRenderingCheckBox", debug_profileRendering);
    settings.setValue("showTextureDebugPanelCheckBox", debug_showTextureDebugPanel);

    if (general_showTutorialLauncher)
    {
        settings.setValue("tutorial/displayMode", "ask");
    }

    if (fileLoggingChanged)
    {
        Logger::instance().setFileEnabled(debug_enableLogging);
    }
    if (consoleLoggingChanged)
    {
        Logger::instance().setConsoleEnabled(debug_enableConsoleOutput);
    }
    if (logLevelChanged)
    {
        Logger::instance().setMinimumLevel(static_cast<Logger::LogLevel>(debug_logLevelIndex));
    }
    if (consoleBufferLinesChanged)
    {
        Logger::instance().setConsoleBufferLines(debug_consoleBufferLines);
    }

    captureAppliedState();

    // Notify other parts of the application about the settings change
	emit settingsChanged();

}

void SettingsDialog::setDefaultValues()
{
    // General tab
    ui->checkRestoreLastFile->setChecked(false);  // No 'checked' property found
    ui->checkTooltips->setChecked(true);          // Explicitly set to true
    ui->comboBoxTheme->setCurrentIndex(0);        // Default: "System Default"
    ui->comboBoxLanguage->setCurrentIndex(0);     // Default: "English"
    ui->checkConfirmExit->setChecked(false);      // No 'checked' property found
	ui->checkTutorialLaunch->setChecked(true);    // Explicitly set to true
	ui->spinBoxUndoLimit->setValue(50);           // Explicitly set
    ui->checkProgressiveLoading->setChecked(false);
    ui->checkAnimateProgressiveFit->setChecked(true);

    // Camera tab
    ui->comboProjectionMode->setCurrentIndex(0);       // "Orthographic"
    ui->comboDefaultView->setCurrentIndex(0);          // "Isometric"
    ui->comboDefaultProjection->setCurrentIndex(0);    // "Isometric"
    ui->comboCameraUpAxis->setCurrentIndex(0);         // "Z-Up"
    ui->checkInvertZoom->setChecked(false);            // No 'checked' property found

    // Background tab
    ui->comboBoxBackgroundStyle->setCurrentIndex(0);   // "Gradient"
    ui->comboBoxGradientStyle->setCurrentIndex(0);     // "Vertical"
    {
        const QColor topDefault(128, 128, 128);
        ui->pushButtonTopColor->setProperty("color", topDefault);
        ui->pushButtonTopColor->setStyleSheet(QString("background-color: %1").arg(topDefault.name()));
        const QColor bottomDefault(64, 64, 64);
        ui->pushButtonBottomColor->setProperty("color", bottomDefault);
        ui->pushButtonBottomColor->setStyleSheet(QString("background-color: %1").arg(bottomDefault.name()));
    }

    // Display tab
    ui->showBoundingBoxCheckBox->setChecked(true);     // Explicitly set to true
    ui->showCornerTrihedronCheckBox->setChecked(true); // Explicitly set to true
    ui->showViewCubeCheckBox->setChecked(true);        // Explicitly set to true
	ui->showCenterTrihedronCheckBox->setChecked(true); // Explicitly set to true
    setCornerTrihedronPositionSelection(ui->comboBoxCornerTrihedronPosition, kCornerTrihedronTopRight);
    refreshViewCubeAvailability();
    ui->showVertexNormalsCheckBox->setChecked(true);   // Explicitly set to true
    ui->showFaceNormalsCheckBox->setChecked(true);     // Explicitly set to true
    ui->showWireframeCheckBox->setChecked(true);       // Explicitly set to true
    ui->fieldOfViewSpinBox->setValue(45);              // Explicitly set
    if (ui->comboBoxHoverHighlightMode)
        ui->comboBoxHoverHighlightMode->setCurrentIndex(0); // Default: Disabled
    ui->vsyncCheckBox->setChecked(true);
    if (ui->comboBoxDefaultSkyboxHDRI->count() > 0)
        ui->comboBoxDefaultSkyboxHDRI->setCurrentIndex(0);
    if (ui->comboBoxDefaultSkyboxLDRI->count() > 0)
        ui->comboBoxDefaultSkyboxLDRI->setCurrentIndex(0);

    // Navigation group
    ui->navigationModeComboBox->setCurrentIndex(0);          // "Orbit"
    ui->mouseSensitivitySlider->setValue(5);                 // Explicitly set
    ui->zoomSensitivitySlider->setValue(5);                  // Explicitly set
    ui->invertYAxisCheckBox->setChecked(false);              // Not set
    ui->smoothNavigationCheckBox->setChecked(true);          // Explicitly set

    // Rendering tab
    ui->comboShadingMode->setCurrentIndex(0);                // "Shaded"
    ui->checkBackfaceCulling->setChecked(false);             // Not set
    ui->shaderModelComboBox->setCurrentIndex(0);             // "Blinn-Phong (ADS)"
    ui->shadingNormalComboBox->setCurrentIndex(0);           // "Smooth"
    	
	int maxSamples = ModelViewerApplication::supportedMSAASamples();
	if(maxSamples >= 4)
        ui->msaaComboBox->setCurrentIndex(3);                    // "4x"    
    else
        ui->msaaComboBox->setCurrentIndex(0);                    // "1 (No MSAA)"
    
	int maxAnisotropy = ModelViewerApplication::supportedAnisotropicFilteringLevel();
    if(maxAnisotropy >= 4)
        ui->anisotropyComboBox->setCurrentIndex(3);              // "4x"
	else
        ui->anisotropyComboBox->setCurrentIndex(0);              // "1x (Off)"

    // Lighting
    ui->enableLightingCheckBox->setChecked(true);
    ui->enableShadowsCheckBox->setChecked(false);
    ui->ambientLightSlider->setValue(20);
    ui->diffuseLightSlider->setValue(80);
    ui->specularLightSlider->setValue(50);

    // Materials
    ui->comboBoxDefaultMaterial->setCurrentIndex(0);     // "Default (Neutral)"
    ui->lineEditTextureDir->clear();                     // Default: empty

    // --- UV Generation Tab ---
    ui->comboUVMethod->setCurrentIndex(0);               // "Angle-Based Smart UV"
    ui->spinAngleThreshold->setValue(60.0);
    ui->checkPreserveUVs->setChecked(true);
    ui->checkAutoPackUVs->setChecked(true);
    ui->checkRelaxUVs->setChecked(false);
    ui->checkRememberUV->setChecked(false);

    // --- Import/Export Tab ---
    // OpenCascade settings
    ui->linearDeflectionSpinBox->setValue(0.1);
    ui->angularDeflectionSpinBox->setValue(0.3);

    // Assimp settings
    ui->assimpGenNormalsCheckBox->setChecked(true);
    ui->assimpSmoothNormalsCheckBox->setChecked(true);
    ui->assimpCalcTangentsCheckBox->setChecked(true);
    ui->assimpOptimizeMeshCheckBox->setChecked(true);
    ui->assimpRemoveDuplicatesCheckBox->setChecked(true);
	ui->assimpAutoOrientCheckBox->setChecked(true);

    // Import/Export Tab - Export
    ui->radioButtonExportScene->setChecked(true);
    ui->radioButtonExportMeshes->setChecked(false);

    // --- Debug Tab ---
    ui->enableLoggingCheckBox->setChecked(false);
	ui->enableConsoleCheckBox->setChecked(false);
    ui->logLevelComboBox->setCurrentText("Warning");
    ui->consoleBufferLinesSpinBox->setValue(20000);
    ui->profileRenderingCheckBox->setChecked(false);
    ui->showTextureDebugPanelCheckBox->setChecked(false);
}

void SettingsDialog::captureAppliedState()
{
    _appliedThemeIndex = general_themeIndex;
    _appliedLanguageIndex = general_languageIndex;
    _appliedDebugEnableLogging = debug_enableLogging;
    _appliedDebugEnableConsoleOutput = debug_enableConsoleOutput;
    _appliedDebugLogLevelIndex = debug_logLevelIndex;
    _appliedDebugConsoleBufferLines = debug_consoleBufferLines;
}

void SettingsDialog::updateSettingsHint()
{
    if (!ui || !ui->label_5 || !ui->tabWidget)
        return;

    QString hint = QCoreApplication::translate("SettingsDialog",
        "Some settings apply immediately. Others take effect only for newly opened documents or after restarting the application.");

    QWidget* currentTab = ui->tabWidget->currentWidget();
    if (currentTab == ui->tabDisplay || currentTab == ui->tabRendering || currentTab == ui->tabDebug)
    {
        hint = QCoreApplication::translate("SettingsDialog",
            "Most settings on this tab apply immediately. MSAA and some graphics options may still require restarting the application.");
    }
    else if (currentTab == ui->tabImportExport)
    {
        hint = QCoreApplication::translate("SettingsDialog",
            "These settings are primarily used for newly opened documents and future imports/exports. They may not affect models that are already loaded.");
    }
    else if (currentTab == ui->tabCamera || currentTab == ui->tabUVGeneration)
    {
        hint = QCoreApplication::translate("SettingsDialog",
            "These settings are mainly used as defaults for future actions and newly opened documents.");
    }

    ui->label_5->setText(hint);
}

void SettingsDialog::blockAllChildWidgetSignals(bool block)
{
    const auto widgets = this->findChildren<QWidget*>();
    for (QWidget* widget : widgets)
        widget->blockSignals(block);
}

void SettingsDialog::syncStateFromUi()
{
    // General tab
    general_themeIndex = ui->comboBoxTheme->currentIndex();
    general_languageIndex = ui->comboBoxLanguage->currentIndex();
    general_restoreLastFile = ui->checkRestoreLastFile->isChecked();
    general_showTooltips = ui->checkTooltips->isChecked();
    general_confirmExit = ui->checkConfirmExit->isChecked();
    general_showTutorialLauncher = ui->checkTutorialLaunch->isChecked();
    general_undoLimit = ui->spinBoxUndoLimit->value();
    general_progressiveLoading = ui->checkProgressiveLoading->isChecked();
    general_animateProgressiveFit = ui->checkAnimateProgressiveFit->isChecked();

    // Camera tab
    camera_projectionModeIndex = ui->comboProjectionMode->currentIndex();
    camera_defaultViewIndex = ui->comboDefaultView->currentIndex();
    camera_defaultProjectionIndex = ui->comboDefaultProjection->currentIndex();
    camera_defaultUpAxisIndex = ui->comboCameraUpAxis->currentIndex();
    camera_invertZoom = ui->checkInvertZoom->isChecked();

    // Background tab
    background_styleIndex = ui->comboBoxBackgroundStyle->currentIndex();
    background_gradientStyleIndex = ui->comboBoxGradientStyle->currentIndex();
    background_topColor = ui->pushButtonTopColor->property("color").value<QColor>();
    background_bottomColor = ui->pushButtonBottomColor->property("color").value<QColor>();

    // Display tab
    display_showBoundingBox = ui->showBoundingBoxCheckBox->isChecked();
    display_showCornerTrihedron = ui->showCornerTrihedronCheckBox->isChecked();
    display_cornerTrihedronPosition = currentCornerTrihedronPositionValue(ui->comboBoxCornerTrihedronPosition);
    display_showViewCube = isViewCubeAvailableForCornerPosition(display_cornerTrihedronPosition)
        && ui->showViewCubeCheckBox->isChecked();
    display_showVertexNormals = ui->showVertexNormalsCheckBox->isChecked();
    display_showFaceNormals = ui->showFaceNormalsCheckBox->isChecked();
    display_showWireframe = ui->showWireframeCheckBox->isChecked();
    display_fieldOfView = ui->fieldOfViewSpinBox->value();
    display_showCenterTrihedron = ui->showCenterTrihedronCheckBox->isChecked();
    if (ui->comboBoxHoverHighlightMode) {
        display_hoverHighlightMode = static_cast<HoverHighlightMode>(ui->comboBoxHoverHighlightMode->currentIndex());
    }
    display_vsync = ui->vsyncCheckBox->isChecked();
    display_defaultSkyboxHDRI = ui->comboBoxDefaultSkyboxHDRI->currentData().toString();
    display_defaultSkyboxLDRI = ui->comboBoxDefaultSkyboxLDRI->currentData().toString();

    // Navigation group
    navigation_modeIndex = ui->navigationModeComboBox->currentIndex();
    navigation_mouseSensitivity = ui->mouseSensitivitySlider->value();
    navigation_wheelSensitivity = ui->zoomSensitivitySlider->value();
    navigation_invertYAxis = ui->invertYAxisCheckBox->isChecked();
    navigation_smoothNavigation = ui->smoothNavigationCheckBox->isChecked();

    // Rendering tab
    rendering_shadingModeIndex = ui->comboShadingMode->currentIndex();
    rendering_backfaceCulling = ui->checkBackfaceCulling->isChecked();
    rendering_shaderModelIndex = ui->shaderModelComboBox->currentIndex();
    rendering_shadingNormalIndex = ui->shadingNormalComboBox->currentIndex();
    rendering_msaaIndex = ui->msaaComboBox->currentIndex();
    rendering_anisotropyIndex = ui->anisotropyComboBox->currentIndex();

    // Lighting
    lighting_enableLighting = ui->enableLightingCheckBox->isChecked();
    lighting_enableShadows = ui->enableShadowsCheckBox->isChecked();
    lighting_ambient = ui->ambientLightSlider->value();
    lighting_diffuse = ui->diffuseLightSlider->value();
    lighting_specular = ui->specularLightSlider->value();

    // Materials
    materials_defaultMaterialKey = ui->comboBoxDefaultMaterial->currentData().toString();
    materials_textureDir = ui->lineEditTextureDir->text();

    // UV Generation Tab
    uv_methodIndex = ui->comboUVMethod->currentIndex();
    uv_angleThreshold = ui->spinAngleThreshold->value();
    uv_preserveUVs = ui->checkPreserveUVs->isChecked();
    uv_autoPackUVs = ui->checkAutoPackUVs->isChecked();
    uv_relaxUVs = ui->checkRelaxUVs->isChecked();
    uv_rememberUV = ui->checkRememberUV->isChecked();

    // Import/Export Tab
    import_linearDeflection = ui->linearDeflectionSpinBox->value();
    import_angularDeflection = ui->angularDeflectionSpinBox->value();
    import_assimpGenNormals = ui->assimpGenNormalsCheckBox->isChecked();
    import_assimpSmoothNormals = ui->assimpSmoothNormalsCheckBox->isChecked();
    import_assimpCalcTangents = ui->assimpCalcTangentsCheckBox->isChecked();
    import_assimpOptimizeMesh = ui->assimpOptimizeMeshCheckBox->isChecked();
    import_assimpRemoveDuplicates = ui->assimpRemoveDuplicatesCheckBox->isChecked();
    import_assimpAutoOrientModel = ui->assimpAutoOrientCheckBox->isChecked();
    export_exportScene = ui->radioButtonExportScene->isChecked();
    export_exportMeshes = ui->radioButtonExportMeshes->isChecked();

    // Debug Tab
    debug_enableLogging = ui->enableLoggingCheckBox->isChecked();
    debug_enableConsoleOutput = ui->enableConsoleCheckBox->isChecked();
    debug_logLevelIndex = ui->logLevelComboBox->currentIndex();
    debug_consoleBufferLines = ui->consoleBufferLinesSpinBox->value();
    debug_profileRendering = ui->profileRenderingCheckBox->isChecked();
    debug_showTextureDebugPanel = ui->showTextureDebugPanelCheckBox->isChecked();
}

void SettingsDialog::loadSettings()
{
    blockAllChildWidgetSignals(true);

	qDebug() << "Loading settings from QSettings...";
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    int iVal = settings.value("comboBoxTheme", ui->comboBoxTheme->currentIndex()).toInt();
    ui->comboBoxTheme->setCurrentIndex(iVal);
    iVal = settings.value("comboBoxLanguage", ui->comboBoxLanguage->currentIndex()).toInt();
    ui->comboBoxLanguage->setCurrentIndex(iVal);
    bool bVal = settings.value("checkRestoreLastFile", ui->checkRestoreLastFile->isChecked()).toBool();
    ui->checkRestoreLastFile->setChecked(bVal);
    bVal = settings.value("checkTooltips", ui->checkTooltips->isChecked()).toBool();
    ui->checkTooltips->setChecked(bVal);
    bVal = settings.value("checkConfirmExit", ui->checkConfirmExit->isChecked()).toBool();
    ui->checkConfirmExit->setChecked(bVal);
	bVal = settings.value("checkTutorialLaunch", ui->checkTutorialLaunch->isChecked()).toBool();
	ui->checkTutorialLaunch->setChecked(bVal);
	iVal = settings.value("spinBoxUndoLimit", ui->spinBoxUndoLimit->value()).toInt();
	ui->spinBoxUndoLimit->setValue(iVal);
    bVal = settings.value("checkProgressiveLoading", ui->checkProgressiveLoading->isChecked()).toBool();
    ui->checkProgressiveLoading->setChecked(bVal);
    bVal = settings.value("checkAnimateProgressiveFit", ui->checkAnimateProgressiveFit->isChecked()).toBool();
    ui->checkAnimateProgressiveFit->setChecked(bVal);
    iVal = settings.value("comboProjectionMode", ui->comboProjectionMode->currentIndex()).toInt();
    ui->comboProjectionMode->setCurrentIndex(iVal);
    iVal = settings.value("comboDefaultView", ui->comboDefaultView->currentIndex()).toInt();
    ui->comboDefaultView->setCurrentIndex(iVal);
    iVal = settings.value("comboDefaultProjection", ui->comboDefaultProjection->currentIndex()).toInt();
    ui->comboDefaultProjection->setCurrentIndex(iVal);
    iVal = settings.value("comboCameraUpAxis", ui->comboCameraUpAxis->currentIndex()).toInt();
    ui->comboCameraUpAxis->setCurrentIndex(iVal);
    bVal = settings.value("checkInvertZoom", ui->checkInvertZoom->isChecked()).toBool();
    ui->checkInvertZoom->setChecked(bVal);
    iVal = settings.value("Background/StyleIndex", ui->comboBoxBackgroundStyle->currentIndex()).toInt();
    ui->comboBoxBackgroundStyle->setCurrentIndex(iVal);
    iVal = settings.value("Background/GradientStyle", ui->comboBoxGradientStyle->currentIndex()).toInt();
    ui->comboBoxGradientStyle->setCurrentIndex(iVal);
    {
        QColor topColor = settings.value("Background/TopColor", QColor(128, 128, 128)).value<QColor>();
        ui->pushButtonTopColor->setProperty("color", topColor);
        ui->pushButtonTopColor->setStyleSheet(QString("background-color: %1").arg(topColor.name()));
        QColor bottomColor = settings.value("Background/BottomColor", QColor(64, 64, 64)).value<QColor>();
        ui->pushButtonBottomColor->setProperty("color", bottomColor);
        ui->pushButtonBottomColor->setStyleSheet(QString("background-color: %1").arg(bottomColor.name()));
    }
    bVal = settings.value("showBoundingBoxCheckBox", ui->showBoundingBoxCheckBox->isChecked()).toBool();
    ui->showBoundingBoxCheckBox->setChecked(bVal);
    bVal = settings.value("showVertexNormalsCheckBox", ui->showVertexNormalsCheckBox->isChecked()).toBool();
    ui->showVertexNormalsCheckBox->setChecked(bVal);
    bVal = settings.value("showFaceNormalsCheckBox", ui->showFaceNormalsCheckBox->isChecked()).toBool();
    ui->showFaceNormalsCheckBox->setChecked(bVal);
    bVal = settings.value("showCornerTrihedronCheckBox", ui->showCornerTrihedronCheckBox->isChecked()).toBool();
    ui->showCornerTrihedronCheckBox->setChecked(bVal);
    bVal = settings.value("showViewCubeCheckBox", ui->showViewCubeCheckBox->isChecked()).toBool();
    ui->showViewCubeCheckBox->setChecked(bVal);
    iVal = settings.value("comboBoxCornerTrihedronPosition", kCornerTrihedronTopRight).toInt();
    setCornerTrihedronPositionSelection(ui->comboBoxCornerTrihedronPosition, iVal);
    refreshViewCubeAvailability();
    iVal = settings.value("fieldOfViewSpinBox", ui->fieldOfViewSpinBox->value()).toInt();
    ui->fieldOfViewSpinBox->setValue(iVal);
    bVal = settings.value("showWireframeCheckBox", ui->showWireframeCheckBox->isChecked()).toBool();
    ui->showWireframeCheckBox->setChecked(bVal);
    bVal = settings.value("showCenterTrihedronCheckBox", ui->showCenterTrihedronCheckBox->isChecked()).toBool();
    ui->showCenterTrihedronCheckBox->setChecked(bVal);
    if (ui->comboBoxHoverHighlightMode) {
        iVal = settings.value("comboBoxHoverHighlightMode", ui->comboBoxHoverHighlightMode->currentIndex()).toInt();
        ui->comboBoxHoverHighlightMode->setCurrentIndex(iVal);
    }
    bVal = settings.value("vsyncCheckBox", ui->vsyncCheckBox->isChecked()).toBool();
    ui->vsyncCheckBox->setChecked(bVal);
    {
        const QString hdriName = settings.value("comboBoxDefaultSkyboxHDRI", QString()).toString();
        const int hdriIdx = ui->comboBoxDefaultSkyboxHDRI->findData(hdriName);
        if (hdriIdx >= 0)
            ui->comboBoxDefaultSkyboxHDRI->setCurrentIndex(hdriIdx);

        const QString ldriName = settings.value("comboBoxDefaultSkyboxLDRI", QString()).toString();
        const int ldriIdx = ui->comboBoxDefaultSkyboxLDRI->findData(ldriName);
        if (ldriIdx >= 0)
            ui->comboBoxDefaultSkyboxLDRI->setCurrentIndex(ldriIdx);
    }
    iVal = settings.value("navigationModeComboBox", ui->navigationModeComboBox->currentIndex()).toInt();
    ui->navigationModeComboBox->setCurrentIndex(iVal);
    iVal = settings.value("mouseSensitivitySlider", ui->mouseSensitivitySlider->value()).toInt();
    ui->mouseSensitivitySlider->setValue(iVal);
    iVal = settings.value("wheelSensitivitySlider", ui->zoomSensitivitySlider->value()).toInt();
    ui->zoomSensitivitySlider->setValue(iVal);
    bVal = settings.value("invertYAxisCheckBox", ui->invertYAxisCheckBox->isChecked()).toBool();
    ui->invertYAxisCheckBox->setChecked(bVal);
    bVal = settings.value("smoothNavigationCheckBox", ui->smoothNavigationCheckBox->isChecked()).toBool();
    ui->smoothNavigationCheckBox->setChecked(bVal);
    iVal = settings.value("comboShadingMode", ui->comboShadingMode->currentIndex()).toInt();
    ui->comboShadingMode->setCurrentIndex(iVal);
    bVal = settings.value("checkBackfaceCulling", ui->checkBackfaceCulling->isChecked()).toBool();
    ui->checkBackfaceCulling->setChecked(bVal);
    iVal = settings.value("shaderModelComboBox", ui->shaderModelComboBox->currentIndex()).toInt();
    ui->shaderModelComboBox->setCurrentIndex(iVal);
    iVal = settings.value("shadingNormalComboBox", ui->shadingNormalComboBox->currentIndex()).toInt();
    ui->shadingNormalComboBox->setCurrentIndex(iVal);
    iVal = settings.value("msaaComboBox", ui->msaaComboBox->currentIndex()).toInt();
    ui->msaaComboBox->setCurrentIndex(iVal);
    iVal = settings.value("anisotropyComboBox", ui->anisotropyComboBox->currentIndex()).toInt();
    ui->anisotropyComboBox->setCurrentIndex(iVal);
    bVal = settings.value("enableLightingCheckBox", ui->enableLightingCheckBox->isChecked()).toBool();
    ui->enableLightingCheckBox->setChecked(bVal);
    bVal = settings.value("enableShadowsCheckBox", ui->enableShadowsCheckBox->isChecked()).toBool();
    ui->enableShadowsCheckBox->setChecked(bVal);
    iVal = settings.value("ambientLightSlider", ui->ambientLightSlider->value()).toInt();
    ui->ambientLightSlider->setValue(iVal);
    iVal = settings.value("diffuseLightSlider", ui->diffuseLightSlider->value()).toInt();
    ui->diffuseLightSlider->setValue(iVal);
    iVal = settings.value("specularLightSlider", ui->specularLightSlider->value()).toInt();
    ui->specularLightSlider->setValue(iVal);
    QString sval = settings.value("lineEditTextureDir", ui->lineEditTextureDir->text()).toString();
    ui->lineEditTextureDir->setText(sval);
    sval = settings.value("comboBoxDefaultMaterial", ui->comboBoxDefaultMaterial->currentData().toString()).toString();
    {
        const int idx = ui->comboBoxDefaultMaterial->findData(sval);
        ui->comboBoxDefaultMaterial->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    iVal = settings.value("UVMethod", ui->comboUVMethod->currentIndex()).toInt();
    ui->comboUVMethod->setCurrentIndex(iVal);
    double dVal = settings.value("angleThreshold", ui->spinAngleThreshold->value()).toDouble();
    ui->spinAngleThreshold->setValue(dVal);
    bVal = settings.value("preserveAspectRatio", ui->checkPreserveUVs->isChecked()).toBool();
    ui->checkPreserveUVs->setChecked(bVal);
    bVal = settings.value("enablePacking", ui->checkAutoPackUVs->isChecked()).toBool();
    ui->checkAutoPackUVs->setChecked(bVal);
    bVal = settings.value("enableRelaxation", ui->checkRelaxUVs->isChecked()).toBool();
    ui->checkRelaxUVs->setChecked(bVal);
    bVal = settings.value("RememberUVMethod", ui->checkRememberUV->isChecked()).toBool();
    ui->checkRememberUV->setChecked(bVal);
    dVal = settings.value("linearDeflectionSpinBox", ui->linearDeflectionSpinBox->value()).toDouble();
    ui->linearDeflectionSpinBox->setValue(dVal);
    dVal = settings.value("angularDeflectionSpinBox", ui->angularDeflectionSpinBox->value()).toDouble();
    ui->angularDeflectionSpinBox->setValue(dVal);
    bVal = settings.value("assimpGenNormalsCheckBox", ui->assimpGenNormalsCheckBox->isChecked()).toBool();
    ui->assimpGenNormalsCheckBox->setChecked(bVal);
    bVal = settings.value("assimpSmoothNormalsCheckBox", ui->assimpSmoothNormalsCheckBox->isChecked()).toBool();
    ui->assimpSmoothNormalsCheckBox->setChecked(bVal);
    bVal = settings.value("assimpCalcTangentsCheckBox", ui->assimpCalcTangentsCheckBox->isChecked()).toBool();
    ui->assimpCalcTangentsCheckBox->setChecked(bVal);
    bVal = settings.value("assimpOptimizeMeshCheckBox", ui->assimpOptimizeMeshCheckBox->isChecked()).toBool();
    ui->assimpOptimizeMeshCheckBox->setChecked(bVal);
    bVal = settings.value("assimpRemoveDuplicatesCheckBox", ui->assimpRemoveDuplicatesCheckBox->isChecked()).toBool();
    ui->assimpRemoveDuplicatesCheckBox->setChecked(bVal);
    bVal = settings.value("assimpAutoOrientCheckBox", ui->assimpAutoOrientCheckBox->isChecked()).toBool();
    ui->assimpAutoOrientCheckBox->setChecked(bVal);
	bVal = settings.value("radioButtonExportScene", ui->radioButtonExportScene->isChecked()).toBool();
	ui->radioButtonExportScene->setChecked(bVal);
	bVal = settings.value("radioButtonExportMeshes", ui->radioButtonExportMeshes->isChecked()).toBool();
	ui->radioButtonExportMeshes->setChecked(bVal);
    bVal = settings.value("enableLoggingCheckBox", ui->enableLoggingCheckBox->isChecked()).toBool();
    ui->enableLoggingCheckBox->setChecked(bVal);
	bVal = settings.value("enableConsoleCheckBox", ui->enableConsoleCheckBox->isChecked()).toBool();
	ui->enableConsoleCheckBox->setChecked(bVal);
    iVal = settings.value("logLevelComboBox", ui->logLevelComboBox->currentIndex()).toInt();
    ui->logLevelComboBox->setCurrentIndex(iVal);
    iVal = settings.value("consoleBufferLinesSpinBox", ui->consoleBufferLinesSpinBox->value()).toInt();
    ui->consoleBufferLinesSpinBox->setValue(iVal);
    bVal = settings.value("profileRenderingCheckBox", ui->profileRenderingCheckBox->isChecked()).toBool();
    ui->profileRenderingCheckBox->setChecked(bVal);
    bVal = settings.value("showTextureDebugPanelCheckBox", ui->showTextureDebugPanelCheckBox->isChecked()).toBool();
    ui->showTextureDebugPanelCheckBox->setChecked(bVal);

    blockAllChildWidgetSignals(false);
    syncStateFromUi();
    captureAppliedState();
}

void SettingsDialog::restoreDefaults()
{
    qDebug() << "Restoring default settings...";
    // Helper lambda to safely restore widget values
    auto restoreWidget = [](QWidget* widget, const QVariant& defaultValue) {
        if (!widget || !defaultValue.isValid()) return;

        // Handle different widget types appropriately
        if (auto* comboBox = qobject_cast<QComboBox*>(widget))
        {
            int index = defaultValue.toInt();
            if (index >= 0 && index < comboBox->count())
            {
                comboBox->setCurrentIndex(index);
            }
        }
        else if (auto* checkBox = qobject_cast<QCheckBox*>(widget))
        {
            checkBox->setChecked(defaultValue.toBool());
        }
        else if (auto* spinBox = qobject_cast<QSpinBox*>(widget))
        {
            spinBox->setValue(defaultValue.toInt());
        }
        else if (auto* doubleSpinBox = qobject_cast<QDoubleSpinBox*>(widget))
        {
            doubleSpinBox->setValue(defaultValue.toDouble());
        }
        else if (auto* slider = qobject_cast<QSlider*>(widget))
        {
            slider->setValue(defaultValue.toInt());
        }
        else if (auto* lineEdit = qobject_cast<QLineEdit*>(widget))
        {
            lineEdit->setText(defaultValue.toString());
        }
        else if (auto* pushButton = qobject_cast<QPushButton*>(widget))
        {
            // For color buttons, restore the color property
            if (widget->objectName().contains("Color"))
            {
                QColor color = defaultValue.value<QColor>();
                if (color.isValid())
                {
                    widget->setStyleSheet(QString("background-color: %1").arg(color.name()));
                    widget->setProperty("color", color);
                }
            }
        }
        };

    // List of all widgets that need to be restored
    QList<QWidget*> widgetsToRestore = {
        ui->comboBoxTheme, ui->comboBoxLanguage,
		ui->checkRestoreLastFile, ui->checkTooltips, ui->checkConfirmExit, ui->checkTutorialLaunch, ui->spinBoxUndoLimit,
		ui->checkProgressiveLoading,
        ui->comboProjectionMode, ui->comboDefaultView, ui->comboDefaultProjection,
        ui->comboCameraUpAxis,
        ui->checkInvertZoom,
        ui->comboBoxBackgroundStyle, ui->pushButtonTopColor, ui->pushButtonBottomColor,
        ui->comboBoxGradientStyle, ui->comboBoxDefaultSkyboxHDRI, ui->comboBoxDefaultSkyboxLDRI, ui->vsyncCheckBox,
        ui->showBoundingBoxCheckBox, ui->showVertexNormalsCheckBox,
        ui->showFaceNormalsCheckBox, ui->showCornerTrihedronCheckBox,
        ui->showViewCubeCheckBox,
        ui->comboBoxCornerTrihedronPosition, ui->fieldOfViewSpinBox,
        ui->showWireframeCheckBox, ui->showCenterTrihedronCheckBox,
        ui->navigationModeComboBox, ui->mouseSensitivitySlider, ui->zoomSensitivitySlider,
        ui->invertYAxisCheckBox, ui->smoothNavigationCheckBox, ui->comboShadingMode,
        ui->checkBackfaceCulling, ui->shaderModelComboBox,
        ui->shadingNormalComboBox,
        ui->msaaComboBox, ui->anisotropyComboBox, ui->enableLightingCheckBox,
        ui->enableShadowsCheckBox, ui->ambientLightSlider, ui->diffuseLightSlider,
        ui->specularLightSlider, ui->lineEditTextureDir, ui->comboBoxDefaultMaterial,
        ui->comboUVMethod, ui->spinAngleThreshold, ui->checkPreserveUVs,
        ui->checkAutoPackUVs, ui->checkRelaxUVs,
        ui->checkRememberUV, ui->buttonResetUVPrompt,
        ui->linearDeflectionSpinBox, ui->angularDeflectionSpinBox,
        ui->assimpGenNormalsCheckBox, ui->assimpSmoothNormalsCheckBox,
        ui->assimpCalcTangentsCheckBox, ui->assimpOptimizeMeshCheckBox, ui->assimpRemoveDuplicatesCheckBox,
		ui->assimpAutoOrientCheckBox,
        ui->radioButtonExportScene, ui->radioButtonExportMeshes,
        ui->enableLoggingCheckBox, ui->enableConsoleCheckBox, ui->logLevelComboBox,
        ui->consoleBufferLinesSpinBox,
        ui->profileRenderingCheckBox,
        ui->showTextureDebugPanelCheckBox,
        ui->clearCacheButton
    };

    // Restore all widgets using the helper function
    for (QWidget* widget : widgetsToRestore)
    {
        if (widget)
        {
            restoreWidget(widget, widget->property("defaultValue"));
        }
    }

    qDebug() << "Default settings restored successfully.";
}

// General tab
void SettingsDialog::on_comboBoxTheme_currentIndexChanged()
{
    general_themeIndex = ui->comboBoxTheme->currentIndex();
}

void SettingsDialog::on_comboBoxLanguage_currentIndexChanged()
{
    general_languageIndex = ui->comboBoxLanguage->currentIndex();    
}

void SettingsDialog::on_checkRestoreLastFile_stateChanged()
{
    general_restoreLastFile = ui->checkRestoreLastFile->isChecked();
}

void SettingsDialog::on_checkTooltips_stateChanged()
{
    general_showTooltips = ui->checkTooltips->isChecked();
}

void SettingsDialog::on_checkConfirmExit_stateChanged()
{
    general_confirmExit = ui->checkConfirmExit->isChecked();
}

void SettingsDialog::on_checkTutorialLaunch_stateChanged()
{
	general_showTutorialLauncher = ui->checkTutorialLaunch->isChecked();
}

void SettingsDialog::on_spinBoxUndoLimit_valueChanged()
{
	general_undoLimit = ui->spinBoxUndoLimit->value();
}

// Camera tab
void SettingsDialog::on_comboProjectionMode_currentIndexChanged()
{
    camera_projectionModeIndex = ui->comboProjectionMode->currentIndex();
}

void SettingsDialog::on_comboDefaultView_currentIndexChanged()
{
    camera_defaultViewIndex = ui->comboDefaultView->currentIndex();
}

void SettingsDialog::on_comboDefaultProjection_currentIndexChanged()
{
    camera_defaultProjectionIndex = ui->comboDefaultProjection->currentIndex();
}

void SettingsDialog::on_comboCameraUpAxis_currentIndexChanged()
{
    camera_defaultUpAxisIndex = ui->comboCameraUpAxis->currentIndex();
}

void SettingsDialog::on_checkInvertZoom_stateChanged()
{
    camera_invertZoom = ui->checkInvertZoom->isChecked();
}

// Background tab
void SettingsDialog::on_comboBoxBackgroundStyle_currentIndexChanged()
{
    background_styleIndex = ui->comboBoxBackgroundStyle->currentIndex();
}

void SettingsDialog::on_pushButtonTopColor_clicked()
{
    QColor current = ui->pushButtonTopColor->property("color").value<QColor>();
    QColor chosen = QColorDialog::getColor(current.isValid() ? current : QColor(128, 128, 128), this, tr("Select Top Color"));
    if (chosen.isValid())
    {
        ui->pushButtonTopColor->setProperty("color", chosen);
        ui->pushButtonTopColor->setStyleSheet(QString("background-color: %1").arg(chosen.name()));
        background_topColor = chosen;
    }
}

void SettingsDialog::on_pushButtonBottomColor_clicked()
{
    QColor current = ui->pushButtonBottomColor->property("color").value<QColor>();
    QColor chosen = QColorDialog::getColor(current.isValid() ? current : QColor(64, 64, 64), this, tr("Select Bottom Color"));
    if (chosen.isValid())
    {
        ui->pushButtonBottomColor->setProperty("color", chosen);
        ui->pushButtonBottomColor->setStyleSheet(QString("background-color: %1").arg(chosen.name()));
        background_bottomColor = chosen;
    }
}

void SettingsDialog::on_comboBoxGradientStyle_currentIndexChanged()
{
    background_gradientStyleIndex = ui->comboBoxGradientStyle->currentIndex();
}

// Display tab
void SettingsDialog::on_showBoundingBoxCheckBox_stateChanged()
{
    display_showBoundingBox = ui->showBoundingBoxCheckBox->isChecked();
}

void SettingsDialog::on_showCornerTrihedronCheckBox_stateChanged()
{
    display_showCornerTrihedron = ui->showCornerTrihedronCheckBox->isChecked();
}

void SettingsDialog::on_showViewCubeCheckBox_stateChanged()
{
    display_showViewCube = ui->showViewCubeCheckBox->isChecked();
}

void SettingsDialog::on_comboBoxCornerTrihedronPosition_currentIndexChanged()
{
    display_cornerTrihedronPosition = currentCornerTrihedronPositionValue(ui->comboBoxCornerTrihedronPosition);
    refreshViewCubeAvailability();
    display_showViewCube = ui->showViewCubeCheckBox->isChecked();
}

void SettingsDialog::on_fieldOfViewSpinBox_valueChanged()
{
    display_fieldOfView = ui->fieldOfViewSpinBox->value();
}

void SettingsDialog::on_showVertexNormalsCheckBox_stateChanged()
{
    display_showVertexNormals = ui->showVertexNormalsCheckBox->isChecked();
}

void SettingsDialog::on_showFaceNormalsCheckBox_stateChanged()
{
    display_showFaceNormals = ui->showFaceNormalsCheckBox->isChecked();
}

void SettingsDialog::on_showWireframeCheckBox_stateChanged()
{
    display_showWireframe = ui->showWireframeCheckBox->isChecked();
}

void SettingsDialog::on_showCenterTrihedronCheckBox_stateChanged()
{
    display_showCenterTrihedron = ui->showCenterTrihedronCheckBox->isChecked();
}

// Navigation group
void SettingsDialog::on_navigationModeComboBox_currentIndexChanged()
{
    navigation_modeIndex = ui->navigationModeComboBox->currentIndex();
}

void SettingsDialog::on_mouseSensitivitySlider_valueChanged()
{
    navigation_mouseSensitivity = ui->mouseSensitivitySlider->value();
}

void SettingsDialog::on_zoomSensitivitySlider_valueChanged()
{
    navigation_wheelSensitivity = ui->zoomSensitivitySlider->value();
}

void SettingsDialog::on_invertYAxisCheckBox_stateChanged()
{
    navigation_invertYAxis = ui->invertYAxisCheckBox->isChecked();
}

void SettingsDialog::on_smoothNavigationCheckBox_stateChanged()
{
    navigation_smoothNavigation = ui->smoothNavigationCheckBox->isChecked();
}

// Rendering tab
void SettingsDialog::on_comboShadingMode_currentIndexChanged()
{
    rendering_shadingModeIndex = ui->comboShadingMode->currentIndex();
}

void SettingsDialog::on_checkBackfaceCulling_stateChanged()
{
    rendering_backfaceCulling = ui->checkBackfaceCulling->isChecked();
}

void SettingsDialog::on_shaderModelComboBox_currentIndexChanged()
{
    rendering_shaderModelIndex = ui->shaderModelComboBox->currentIndex();
}

void SettingsDialog::on_shadingNormalComboBox_currentIndexChanged()
{
    rendering_shadingNormalIndex = ui->shadingNormalComboBox->currentIndex();
}

void SettingsDialog::on_msaaComboBox_currentIndexChanged()
{
    rendering_msaaIndex = ui->msaaComboBox->currentIndex();    
    QMessageBox::information(this, tr("MSAA Change"), tr("Please restart the application for the MSAA change to take effect."));
}

void SettingsDialog::on_anisotropyComboBox_currentIndexChanged()
{
    rendering_anisotropyIndex = ui->anisotropyComboBox->currentIndex();
}

// Lighting
void SettingsDialog::on_enableLightingCheckBox_stateChanged()
{
    lighting_enableLighting = ui->enableLightingCheckBox->isChecked();
}

void SettingsDialog::on_enableShadowsCheckBox_stateChanged()
{
    lighting_enableShadows = ui->enableShadowsCheckBox->isChecked();
}

void SettingsDialog::on_ambientLightSlider_valueChanged()
{
    lighting_ambient = ui->ambientLightSlider->value();
}

void SettingsDialog::on_diffuseLightSlider_valueChanged()
{
    lighting_diffuse = ui->diffuseLightSlider->value();
}

void SettingsDialog::on_specularLightSlider_valueChanged()
{
    lighting_specular = ui->specularLightSlider->value();
}

// Materials
void SettingsDialog::on_comboBoxDefaultMaterial_currentIndexChanged()
{
    materials_defaultMaterialKey = ui->comboBoxDefaultMaterial->currentData().toString();
}

void SettingsDialog::on_lineEditTextureDir_textChanged()
{
    materials_textureDir = ui->lineEditTextureDir->text();
}

// UV Generation Tab
void SettingsDialog::on_comboUVMethod_currentIndexChanged()
{
    uv_methodIndex = ui->comboUVMethod->currentIndex();
}

void SettingsDialog::on_spinAngleThreshold_valueChanged()
{
    uv_angleThreshold = ui->spinAngleThreshold->value();
}

void SettingsDialog::on_checkPreserveUVs_stateChanged()
{
    uv_preserveUVs = ui->checkPreserveUVs->isChecked();
}

void SettingsDialog::on_checkAutoPackUVs_stateChanged()
{
    uv_autoPackUVs = ui->checkAutoPackUVs->isChecked();
}

void SettingsDialog::on_checkRelaxUVs_stateChanged()
{
    uv_relaxUVs = ui->checkRelaxUVs->isChecked();
}

void SettingsDialog::on_checkRememberUV_stateChanged()
{
    uv_rememberUV = ui->checkRememberUV->isChecked();
}

void SettingsDialog::on_buttonResetUVPrompt_clicked()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.remove("UVMethod");
    settings.remove("RememberUVMethod");

    qDebug() << "UV Prompt settings have been reset.";
    QMessageBox::information(this, tr("Settings Reset"), tr("UV Prompt settings have been cleared."));
}

// Import/Export Tab - OpenCascade
void SettingsDialog::on_linearDeflectionSpinBox_valueChanged()
{
    import_linearDeflection = ui->linearDeflectionSpinBox->value();
}

void SettingsDialog::on_angularDeflectionSpinBox_valueChanged()
{
    import_angularDeflection = ui->angularDeflectionSpinBox->value();
}

// Import/Export Tab - Assimp
void SettingsDialog::on_assimpGenNormalsCheckBox_stateChanged()
{
    import_assimpGenNormals = ui->assimpGenNormalsCheckBox->isChecked();
}

void SettingsDialog::on_assimpSmoothNormalsCheckBox_stateChanged()
{
    import_assimpSmoothNormals = ui->assimpSmoothNormalsCheckBox->isChecked();
}

void SettingsDialog::on_assimpCalcTangentsCheckBox_stateChanged()
{
    import_assimpCalcTangents = ui->assimpCalcTangentsCheckBox->isChecked();
}

void SettingsDialog::on_assimpOptimizeMeshCheckBox_stateChanged()
{
    import_assimpOptimizeMesh = ui->assimpOptimizeMeshCheckBox->isChecked();
}

void SettingsDialog::on_assimpRemoveDuplicatesCheckBox_stateChanged()
{
    import_assimpRemoveDuplicates = ui->assimpRemoveDuplicatesCheckBox->isChecked();
}

void SettingsDialog::on_assimpAutoOrientCheckBox_stateChanged()
{
	import_assimpAutoOrientModel = ui->assimpAutoOrientCheckBox->isChecked();
}

void SettingsDialog::on_radioButtonExportScene_toggled(bool checked)
{
	export_exportScene = checked;
}

void SettingsDialog::on_radioButtonExportMeshes_toggled(bool checked)
{
	export_exportMeshes = checked;
}

void SettingsDialog::on_checkProgressiveLoading_stateChanged()
{
    general_progressiveLoading = ui->checkProgressiveLoading->isChecked();
}

void SettingsDialog::on_checkAnimateProgressiveFit_stateChanged()
{
    general_animateProgressiveFit = ui->checkAnimateProgressiveFit->isChecked();
}

void SettingsDialog::on_vsyncCheckBox_stateChanged()
{
    display_vsync = ui->vsyncCheckBox->isChecked();
}

void SettingsDialog::on_comboBoxDefaultSkyboxHDRI_currentIndexChanged()
{
    display_defaultSkyboxHDRI = ui->comboBoxDefaultSkyboxHDRI->currentData().toString();
}

void SettingsDialog::on_comboBoxDefaultSkyboxLDRI_currentIndexChanged()
{
    display_defaultSkyboxLDRI = ui->comboBoxDefaultSkyboxLDRI->currentData().toString();
}

// Debug Tab
void SettingsDialog::on_enableLoggingCheckBox_stateChanged()
{
    debug_enableLogging = ui->enableLoggingCheckBox->isChecked();
}

void SettingsDialog::on_enableConsoleCheckBox_stateChanged()
{
	debug_enableConsoleOutput = ui->enableConsoleCheckBox->isChecked();
}

void SettingsDialog::on_logLevelComboBox_currentIndexChanged()
{
    debug_logLevelIndex = ui->logLevelComboBox->currentIndex();
}

void SettingsDialog::on_consoleBufferLinesSpinBox_valueChanged()
{
    debug_consoleBufferLines = ui->consoleBufferLinesSpinBox->value();
}

void SettingsDialog::on_profileRenderingCheckBox_stateChanged()
{
    debug_profileRendering = ui->profileRenderingCheckBox->isChecked();
}

void SettingsDialog::on_showTextureDebugPanelCheckBox_stateChanged()
{
    debug_showTextureDebugPanel = ui->showTextureDebugPanelCheckBox->isChecked();
    emit textureDebugPanelVisibilityChanged(debug_showTextureDebugPanel);
}

void SettingsDialog::on_clearCacheButton_clicked()
{
    emit clearCachesRequested();
}

