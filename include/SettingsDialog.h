#pragma once


#include <QDialog>
#include <QSettings>
#include "ThemeManager.h"
#include "SelectionManager.h"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    void retranslateUI();

    void blockAllChildWidgetSignals(bool block);

    void setMaxMSAASamples(int maxSamples);
	void setMaxAnisotropy(int maxAnisotropy);

public:
    // General tab
    int generalThemeIndex() const { return general_themeIndex; }
    int generalLanguageIndex() const { return general_languageIndex; }
    bool generalRestoreLastFile() const { return general_restoreLastFile; }
    bool generalShowTooltips() const { return general_showTooltips; }
    bool generalConfirmExit() const { return general_confirmExit; }
    bool generalProgressiveLoading() const { return general_progressiveLoading; }
    bool generalAnimateProgressiveFit() const { return general_animateProgressiveFit; }

    // Camera tab
    int cameraProjectionModeIndex() const { return camera_projectionModeIndex; }
    int cameraDefaultViewIndex() const { return camera_defaultViewIndex; }
    int cameraDefaultProjectionIndex() const { return camera_defaultProjectionIndex; }
    int cameraDefaultUpAxisIndex() const { return camera_defaultUpAxisIndex; }
    bool cameraInvertZoom() const { return camera_invertZoom; }

    // Background tab
    int backgroundStyleIndex() const { return background_styleIndex; }
    int backgroundGradientStyleIndex() const { return background_gradientStyleIndex; }
    QColor backgroundTopColor() const { return background_topColor; }
    QColor backgroundBottomColor() const { return background_bottomColor; }

    // Display tab
    bool displayShowBoundingBox() const { return display_showBoundingBox; }
    bool displayShowCornerTrihedron() const { return display_showCornerTrihedron; }
    bool displayShowViewCube() const { return display_showViewCube; }
    int  displayCornerTrihedronPosition() const { return display_cornerTrihedronPosition; }
    bool displayShowVertexNormals() const { return display_showVertexNormals; }
    bool displayShowFaceNormals() const { return display_showFaceNormals; }
    bool displayShowWireframe() const { return display_showWireframe; }
    int displayFieldOfView() const { return display_fieldOfView; }
    bool displayShowCenterTrihedron() const { return display_showCenterTrihedron; }
    HoverHighlightMode displayHoverHighlightMode() const { return display_hoverHighlightMode; }
    bool displayVsync() const { return display_vsync; }
    QString displayDefaultSkyboxHDRI() const { return display_defaultSkyboxHDRI; }
    QString displayDefaultSkyboxLDRI() const { return display_defaultSkyboxLDRI; }

    // Navigation group
    int navigationModeIndex() const { return navigation_modeIndex; }
    int navigationMouseSensitivity() const { return navigation_mouseSensitivity; }
    int navigationWheelSensitivity() const { return navigation_wheelSensitivity; }
    bool navigationInvertYAxis() const { return navigation_invertYAxis; }
    bool navigationSmoothNavigation() const { return navigation_smoothNavigation; }

    // Rendering tab
    int renderingShadingModeIndex() const { return rendering_shadingModeIndex; }
    bool renderingBackfaceCulling() const { return rendering_backfaceCulling; }
    int renderingShaderModelIndex() const { return rendering_shaderModelIndex; }
    int renderingShadingNormalIndex() const { return rendering_shadingNormalIndex; }
    int renderingMsaaIndex() const { return rendering_msaaIndex; }
    int renderingAnisotropyIndex() const { return rendering_anisotropyIndex; }

    // Lighting
    bool lightingEnableLighting() const { return lighting_enableLighting; }
    bool lightingEnableShadows() const { return lighting_enableShadows; }
    int lightingAmbient() const { return lighting_ambient; }
    int lightingDiffuse() const { return lighting_diffuse; }
    int lightingSpecular() const { return lighting_specular; }

    // Materials
    QString materialsDefaultMaterialKey() const { return materials_defaultMaterialKey; }
    QString materialsTextureDir() const { return materials_textureDir; }

    // UV Generation Tab
    int uvMethodIndex() const { return uv_methodIndex; }
    double uvAngleThreshold() const { return uv_angleThreshold; }
    bool uvPreserveUVs() const { return uv_preserveUVs; }
    bool uvAutoPackUVs() const { return uv_autoPackUVs; }
    bool uvRelaxUVs() const { return uv_relaxUVs; }
    bool uvRememberUV() const { return uv_rememberUV; }

    // Import/Export Tab
    // OpenCascade
    double importLinearDeflection() const { return import_linearDeflection; }
    double importAngularDeflection() const { return import_angularDeflection; }

    // Assimp
    bool importAssimpGenNormals() const { return import_assimpGenNormals; }
    bool importAssimpSmoothNormals() const { return import_assimpSmoothNormals; }
    bool importAssimpCalcTangents() const { return import_assimpCalcTangents; }
    bool importAssimpOptimizeMesh() const { return import_assimpOptimizeMesh; }
    bool importAssimpRemoveDuplicates() const { return import_assimpRemoveDuplicates; }

    // Debug Tab
    bool debugEnableLogging() const { return debug_enableLogging; }
    int debugLogLevelIndex() const { return debug_logLevelIndex; }
    int debugConsoleBufferLines() const { return debug_consoleBufferLines; }
    bool debugProfileRendering() const { return debug_profileRendering; }
    bool debugShowTextureDebugPanel() const { return debug_showTextureDebugPanel; }

    ThemeManager* themeManager() const { return _themeManager; }


