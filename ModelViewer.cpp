#include <QApplication>
#include "ModelViewer.h"
#include "GLWidget.h"
#include "SphericalHarmonicsEditor.h"
#include "TriangleMesh.h"

ModelViewer::ModelViewer(QWidget *parent) : QWidget(parent)
{
    _bFirstTime = true;
    isometricView = new QAction(QIcon(":/new/prefix1/res/isometric.png"), "Isometric", this);
    isometricView->setObjectName(QString::fromUtf8("isometricView"));
    isometricView->setShortcut(QKeySequence(Qt::Key_Home));

    dimetricView = new QAction(QIcon(":/new/prefix1/res/dimetric.png"), "Dimetric", this);
    dimetricView->setObjectName(QString::fromUtf8("dimetricView"));
    dimetricView->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_End));

    trimetricView = new QAction(QIcon(":/new/prefix1/res/trimetric.png"), "Trimetric", this);
    trimetricView->setObjectName(QString::fromUtf8("trimetricView"));
    trimetricView->setShortcut(QKeySequence(Qt::Key_End));


    displayShaded = new QAction(QIcon(":/new/prefix1/res/shaded.png"), "Shaded", this);
    displayShaded->setObjectName(QString::fromUtf8("displayShaded"));
    displayShaded->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_S));

    displayWireframe = new QAction(QIcon(":/new/prefix1/res/wireframe.png"), "Wireframe", this);
    displayWireframe->setObjectName(QString::fromUtf8("displayWireframe"));
    displayWireframe->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_W));

    displayWireShaded = new QAction(QIcon(":/new/prefix1/res/wireshaded.png"), "Wire Shaded", this);
    displayWireShaded->setObjectName(QString::fromUtf8("displayWireShaded"));
    displayWireShaded->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_W));


    setupUi(this);

    // View
    QMenu *axoMenu = new QMenu;
    axoMenu->addAction(isometricView);
    axoMenu->addAction(dimetricView);
    axoMenu->addAction(trimetricView);
    // add action to widget as well
    addAction(isometricView);
    addAction(dimetricView);
    addAction(trimetricView);

    toolButtonIsometricView->setMenu(axoMenu);
    toolButtonIsometricView->setDefaultAction(isometricView);
    QObject::connect(toolButtonIsometricView, SIGNAL(triggered(QAction *)),
                     toolButtonIsometricView, SLOT(setDefaultAction(QAction *)));

    // Shading
    QMenu *dispMenu = new QMenu;
    dispMenu->addAction(displayShaded);
    dispMenu->addAction(displayWireframe);
    dispMenu->addAction(displayWireShaded);
    // add action to widget as well
    addAction(displayShaded);
    addAction(displayWireframe);
    addAction(displayWireShaded);

    toolButtonDisplayMode->setMenu(dispMenu);
    toolButtonDisplayMode->setDefaultAction(displayShaded);
    QObject::connect(toolButtonDisplayMode, SIGNAL(triggered(QAction *)),
                     toolButtonDisplayMode, SLOT(setDefaultAction(QAction *)));

    setAttribute(Qt::WA_DeleteOnClose);

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    format.setStereo(true);
    _glWidget = new GLWidget(glframe, "glwidget");
    _glWidget->setAttribute(Qt::WA_DeleteOnClose);
    _glWidget->setFormat(format);
    _glWidget->setMouseTracking(true);
    // Put the GL widget inside the frame
    QVBoxLayout *flayout = new QVBoxLayout(glframe);
    flayout->addWidget(_glWidget, 1);
    _glWidget->installEventFilter(tabWidget_2);
    tabWidget_2->setParent(_glWidget);
    _glWidget->layout()->addWidget(tabWidget_2);
    //connect(_glWidget, SIGNAL(displayListSet()), this, SLOT(updateDisplayList()));

    QObject::connect(_glWidget, SIGNAL(windowZoomEnded()), toolButtonWindowZoom, SLOT(toggle()));
    QObject::connect(_glWidget, SIGNAL(objectSelectionChanged(int)), this, SLOT(setListRow(int)));

    listWidgetModel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listWidgetModel, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_Delete), listWidgetModel);
    connect(shortcut, SIGNAL(activated()), this, SLOT(deleteItem()));

    shortcut = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_O), this);
    connect(shortcut, SIGNAL(activated()), this, SLOT(on_toolButtonOpen_clicked()));

    _opacity = 1.0f;
    _ambiMat = {0.2109375f, 0.125f, 0.05078125f, _opacity};
    _diffMat = {0.7109375f, 0.62890625f, 0.55078125f, _opacity};
    _specMat = {0.37890625f, 0.390625f, 0.3359375f, _opacity};
    _emmiMat = {0.0f, 0.0f, 0.0f, _opacity};

    _specRef = {1.0f, 1.0f, 1.0f, 1.0f};

    _shine = 128 * 0.2f;
    _bHasTexture = false;

    updateControls();
}

