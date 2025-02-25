#ifndef __MODELVIEWER_H__
#define __MODELVIEWER_H__

#include "ui_ModelViewer.h"

#include "GLWidget.h"
#include "GLMaterial.h"

class ModelViewer : public QWidget, public Ui::ModelViewer
{
	Q_OBJECT
public:
	ModelViewer(QWidget* parent = 0);
	~ModelViewer();

	GLWidget* getGLView() const { return _glWidget; }

	void setMaterialToSelectedItems(const GLMaterial& mat);
	void setTransformation();
	void resetTransformation();

	QListWidget* getListModel() { return listWidgetModel; }

	void updateTransformationValues();
	void resetTransformationValues();

	void switchToRealisticRendering();

	static QString getLastOpenedDir();
	static void setLastOpenedDir(const QString& lastOpenedDir);

	static QString getLastSelectedFilter();
	static void setLastSelectedFilter(const QString& lastSelectedFilter);

    static QString getSupportedExtensions();
    static void setSupportedExtensions(const QString &supportedExtensions);

	QString currentFile() const;

	bool loadFile(const QString& fileName);

	void importModel();
	void exportModel();

	bool hasUndo();
	bool hasRedo();
	bool documentModified();

	bool save();
	bool saveAs();

	void selectAll();
	void deselectAll();

public slots:
    void updateDisplayList();
    void updateSelectionStatusMessage();
    void showAllItems();
    void showSelectedItems();
	void showOnlySelectedItems();
	void hideAllItems();
	void hideSelectedItems();
	void duplicateSelectedItems();
	void deleteSelectedItems();
	void displaySelectedMeshInfo();
	void showVisualizationModelPage();
	void showEnvironmentPage();
	void showPredefinedMaterialsPage();
	void showTransformationsPage();
	void clickMultiViewButton();

private slots:
	void setListRow(int index);
	void setListRows(QList<int> indices);
	void showContextMenu(const QPoint& pos);
	void centerScreen();
	void lightingType_toggled(QAbstractButton *, bool);

	void on_checkTexture_toggled(bool checked);
	void on_pushButtonTexture_clicked();
	void on_pushButtonDefaultLights_clicked();
	void on_pushButtonApplyADSColors_clicked();
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
	void on_toolButtonImport_clicked();
	void on_toolButtonExport_clicked();
	void on_toolButtonShowHideAxis_toggled(bool checked);
	void on_toolButtonVertexNormal_clicked(bool checked);
	void on_toolButtonFaceNormal_clicked(bool checked);

	void on_checkBoxSelectAll_stateChanged(int arg1);

	void on_listWidgetModel_itemChanged(QListWidgetItem*);
	void on_listWidgetModel_itemSelectionChanged();
	void itemEdited(QWidget* widget, QAbstractItemDelegate::EndEditHint hint);

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

	void on_pushButtonAlbedoColor_clicked();
	void on_sliderMetallic_valueChanged(int value);
	void on_sliderRoughness_valueChanged(int value);

	void on_checkBoxAlbedoMap_toggled(bool checked);
	void on_pushButtonAlbedoMap_clicked();

	void on_checkBoxMetallicMap_toggled(bool checked);
	void on_pushButtonMetallicMap_clicked();

	void on_checkBoxRoughnessMap_toggled(bool checked);
	void on_pushButtonRoughnessMap_clicked();

	void on_checkBoxNormalMap_toggled(bool checked);
	void on_pushButtonNormalMap_clicked();

	void on_checkBoxAOMap_toggled(bool checked);
	void on_pushButtonAOMap_clicked();

	void on_checkBoxOpacityMap_toggled(bool checked);
	void on_checkBoxOpacMapInvert_toggled(bool inverted);
	void on_pushButtonOpacityMap_clicked();
	void on_toolButtonClearOpacityMap_clicked();

	void on_checkBoxHeightMap_toggled(bool checked);
	void on_pushButtonHeightMap_clicked();
	void on_doubleSpinBoxHeightScale_valueChanged(double val);