signals:
	void settingsChanged(); // Signal to notify that settings have changed
	void textureDebugPanelVisibilityChanged(bool enabled);
	void clearCachesRequested();

private slots:   
    void onOkClicked();
    void onCancelClicked();
    void onApplyClicked();
    void onRestoreDefaults();

    void on_comboBoxTheme_currentIndexChanged();
    void on_comboBoxLanguage_currentIndexChanged();
    void on_checkRestoreLastFile_stateChanged();
    void on_checkTooltips_stateChanged();
    void on_checkConfirmExit_stateChanged();
	void on_checkTutorialLaunch_stateChanged();
	void on_spinBoxUndoLimit_valueChanged();
    void on_comboProjectionMode_currentIndexChanged();
    void on_comboDefaultView_currentIndexChanged();
    void on_comboDefaultProjection_currentIndexChanged();
    void on_comboCameraUpAxis_currentIndexChanged();
    void on_checkInvertZoom_stateChanged();
    void on_comboBoxBackgroundStyle_currentIndexChanged();
    void on_pushButtonTopColor_clicked();
    void on_pushButtonBottomColor_clicked();
    void on_comboBoxGradientStyle_currentIndexChanged();
    void on_comboBoxDefaultSkyboxHDRI_currentIndexChanged();
    void on_comboBoxDefaultSkyboxLDRI_currentIndexChanged();
    void on_showBoundingBoxCheckBox_stateChanged();
    void on_showCornerTrihedronCheckBox_stateChanged();
    void on_showViewCubeCheckBox_stateChanged();
    void on_comboBoxCornerTrihedronPosition_currentIndexChanged();
    void on_fieldOfViewSpinBox_valueChanged();
    void on_showVertexNormalsCheckBox_stateChanged();
    void on_showFaceNormalsCheckBox_stateChanged();
    void on_showWireframeCheckBox_stateChanged();
    void on_showCenterTrihedronCheckBox_stateChanged();
    void on_navigationModeComboBox_currentIndexChanged();
    void on_mouseSensitivitySlider_valueChanged();
    void on_zoomSensitivitySlider_valueChanged();
    void on_invertYAxisCheckBox_stateChanged();
    void on_smoothNavigationCheckBox_stateChanged();
    void on_comboShadingMode_currentIndexChanged();
    void on_checkBackfaceCulling_stateChanged();
    void on_shaderModelComboBox_currentIndexChanged();
    void on_shadingNormalComboBox_currentIndexChanged();
    void on_msaaComboBox_currentIndexChanged();
    void on_anisotropyComboBox_currentIndexChanged();
    void on_enableLightingCheckBox_stateChanged();
    void on_enableShadowsCheckBox_stateChanged();
    void on_ambientLightSlider_valueChanged();
    void on_diffuseLightSlider_valueChanged();
    void on_specularLightSlider_valueChanged();
    void on_lineEditTextureDir_textChanged();
    void on_comboBoxDefaultMaterial_currentIndexChanged();
    void on_comboUVMethod_currentIndexChanged();
    void on_spinAngleThreshold_valueChanged();
    void on_checkPreserveUVs_stateChanged();
    void on_checkAutoPackUVs_stateChanged();
    void on_checkRelaxUVs_stateChanged();
    void on_checkRememberUV_stateChanged();
    void on_buttonResetUVPrompt_clicked();
    void on_linearDeflectionSpinBox_valueChanged();
    void on_angularDeflectionSpinBox_valueChanged();
    void on_assimpGenNormalsCheckBox_stateChanged();
    void on_assimpSmoothNormalsCheckBox_stateChanged();
    void on_assimpCalcTangentsCheckBox_stateChanged();
    void on_assimpOptimizeMeshCheckBox_stateChanged();
    void on_assimpRemoveDuplicatesCheckBox_stateChanged();
	void on_assimpAutoOrientCheckBox_stateChanged();
	void on_radioButtonExportScene_toggled(bool checked);
    void on_radioButtonExportMeshes_toggled(bool checked);
    void on_checkProgressiveLoading_stateChanged();
    void on_checkAnimateProgressiveFit_stateChanged();
    void on_vsyncCheckBox_stateChanged();
    void on_enableLoggingCheckBox_stateChanged();
	void on_enableConsoleCheckBox_stateChanged();
    void on_logLevelComboBox_currentIndexChanged();
    void on_consoleBufferLinesSpinBox_valueChanged();
    void on_profileRenderingCheckBox_stateChanged();
    void on_showTextureDebugPanelCheckBox_stateChanged();
    void on_clearCacheButton_clicked();
    void restoreDefaults();