ModelViewer::~ModelViewer()
{
    if (_glWidget)
    {
        delete _glWidget;
    }
}

void ModelViewer::setListRow(int index)
{
	if (index != -1)
	{
		QListWidgetItem* item = listWidgetModel->item(index);
		listWidgetModel->setItemSelected(item, item->isSelected());
	}
}

void ModelViewer::on_checkTexture_toggled(bool checked)
{
    _bHasTexture = checked;
    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           _emmiMat,
                           _shine,
                           _opacity,
                           checkTexture->isChecked()};
    setMaterialProps(mat);
    _glWidget->updateView();
}


void ModelViewer::on_textureButton_clicked()
{
    QImage buf;
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Choose an image for texture",
        _lastOpenedDir,
        "Images (*.bmp *.png *.xpm *.jpg *.tga *.ppm *.pcx)");
    _lastOpenedDir = QFileInfo(fileName).path(); // store path for next time
    if (fileName != "")
    {
        if (!buf.load(fileName))
        { // Load first image from file
            qWarning("Could not read image file, using single-color instead.");
            QImage dummy(128, 128, (QImage::Format)5);
            dummy.fill(1);
            buf = dummy;
        }
        _glWidget->setTexture(buf);

        if (listWidgetModel->count())
        {
            std::vector<int> ids;
            QList<QListWidgetItem *> items = listWidgetModel->selectedItems();
            for (QListWidgetItem *i : (items.isEmpty() ? listWidgetModel->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard) : items))
            {
                int rowId = listWidgetModel->row(i);
                ids.push_back(rowId);
            }
            _glWidget->setTexture(ids, buf);
        }

        _glWidget->updateView();
    }
}

void ModelViewer::on_defaultButton_clicked()
{
    _opacity = 1.0;
    _glWidget->setAmbientLight({0.0f, 0.0f, 0.0f, 1.0f});
    _glWidget->setDiffuseLight({1.0f, 1.0f, 1.0f, 1.0f});
    _glWidget->setSpecularLight({0.5f, 0.5f, 0.5f, 1.0f});
    _ambiMat = {0.2109375f, 0.125f, 0.05078125f, _opacity};      // 54 32 13
    _diffMat = {0.7109375f, 0.62890625f, 0.55078125f, _opacity}; // 182 161 141
    _specMat = {0.37890625f, 0.390625f, 0.3359375f, _opacity};   // 97 100 86
    _emmiMat = {0, 0, 0, 1};
    _shine = 128 * 0.2f;
    _glWidget->setLightPosition({0.0, 0.0, 50.0f});

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           _emmiMat,
                           _shine,
                           _opacity,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    updateControls();
    _glWidget->updateView();
}

void ModelViewer::on_isometricView_triggered(bool /*checked*/)
{
    buttonGroup->setExclusive(false);
    for (auto b : buttonGroup->buttons())
    {
        b->setChecked(false);
    }
    buttonGroup->setExclusive(true);

    toolButtonTopView->setChecked(false);
    _glWidget->setViewMode(ViewMode::ISOMETRIC);
    _glWidget->updateView();

    toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction *>(sender()));
}

void ModelViewer::on_dimetricView_triggered(bool /*checked*/)
{
    buttonGroup->setExclusive(false);
    for (auto b : buttonGroup->buttons())
    {
        b->setChecked(false);
    }
    buttonGroup->setExclusive(true);

    _glWidget->setViewMode(ViewMode::DIMETRIC);
    _glWidget->updateView();

    toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction *>(sender()));
}

void ModelViewer::on_trimetricView_triggered(bool /*checked*/)
{
    buttonGroup->setExclusive(false);
    for (auto b : buttonGroup->buttons())
    {
        b->setChecked(false);
    }
    buttonGroup->setExclusive(true);

    _glWidget->setViewMode(ViewMode::TRIMETRIC);
    _glWidget->updateView();

    toolButtonIsometricView->setDefaultAction(dynamic_cast<QAction *>(sender()));
}