	void on_pushButtonApplyPBRTexture_clicked();
	void on_pushButtonClearPBRTextures_clicked();

	void on_toolButtonClearAlbedo_clicked();
	void on_toolButtonClearMetallic_clicked();
	void on_toolButtonClearRoughness_clicked();
	void on_toolButtonClearNormal_clicked();
	void on_toolButtonClearAO_clicked();

	void on_toolButtonClearHeight_clicked();

	void on_checkBoxDiffuseTex_toggled(bool checked);
	void on_pushButtonDiffuseTexture_clicked();
	void on_toolButtonClearDiffuseTex_clicked();

	void on_checkBoxSpecularTex_toggled(bool checked);
	void on_pushButtonSpecularTexture_clicked();
	void on_toolButtonClearSpecularTex_clicked();

	void on_checkBoxEmissiveTex_toggled(bool checked);
	void on_pushButtonEmissiveTexture_clicked();
	void on_toolButtonClearEmissiveTex_clicked();

	void on_checkBoxNormalTex_toggled(bool checked);
	void on_pushButtonNormalTexture_clicked();
	void on_toolButtonClearNormalTex_clicked();

	void on_checkBoxHeightTex_toggled(bool checked);
	void on_pushButtonHeightTexture_clicked();
	void on_toolButtonClearHeightTex_clicked();

	void on_checkBoxOpacityTex_toggled(bool checked);
	void on_checkBoxOpacInvert_toggled(bool inverted);
	void on_pushButtonOpacityTexture_clicked();
	void on_toolButtonClearOpacityTex_clicked();

	void on_pushButtonApplyADSTexture_clicked();
	void on_pushButtonClearADSTextures_clicked();

	void on_toolButtonSwapVisible_clicked(bool checked);

protected:
	void showEvent(QShowEvent* event);
	void keyPressEvent(QKeyEvent* event);
	void dragEnterEvent(QDragEnterEvent* event);
	void dropEvent(QDropEvent* event);

private:
	void checkAndRenameModel(TriangleMesh* mesh, const QString& name);

private:
	GLWidget* _glWidget;

	QAction* isometricView;
	QAction* dimetricView;
	QAction* trimetricView;

	QAction* displayShaded;
	QAction* displayWireframe;
	QAction* displayWireShaded;
	QAction* displayRealShaded;

	GLMaterial _material;

	bool _bHasTexture;

	QString _diffuseADSTexture;
	QString _specularADSTexture;
	QString _emissiveADSTexture;
	QString _normalADSTexture;
	QString _heightADSTexture;
	QString _opacityADSTexture;
	bool    _hasADSDiffuseTex;
	bool    _hasADSSpecularTex;
	bool    _hasADSEmissiveTex;
	bool    _hasADSNormalTex;
	bool    _hasADSHeightTex;
	bool    _hasADSOpacityTex;

	QString _albedoPBRTexture;
	QString _metallicPBRTexture;
	QString _roughnessPBRTexture;
	QString _normalPBRTexture;
	QString _aoPBRTexture;
	QString _opacityPBRTexture;
	QString _heightPBRTexture;
	bool    _hasPBRAlbedoTex;
	bool    _hasPBRMetallicTex;
	bool    _hasPBRRoughnessTex;
	bool    _hasPBRNormalTex;
	bool    _hasPBRAOTex;
	bool    _hasPBROpacTex;
	bool    _hasPBRHeightTex;
	float   _heightPBRTexScale;

	bool _runningFirstTime;

	QString _currentFile;
	bool _textureDirOpenedFirstTime;
	bool _documentSaved;

	static QString _lastOpenedDir;
	static QString _lastSelectedFilter;
	static QString _supportedExtensions;

private:
	bool checkForActiveSelection();
	std::vector<int> getSelectedIDs() const;
	void updateControls();
	QString getSupportedQtImagesFilter();
};

#endif
