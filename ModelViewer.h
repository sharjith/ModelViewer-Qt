#ifndef __MODELVIEWER_H__
#define __MODELVIEWER_H__

#include "ui_ModelViewer.h"

#include <GLWidget.h>

class SphericalHarmonicsEditor;
class ModelViewer : public QWidget, public Ui::ModelViewer
{
	Q_OBJECT
public:
	ModelViewer(QWidget* parent = 0);
	~ModelViewer();

	GLWidget* getGLView() const { return _glWidget; }

	void setMaterialProps(const GLMaterialProps& mat);
	void setTransformation();
	void resetTransformation();

	QListWidget* getListModel() { return listWidgetModel; }

    void updateTransformationValues();
    void resetTransformationValues();

public slots:
	void updateDisplayList();
	void showSelectedItems();
	void hideSelectedItems();
	void deleteSelectedItems();
	void displaySelectedMeshInfo();
	void showObjectDisplayList();
	void showObjectsPropertiesPage();
	void showEnvironmentPage();
	void showTransformationsPage();
    void clickMultiViewButton();

private slots:
	void on_checkTexture_toggled(bool checked);
	void on_pushButtonTexture_clicked();
	void on_pushButtonDefaultLights_clicked();
	void on_pushButtonDefaultMatls_clicked();
	void on_pushButtonApplyTransformations_clicked();
	void on_pushButtonResetTransformations_clicked();

    void on_toolButtonFitAll_clicked();
	void on_toolButtonWindowZoom_clicked(bool checked);
    void on_toolButtonTopView_clicked();
    void on_toolButtonBottomView_clicked();
    void on_toolButtonLeftView_clicked();
    void on_toolButtonRightView_clicked();
    void on_toolButtonFrontView_clicked();
    void on_toolButtonBackView_clicked();
	void on_toolButtonProjection_toggled(bool checked);
	void on_toolButtonSectionView_toggled(bool checked);
	void on_toolButtonMultiView_toggled(bool checked);

	void on_isometricView_triggered(bool checked);
	void on_dimetricView_triggered(bool checked);
	void on_trimetricView_triggered(bool checked);

	void on_displayShaded_triggered(bool);
	void on_displayWireframe_triggered(bool);
	void on_displayWireShaded_triggered(bool);
	void on_displayRealShaded_triggered(bool);

	void on_pushButtonLightAmbient_clicked();
	void on_pushButtonLightDiffuse_clicked();
	void on_pushButtonLightSpecular_clicked();

	void on_pushButtonMaterialAmbient_clicked();
	void on_pushButtonMaterialDiffuse_clicked();
	void on_pushButtonMaterialSpecular_clicked();
	void on_pushButtonMaterialEmissive_clicked();

	void on_sliderLightPosX_valueChanged(int);
	void on_sliderLightPosY_valueChanged(int);
	void on_sliderLightPosZ_valueChanged(int);

	void on_sliderTransparency_valueChanged(int value);
	void on_sliderShine_valueChanged(int value);

	void on_pushButtonBrass_clicked();
	void on_pushButtonBronze_clicked();
	void on_pushButtonCopper_clicked();
	void on_pushButtonGold_clicked();
	void on_pushButtonSilver_clicked();
	void on_pushButtonRuby_clicked();
	void on_pushButtonEmerald_clicked();
	void on_pushButtonTurquoise_clicked();
	void on_pushButtonJade_clicked();
	void on_pushButtonObsidian_clicked();
	void on_pushButtonPearl_clicked();
	void on_pushButtonChrome_clicked();
	void on_pushButtonBlackPlastic_clicked();
	void on_pushButtonCyanPlastic_clicked();
	void on_pushButtonGreenPlastic_clicked();
	void on_pushButtonRedPlastic_clicked();
	void on_pushButtonWhitePlastic_clicked();
	void on_pushButtonYellowPlastic_clicked();
	void on_pushButtonBlackRubber_clicked();
	void on_pushButtonCyanRubber_clicked();
	void on_pushButtonGreenRubber_clicked();
	void on_pushButtonRedRubber_clicked();
	void on_pushButtonWhiteRubber_clicked();
	void on_pushButtonYellowRubber_clicked();

	void on_toolButtonOpen_clicked();

	void setListRow(int index);

	void showContextMenu(const QPoint& pos);
	void centerScreen();

	void on_toolButtonShowHideAxis_toggled(bool checked);
	void on_toolButtonVertexNormal_clicked(bool checked);
	void on_toolButtonFaceNormal_clicked(bool checked);

	void on_checkBoxSelectAll_toggled(bool checked);

	void on_listWidgetModel_itemChanged(QListWidgetItem*);
	void on_listWidgetModel_itemSelectionChanged();

	void on_checkBoxShadowMapping_toggled(bool checked);
	void on_checkBoxEnvMapping_toggled(bool checked);
	void on_checkBoxSkyBox_toggled(bool checked);
	void on_checkBoxReflections_toggled(bool checked);
	void on_checkBoxFloor_toggled(bool checked);

    void on_checkBoxFloorTexture_toggled(bool checked);

    void on_pushButtonFloorTexture_clicked();

    void on_toolBox_currentChanged(int index);

    void on_toolButtonRotateView_clicked();

    void on_toolButtonPanView_clicked();

    void on_toolButtonZoomView_clicked();

    void on_pushButtonSkyBoxTex_clicked();

protected:
	void showEvent(QShowEvent* event);

private:
	GLWidget* _glWidget;

	QAction* isometricView;
	QAction* dimetricView;
	QAction* trimetricView;

	QAction* displayShaded;
	QAction* displayWireframe;
	QAction* displayWireShaded;
	QAction* displayRealShaded;

	QVector4D _ambiMat;
	QVector4D _diffMat;
	QVector4D _specMat;
	QVector4D _emmiMat;
	QVector4D _specRef;
	float _opacity;
	float _shine;
	bool _bHasTexture;

	bool _bFirstTime;
	bool _bDeletionInProgress;

	QString _lastOpenedDir;

private:
	void updateControls();
};

#endif