void ModelViewer::on_displayShaded_triggered(bool)
{
    _glWidget->setDisplayMode(DisplayMode::SHADED);
    _glWidget->updateView();
    displayShaded->setToolTip("Wireframe");
}

void ModelViewer::on_displayWireframe_triggered(bool)
{
    _glWidget->setDisplayMode(DisplayMode::WIREFRAME);
    _glWidget->updateView();
    displayShaded->setToolTip("Shaded");
}

void ModelViewer::on_displayWireShaded_triggered(bool)
{
    _glWidget->setDisplayMode(DisplayMode::WIRESHADED);
    _glWidget->updateView();
    displayShaded->setToolTip("Wire Shaded");
}

void ModelViewer::on_toolButtonFitAll_clicked(bool /*checked*/)
{
    _glWidget->fitAll();
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonWindowZoom_clicked(bool checked)
{
    if (checked)
    {
        _glWidget->beginWindowZoom();
    }
}

void ModelViewer::on_toolButtonTopView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::TOP);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonBottomView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::BOTTOM);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonLeftView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::LEFT);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonRightView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::RIGHT);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonFrontView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::FRONT);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonBackView_clicked(bool /*checked*/)
{
    _glWidget->setMultiView(false);
    toolButtonMultiView->setChecked(false);
    _glWidget->setViewMode(ViewMode::BACK);
    _glWidget->updateView();
}

void ModelViewer::on_toolButtonProjection_toggled(bool checked)
{
    _glWidget->setProjection(checked ? ViewProjection::PERSPECTIVE : ViewProjection::ORTHOGRAPHIC);
    toolButtonProjection->setToolTip(checked ? "Orthographic" : "Perspective");
}

void ModelViewer::on_toolButtonSectionView_toggled(bool checked)
{
    _glWidget->showClippingPlaneEditor(checked);
}

void ModelViewer::on_toolButtonShowHideAxis_toggled(bool checked)
{
    _glWidget->showAxis(checked);
}

void ModelViewer::on_toolButtonMultiView_toggled(bool checked)
{
    _glWidget->setMultiView(checked);
    toolButtonIsometricView->animateClick(0);
    _glWidget->resizeView(glframe->width(), glframe->height());
    _glWidget->updateView();
}