private:
    void captureAppliedState();
    void syncStateFromUi();
    void updateSettingsHint();
    void refreshViewCubeAvailability();
    void setDefaultValues();
    void applySettings();    
    void loadSettings();

private:

    // General tab
    int general_themeIndex = 0;
    int general_languageIndex = 0;
    bool general_restoreLastFile = false;
    bool general_showTooltips = true;
    bool general_confirmExit = false;
    bool general_showTutorialLauncher = true;
	int general_undoLimit = 50;
    bool general_progressiveLoading = false;
    bool general_animateProgressiveFit = true;

    // Camera tab
    int camera_projectionModeIndex = 0;
    int camera_defaultViewIndex = 0;
    int camera_defaultProjectionIndex = 0;
    int camera_defaultUpAxisIndex = 0;
    bool camera_invertZoom = false;

    // Background tab
    int background_styleIndex = 0;
    int background_gradientStyleIndex = 0;
    // For color buttons, use QColor
    QColor background_topColor = Qt::white;
    QColor background_bottomColor = Qt::white;

    // Display tab
    bool display_showBoundingBox = true;
    bool display_showCornerTrihedron = true;
    bool display_showViewCube = true;
    int display_cornerTrihedronPosition = 1;
    bool display_showVertexNormals = true;
    bool display_showFaceNormals = true;
    bool display_showWireframe = true;
    int display_fieldOfView = 60;
    bool display_showCenterTrihedron = false;
    HoverHighlightMode display_hoverHighlightMode = HoverHighlightMode::RaycastOnly;
    bool display_vsync = true;
    QString display_defaultSkyboxHDRI; // "" = no preference; matched by preset folder name
    QString display_defaultSkyboxLDRI; // "" = no preference; matched by preset folder name

    // Navigation group
    int navigation_modeIndex = 0;
    int navigation_mouseSensitivity = 5;
    int navigation_wheelSensitivity = 5;
    bool navigation_invertYAxis = false;
    bool navigation_smoothNavigation = true;

    // Rendering tab
    int rendering_shadingModeIndex = 0;
    bool rendering_backfaceCulling = false;
    int rendering_shaderModelIndex = 0;
    int rendering_shadingNormalIndex = 0;
    int rendering_msaaIndex = 0;
    int rendering_anisotropyIndex = 0;

    // Lighting
    bool lighting_enableLighting = true;
    bool lighting_enableShadows = false;
    int lighting_ambient = 20;
    int lighting_diffuse = 80;
    int lighting_specular = 50;

    // Materials
    QString materials_defaultMaterialKey; // "" = Default (Neutral); see MaterialProcessor::setDefaultMaterial
    QString materials_textureDir;

    // UV Generation Tab
    int uv_methodIndex = 0;
    double uv_angleThreshold = 60.0;
    bool uv_preserveUVs = true;
    bool uv_autoPackUVs = true;
    bool uv_relaxUVs = false;
    bool uv_rememberUV = false;

    // Import/Export Tab
    // OpenCascade
    double import_linearDeflection = 0.1;
    double import_angularDeflection = 0.3;

    // Assimp
    bool import_assimpGenNormals = true;
    bool import_assimpSmoothNormals = true;
    bool import_assimpCalcTangents = true;
    bool import_assimpOptimizeMesh = true;
    bool import_assimpRemoveDuplicates = true;
	bool import_assimpAutoOrientModel = true;

    bool export_exportScene = true;
    bool export_exportMeshes = false;

    // Debug Tab
    bool debug_enableLogging = false;
	bool debug_enableConsoleOutput = false;
    int debug_logLevelIndex = 0;
    int debug_consoleBufferLines = 20000;
    bool debug_profileRendering = false;
    bool debug_showTextureDebugPanel = false;

	ThemeManager* _themeManager;

    int _appliedThemeIndex = 0;
    int _appliedLanguageIndex = 0;
    bool _appliedDebugEnableLogging = false;
    bool _appliedDebugEnableConsoleOutput = false;
    int _appliedDebugLogLevelIndex = 0;
    int _appliedDebugConsoleBufferLines = 20000;

    Ui::SettingsDialog *ui;	
};