void ModelViewer::on_pushButtonLightAmbient_clicked()
{
    QVector4D ambientLight = _glWidget->getAmbientLight();
    QColor c = QColorDialog::getColor(QColor::fromRgbF(ambientLight.x(), ambientLight.y(), ambientLight.z()), this, "Ambient Light Color");
    if (c.isValid())
    {
        _glWidget->setAmbientLight({static_cast<float>(c.redF()),
                                    static_cast<float>(c.greenF()),
                                    static_cast<float>(c.blueF()),
                                    static_cast<float>(c.alphaF())});
        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonLightDiffuse_clicked()
{
    QVector4D diffuseLight = _glWidget->getDiffuseLight();
    QColor c = QColorDialog::getColor(QColor::fromRgbF(diffuseLight.x(), diffuseLight.y(), diffuseLight.z()), this, "Diffuse Light Color");
    if (c.isValid())
    {
        _glWidget->setDiffuseLight({static_cast<float>(c.redF()),
                                    static_cast<float>(c.greenF()),
                                    static_cast<float>(c.blueF()),
                                    static_cast<float>(c.alphaF())});
        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonLightSpecular_clicked()
{
    QVector4D specularLight = _glWidget->getSpecularLight();
    QColor c = QColorDialog::getColor(QColor::fromRgbF(specularLight.x(), specularLight.y(), specularLight.z()), this, "Specular Light Color");
    if (c.isValid())
    {
        _glWidget->setSpecularLight({static_cast<float>(c.redF()),
                                     static_cast<float>(c.greenF()),
                                     static_cast<float>(c.blueF()),
                                     static_cast<float>(c.alphaF())});
        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonMaterialAmbient_clicked()
{
    QColor c = QColorDialog::getColor(QColor::fromRgbF(_ambiMat.x(), _ambiMat.y(), _ambiMat.z()), this, "Ambient Material Color");
    if (c.isValid())
    {
        _ambiMat = {
            static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};

        GLMaterialProps mat = {_ambiMat,
                               _diffMat,
                               _specMat,
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               _emmiMat,
                               _shine,
                               _opacity,
                               checkTexture->isChecked()};
        setMaterialProps(mat);

        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonMaterialDiffuse_clicked()
{
    QColor c = QColorDialog::getColor(QColor::fromRgbF(_diffMat.x(), _diffMat.y(), _diffMat.z()), this, "Diffuse Material Color");
    if (c.isValid())
    {
        _diffMat = {
            static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};

        GLMaterialProps mat = {_ambiMat,
                               _diffMat,
                               _specMat,
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               _emmiMat,
                               _shine,
                               _opacity,
                               checkTexture->isChecked()};
        setMaterialProps(mat);

        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonMaterialSpecular_clicked()
{
    QColor c = QColorDialog::getColor(QColor::fromRgbF(_specMat.x(), _specMat.y(), _specMat.z()), this, "Specular Material Color");
    if (c.isValid())
    {
        _specMat = {
            static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};

        GLMaterialProps mat = {_ambiMat,
                               _diffMat,
                               _specMat,
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               _emmiMat,
                               _shine,
                               _opacity,
                               checkTexture->isChecked()};
        setMaterialProps(mat);

        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_pushButtonMaterialEmissive_clicked()
{
    QColor c = QColorDialog::getColor(QColor::fromRgbF(_emmiMat.x(), _emmiMat.y(), _emmiMat.z()), this, "Emissive Material Color");
    if (c.isValid())
    {
        _emmiMat = {
            static_cast<float>(c.redF()),
            static_cast<float>(c.greenF()),
            static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};

        GLMaterialProps mat = {_ambiMat,
                               _diffMat,
                               _specMat,
                               {1.0f, 1.0f, 1.0f, 1.0f},
                               _emmiMat,
                               _shine,
                               _opacity,
                               checkTexture->isChecked()};
        setMaterialProps(mat);

        updateControls();
        _glWidget->updateView();
    }
}

void ModelViewer::on_sliderLightPosX_valueChanged(int)
{
    _glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
                                          static_cast<float>(sliderLightPosY->value()),
                                          static_cast<float>(sliderLightPosZ->value())));
    _glWidget->updateView();
}

void ModelViewer::on_sliderLightPosY_valueChanged(int)
{
    _glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
                                          static_cast<float>(sliderLightPosY->value()),
                                          static_cast<float>(sliderLightPosZ->value())));
    _glWidget->updateView();
}

void ModelViewer::on_sliderLightPosZ_valueChanged(int)
{
    _glWidget->setLightPosition(QVector3D(static_cast<float>(sliderLightPosX->value()),
                                          static_cast<float>(sliderLightPosY->value()),
                                          static_cast<float>(sliderLightPosZ->value())));
    _glWidget->updateView();
}

void ModelViewer::on_sliderTransparency_valueChanged(int value)
{
    _opacity = (GLfloat)value / 100.0;
    _ambiMat[3] = _opacity;
    _diffMat[3] = _opacity;
    _specMat[3] = _opacity;

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           _opacity,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
}

void ModelViewer::on_sliderShine_valueChanged(int value)
{
    _shine = value;

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           _opacity,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
}

void ModelViewer::on_pushButtonBrass_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat = {0.329412f, 0.223529f, 0.027451f, 1};
    _diffMat = {0.780392f, 0.568627f, 0.113725f, 1};
    _specMat = {0.992157f, 0.941176f, 0.807843f, 1};
    _shine = fabs(128.0 * 0.21794872);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonBronze_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat = {0.2125f, 0.1275f, 0.054f, 1};
    _diffMat = {0.714f, 0.4284f, 0.18144f, 1};
    _specMat = {0.393548f, 0.271906f, 0.166721f, 1};
    _shine = fabs(128.0 * 0.2);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonCopper_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat = {0.19125f, 0.0735f, 0.0225f, 1.0f};
    _diffMat = {0.7038f, 0.27048f, 0.0828f, 1.0f};
    _specMat = {0.256777f, 0.137622f, 0.086014f, 1.0f};
    _shine = fabs(128.0 * 0.1);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonGold_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.24725f;
    _ambiMat[1] = 0.1995f;
    _ambiMat[2] = 0.0745f;
    _diffMat[0] = 0.75164f;
    _diffMat[1] = 0.60648f;
    _diffMat[2] = 0.22648f;
    _specMat[0] = 0.628281f;
    _specMat[1] = 0.555802f;
    _specMat[2] = 0.366065f;
    _shine = fabs(128.0 * 0.4);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonSilver_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.19225f;
    _ambiMat[1] = 0.19225f;
    _ambiMat[2] = 0.19225f;
    _diffMat[0] = 0.50754f;
    _diffMat[1] = 0.50654f;
    _diffMat[2] = 0.50754f;
    _specMat[0] = 0.508273f;
    _specMat[1] = 0.508273f;
    _specMat[2] = 0.508273f;
    _shine = fabs(128.0 * 0.4);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonRuby_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.1745f;
    _ambiMat[1] = 0.01175f;
    _ambiMat[2] = 0.01175f;
    _diffMat[0] = 0.61424f;
    _diffMat[1] = 0.04136f;
    _diffMat[2] = 0.04136f;
    _specMat[0] = 0.727811f;
    _specMat[1] = 0.626959f;
    _specMat[2] = 0.626959f;
    _shine = fabs(128.0 * 0.6);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonEmerald_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0215f;
    _ambiMat[1] = 0.1745f;
    _ambiMat[2] = 0.0215f;
    _diffMat[0] = 0.07568f;
    _diffMat[1] = 0.61424f;
    _diffMat[2] = 0.07568f;
    _specMat[0] = 0.633f;
    _specMat[1] = 0.727811f;
    _specMat[2] = 0.633f;
    _shine = fabs(128.0 * 0.6);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonTurquoise_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.1f;
    _ambiMat[1] = 0.18725f;
    _ambiMat[2] = 0.1745f;
    _diffMat[0] = 0.396f;
    _diffMat[1] = 0.74151f;
    _diffMat[2] = 0.69102f;
    _specMat[0] = 0.297254f;
    _specMat[1] = 0.30829f;
    _specMat[2] = 0.306678f;
    _shine = fabs(128.0 * 0.1);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonJade_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.135f;
    _ambiMat[1] = 0.2225f;
    _ambiMat[2] = 0.1575f;
    _diffMat[0] = 0.54f;
    _diffMat[1] = 0.89f;
    _diffMat[2] = 0.63f;
    _specMat[0] = 0.316228f;
    _specMat[1] = 0.316228f;
    _specMat[2] = 0.316228f;
    _shine = fabs(128.0 * 0.1);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonObsidian_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.05375f;
    _ambiMat[1] = 0.05f;
    _ambiMat[2] = 0.06625f;
    _diffMat[0] = 0.18275f;
    _diffMat[1] = 0.17f;
    _diffMat[2] = 0.22525f;
    _specMat[0] = 0.332741f;
    _specMat[1] = 0.328634f;
    _specMat[2] = 0.346435f;
    _shine = fabs(128.0 * 0.3);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonPearl_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.25f;
    _ambiMat[1] = 0.20725f;
    _ambiMat[2] = 0.20725f;
    _diffMat[0] = 1.0f;
    _diffMat[1] = 0.829f;
    _diffMat[2] = 0.829f;
    _specMat[1] = 0.296648f;
    _specMat[2] = 0.296648f;
    _specMat[0] = 0.299948f;
    _shine = fabs(128.0 * 0.088);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonChrome_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.25f;
    _ambiMat[1] = 0.25f;
    _ambiMat[2] = 0.25f;
    _diffMat[0] = 0.4f;
    _diffMat[1] = 0.4f;
    _diffMat[2] = 0.4f;
    _specMat[0] = 0.774597f;
    _specMat[1] = 0.774597f;
    _specMat[2] = 0.774597f;
    _shine = fabs(128.0 * 0.6);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonBlackPlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.01f;
    _diffMat[1] = 0.01f;
    _diffMat[2] = 0.01f;
    _specMat[0] = 0.5f;
    _specMat[1] = 0.5f;
    _specMat[2] = 0.5f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonCyanPlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.1f;
    _ambiMat[2] = 0.06f;
    _diffMat[0] = 0.0f;
    _diffMat[1] = 0.50980392f;
    _diffMat[2] = 0.50980392f;
    _specMat[0] = 0.50196078f;
    _specMat[1] = 0.50196078f;
    _specMat[2] = 0.50196078f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonGreenPlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.1f;
    _diffMat[1] = 0.35f;
    _diffMat[2] = 0.1f;
    _specMat[0] = 0.45f;
    _specMat[1] = 0.55f;
    _specMat[2] = 0.45f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonRedPlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.5f;
    _diffMat[1] = 0.0f;
    _diffMat[2] = 0.0f;
    _specMat[0] = 0.7f;
    _specMat[1] = 0.6f;
    _specMat[2] = 0.6f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonWhitePlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.55f;
    _diffMat[1] = 0.55f;
    _diffMat[2] = 0.55f;
    _specMat[0] = 0.70f;
    _specMat[1] = 0.70f;
    _specMat[2] = 0.70f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonYellowPlastic_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.5f;
    _diffMat[1] = 0.5f;
    _diffMat[2] = 0.0f;
    _specMat[0] = 0.6f;
    _specMat[1] = 0.6f;
    _specMat[2] = 0.5f;
    _shine = fabs(128.0 * 0.25);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonBlackRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.02f;
    _ambiMat[1] = 0.02f;
    _ambiMat[2] = 0.02f;
    _diffMat[0] = 0.01f;
    _diffMat[1] = 0.01f;
    _diffMat[2] = 0.01f;
    _specMat[0] = 0.4f;
    _specMat[1] = 0.4f;
    _specMat[2] = 0.4f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonCyanRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.05f;
    _ambiMat[2] = 0.05f;
    _diffMat[0] = 0.4f;
    _diffMat[1] = 0.5f;
    _diffMat[2] = 0.5f;
    _specMat[0] = 0.04f;
    _specMat[1] = 0.7f;
    _specMat[2] = 0.7f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonGreenRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.0f;
    _ambiMat[1] = 0.05f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.4f;
    _diffMat[1] = 0.5f;
    _diffMat[2] = 0.4f;
    _specMat[0] = 0.04f;
    _specMat[1] = 0.7f;
    _specMat[2] = 0.04f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonRedRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.05f;
    _ambiMat[1] = 0.0f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.7f;
    _diffMat[1] = 0.4f;
    _diffMat[2] = 0.4f;
    _specMat[0] = 0.7f;
    _specMat[1] = 0.04f;
    _specMat[2] = 0.04f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonWhiteRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.05f;
    _ambiMat[1] = 0.05f;
    _ambiMat[2] = 0.05f;
    _diffMat[0] = 0.5f;
    _diffMat[1] = 0.5f;
    _diffMat[2] = 0.5f;
    _specMat[0] = 0.7f;
    _specMat[1] = 0.7f;
    _specMat[2] = 0.7f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::on_pushButtonYellowRubber_clicked()
{
    //Light Values
    _glWidget->setAmbientLight({0, 0, 0, 1});
    _glWidget->setDiffuseLight({1, 1, 1, 1});
    _glWidget->setSpecularLight({0.5, 0.5, 0.5, 1});

    //Material Values
    _ambiMat[0] = 0.05f;
    _ambiMat[1] = 0.05f;
    _ambiMat[2] = 0.0f;
    _diffMat[0] = 0.5f;
    _diffMat[1] = 0.5f;
    _diffMat[2] = 0.4f;
    _specMat[0] = 0.7f;
    _specMat[1] = 0.7f;
    _specMat[2] = 0.04f;
    _shine = fabs(128.0 * 0.078125);

    GLMaterialProps mat = {_ambiMat,
                           _diffMat,
                           _specMat,
                           {1.0f, 1.0f, 1.0f, 1.0f},
                           {0.0f, 0.0f, 0.0f, 1.0f},
                           _shine,
                           1.0f,
                           checkTexture->isChecked()};
    setMaterialProps(mat);

    _glWidget->updateView();
    updateControls();
}

void ModelViewer::updateControls()
{
    sliderShine->setValue((int)_shine);
    sliderTransparency->setValue((int)(255 * _opacity));
    sliderLightPosX->setValue((int)_glWidget->getLightPosition().x());
    sliderLightPosY->setValue((int)_glWidget->getLightPosition().y());
    sliderLightPosZ->setValue((int)_glWidget->getLightPosition().z());

    QColor col;
    QVector4D ambientLight = _glWidget->getAmbientLight();
    col.setRgbF(ambientLight.x(), ambientLight.y(), ambientLight.z());
    QString qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonLightAmbient->setStyleSheet(qss);

    QVector4D diffuseLight = _glWidget->getDiffuseLight();
    col.setRgbF(diffuseLight.x(), diffuseLight.y(), diffuseLight.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonLightDiffuse->setStyleSheet(qss);

    QVector4D specularLight = _glWidget->getSpecularLight();
    col.setRgbF(specularLight.x(), specularLight.y(), specularLight.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonLightSpecular->setStyleSheet(qss);

    col.setRgbF(_ambiMat.x(), _ambiMat.y(), _ambiMat.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonMaterialAmbient->setStyleSheet(qss);

    col.setRgbF(_diffMat.x(), _diffMat.y(), _diffMat.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonMaterialDiffuse->setStyleSheet(qss);

    col.setRgbF(_specMat.x(), _specMat.y(), _specMat.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonMaterialSpecular->setStyleSheet(qss);

    col.setRgbF(_emmiMat.x(), _emmiMat.y(), _emmiMat.z());
    qss = QString("background-color: %1;color: %2").arg(col.name()).arg(col.lightness() < 75 ? QColor(Qt::white).name() : QColor(Qt::black).name());
    pushButtonMaterialEmissive->setStyleSheet(qss);
}

void ModelViewer::updateDisplayList()
{
    listWidgetModel->clear();
    std::vector<TriangleMesh *> store = _glWidget->getMeshStore();
    std::vector<int> ids = _glWidget->getDisplayedObjectsIds();
    int id = 0;
    for (TriangleMesh *mesh : store)
    {
        QListWidgetItem* item = new QListWidgetItem(mesh->getName());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); // set checkable flag
        // AND initialize check state
        if(std::count(ids.begin(), ids.end(), id))
            item->setCheckState(Qt::Checked);
        else
            item->setCheckState(Qt::Unchecked);
        listWidgetModel->addItem(item);
        id++;
    }
}

void ModelViewer::showEvent(QShowEvent *)
{
    //showMaximized();
    if (_bFirstTime)
    {
        updateDisplayList();
        _bFirstTime = false;
    }
}

void ModelViewer::showContextMenu(const QPoint &pos)
{
    if (listWidgetModel->selectedItems().count() != 0)
    {
        // Handle global position
        QPoint globalPos = listWidgetModel->mapToGlobal(pos);

        // Create menu and insert some actions
        QMenu myMenu;

        QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
        if(selectedItems.count() <= 1 && selectedItems.at(0)->checkState() == Qt::Checked)
            myMenu.addAction("Center Screen", this, SLOT(centerScreen()));

        myMenu.addAction("Visualization Properties", this, SLOT(showPropertiesPage()));
        myMenu.addAction("Transformations", this, SLOT(showTransformationsPage()));
        myMenu.addAction("Delete", this, SLOT(deleteItem()));

        // Show context menu at handling position
        myMenu.exec(globalPos);
    }
}

void ModelViewer::centerScreen()
{
    QListWidgetItem* item = listWidgetModel->currentItem();
    int rowId = listWidgetModel->row(item);
    _glWidget->centerScreen(rowId);
}

void ModelViewer::deleteItem()
{
    if (QMessageBox::question(this, "Confirmation", "Delete selection?") == QMessageBox::Yes)
    {
        // If multiple selection is on, we need to erase all selected items
        QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
        for (QListWidgetItem *item : selectedItems)
        {
            item->setCheckState(Qt::Unchecked);
            int rowId = listWidgetModel->row(item);

            // Remove the displayed object
            _glWidget->removeFromDisplay(rowId);

            // Get curent item on selected row
            QListWidgetItem *curItem = listWidgetModel->takeItem(rowId);
            // And remove it
            delete curItem;
        }        
        if (listWidgetModel->count())
        {
            listWidgetModel->setCurrentRow(0);
            on_listWidgetModel_itemChanged(nullptr);
        }
        _glWidget->update();
    }
}

void ModelViewer::showPropertiesPage()
{
    toolBox->setCurrentIndex(1);
}

void ModelViewer::showTransformationsPage()
{
    toolBox->setCurrentIndex(2);
}

void ModelViewer::on_listWidgetModel_itemChanged(QListWidgetItem*)
{
    if (listWidgetModel->count())
    {
        std::vector<int> ids;
        for (int i = 0; i < listWidgetModel->count(); i++)
        {            
            QListWidgetItem* item = listWidgetModel->item(i);            
            if (item->checkState() == Qt::Checked)
            {
                int rowId = listWidgetModel->row(item);
                ids.push_back(rowId);
            }            
        }
        _glWidget->setDisplayList(ids);
    }
}

void ModelViewer::on_listWidgetModel_itemSelectionChanged()
{
    for (int i = 0; i < listWidgetModel->count(); i++)
    {
        QListWidgetItem* item = listWidgetModel->item(i);
        int rowId = listWidgetModel->row(item);
        if(item->isSelected())
            _glWidget->select(rowId);
        else
            _glWidget->deselect(rowId);
    }
    _glWidget->update();
}


#include "STLMesh.h"
#include "MeshProperties.h"
void ModelViewer::on_toolButtonOpen_clicked()
{
    TriangleMesh *mesh = nullptr;

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Model File"),
                                                    _lastOpenedDir,
                                                    tr("All Models(*.stl *.obj);;STL Files (*.stl *.STL);;OBJ Files (*.obj *.OBJ)"));

    if (fileName != "")
    {
        QFileInfo fi(fileName);
        if (fi.suffix().toLower() == "stl")
        {
            mesh = _glWidget->loadSTLMesh(fileName);
            if (mesh)
            {
                if (!static_cast<STLMesh*>(mesh)->loaded())
                {
                    delete mesh;
                    mesh = nullptr;
                }
            }
        }
        if (fi.suffix().toLower() == "obj")
            mesh = _glWidget->loadOBJMesh(fileName);
        _lastOpenedDir = QFileInfo(fileName).path(); // store path for next time

        if (mesh)
        {
            updateDisplayList();

            listWidgetModel->setCurrentRow(listWidgetModel->count() - 1);
            listWidgetModel->currentItem()->setCheckState(Qt::Checked);

            updateDisplayList();

            MeshProperties props(mesh);
            std::cout << "Mesh Volume = " << props.volume() << "\nSurface Area = " << props.surfaceArea() << std::endl;
        }
        else
        {
            QMessageBox::critical(this, "Error", "Model load unsuccessful!");
        }
    }
}

void ModelViewer::setMaterialProps(const GLMaterialProps &mat)
{
    if (listWidgetModel->count())
    {
        std::vector<int> ids;
        QList<QListWidgetItem *> items = listWidgetModel->selectedItems();
        for (QListWidgetItem *i : (items.isEmpty() ? listWidgetModel->findItems(QString("*"), Qt::MatchWrap | Qt::MatchWildcard) : items))
        {
            int rowId = listWidgetModel->row(i);
            ids.push_back(rowId);
        }
        _glWidget->setMaterialProps(ids, mat);
    }
}

void ModelViewer::setTransformation()
{
    if (listWidgetModel->count())
    {
        std::vector<int> ids;
        QList<QListWidgetItem *> items = listWidgetModel->selectedItems();
        for (QListWidgetItem *i : items)
        {
            int rowId = listWidgetModel->row(i);
            ids.push_back(rowId);
        }
        QMatrix4x4 mat;
        mat.translate(doubleSpinBoxDX->value(), doubleSpinBoxDY->value(), doubleSpinBoxDZ->value());
        mat.rotate(QQuaternion::fromEulerAngles(doubleSpinBoxRX->value(), doubleSpinBoxRY->value(), doubleSpinBoxRZ->value()));
        mat.scale(doubleSpinBoxSX->value(), doubleSpinBoxSY->value(), doubleSpinBoxSZ->value());
        _glWidget->setTransformation(ids, mat);
    }
}

void ModelViewer::on_doubleSpinBoxDX_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxDY_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxDZ_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxRX_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxRY_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxRZ_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxSX_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxSY_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_doubleSpinBoxSZ_valueChanged(double)
{
    setTransformation();
    _glWidget->update();
}

void ModelViewer::on_toolButtonVertexNormal_clicked(bool checked)
{
    _glWidget->setShowVertexNormals(checked);
    _glWidget->update();
}

void ModelViewer::on_toolButtonFaceNormal_clicked(bool checked)
{
    _glWidget->setShowFaceNormals(checked);
    _glWidget->update();
}


void ModelViewer::on_checkBoxSelectAll_toggled(bool checked)
{
    if (listWidgetModel->count())
    {
        for (int i = 0; i < listWidgetModel->count(); i++)
        {
            QListWidgetItem* item = listWidgetModel->item(i);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
        on_listWidgetModel_itemChanged(nullptr);
    }
}


