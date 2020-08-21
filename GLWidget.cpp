
#include "GLWidget.h"

#include "TextRenderer.h"

#include "Cylinder.h"
#include "Cone.h"
#include "Torus.h"
#include "Sphere.h"
#include "Cube.h"
#include "Teapot.h"
#include "KleinBottle.h"
#include "Figure8KleinBottle.h"
#include "BoySurface.h"
#include "TwistedTriaxial.h"
#include "SteinerSurface.h"
#include "SuperToroid.h"
#include "SuperToroidEditor.h"
#include "SuperEllipsoid.h"
#include "SuperEllipsoidEditor.h"
#include "Spring.h"
#include "SpringEditor.h"
#include "AppleSurface.h"
#include "DoubleCone.h"
#include "BentHorns.h"
#include "Folium.h"
#include "LimpetTorus.h"
#include "SaddleTorus.h"
#include "GraysKlein.h"
#include "GraysKleinEditor.h"
#include "BowTie.h"
#include "TriaxialTritorus.h"
#include "TriaxialHexatorus.h"
#include "VerrillMinimal.h"
#include "Horn.h"
#include "Crescent.h"
#include "ConeShell.h"
#include "Periwinkle.h"
#include "TopShell.h"
#include "WrinkledPeriwinkle.h"
#include "SpindleShell.h"
#include "TurretShell.h"
#include "TwistedPseudoSphere.h"
#include "BreatherSurface.h"
#include "SphericalHarmonic.h"
#include "SphericalHarmonicsEditor.h"
#include "ClippingPlanesEditor.h"

#include "STLMesh.h"
#include "ObjMesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include "Plane.h"

#include "ModelViewer.h"

using glm::mat4;
using glm::vec3;

GLWidget::GLWidget(QWidget* parent, const char* /*name*/) : QOpenGLWidget(parent),
    _textRenderer(nullptr),
    _axisTextRenderer(nullptr),
    _sphericalHarmonicsEditor(nullptr),
    _superToroidEditor(nullptr),
    _superEllipsoidEditor(nullptr),
    _springEditor(nullptr),
    _graysKleinEditor(nullptr),
    _clippingPlanesEditor(nullptr),
    _floorPlane(nullptr),
    _skyBox(nullptr)
{
    _viewer = static_cast<ModelViewer*>(parent);
    _bgTopColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f, 1.0f);
    _bgBotColor = QColor::fromRgbF(0.925f, 0.913f, 0.847f, 1.0f);
    quadVAO = 0;
    _planeVAO = 0;
    _fgShader = new QOpenGLShaderProgram(this);
    _axisShader = new QOpenGLShaderProgram(this);
    _vertexNormalShader = new QOpenGLShaderProgram(this);
    _faceNormalShader = new QOpenGLShaderProgram(this);
    _shadowMappingShader = new QOpenGLShaderProgram(this);
    _skyBoxShader = new QOpenGLShaderProgram(this);
    _reflectionMappingShader = new QOpenGLShaderProgram(this);

    _viewBoundingSphereDia = 200.0f;
    _viewRange = _viewBoundingSphereDia;
    _rubberBandZoomRatio = 1.0f;
    _FOV = 60.0f;
    _currentViewRange = 1.0f;
    _viewMode = ViewMode::ISOMETRIC;
    _projection = ViewProjection::ORTHOGRAPHIC;

    _camera = new GLCamera(width(), height(), _viewRange, _FOV);
    _camera->setView(GLCamera::ViewProjection::SE_ISOMETRIC_VIEW);

    _currentRotation = QQuaternion::fromRotationMatrix(_camera->getViewMatrix().toGenericMatrix<3, 3>());
    _currentTranslation = _camera->getPosition();
    _currentViewRange = _viewRange;

    _slerpStep = 0.0f;
    _slerpFrac = 0.02f;

    _modelNum = 6;

    _ambientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
    _diffuseLight = { 1.0f, 1.0f, 1.0f, 1.0f };
    _specularLight = { 0.5f, 0.5f, 0.5f, 1.0f };

    _lightPosition = { 25.0f, 25.0f, 50.0f };
    _prevLightPosition = _lightPosition;

    _displayMode = DisplayMode::SHADED;

    _bMultiView = false;

    _bShowAxis = true;

    _bWindowZoomActive = false;
    _rubberBand = nullptr;

    _modelName = "Model";

    _clipXEnabled = false;
    _clipYEnabled = false;
    _clipZEnabled = false;

    _clipXFlipped = false;
    _clipYFlipped = false;
    _clipZFlipped = false;

    _showVertexNormals = false;
    _showFaceNormals = false;

    _envMapEnabled = false;
    _shadowsEnabled = false;
    _reflectionsEnabled = false;
    _floorDisplayed = false;
    _skyBoxEnabled = false;

    _shadowWidth = 1024;
    _shadowHeight = 1024;

    _environmentMap = 0;
    _shadowMap = 0;
    _shadowMapFBO = 0;
    _reflectionMap = 0;
    _reflectionMapFBO = 0;
    _reflectionDepthMap = 0;
    _reflectionMapDBO = 0;

    _clipXCoeff = 0.0f;
    _clipYCoeff = 0.0f;
    _clipZCoeff = 0.0f;

    _clipDX = 0.0f;
    _clipDY = 0.0f;
    _clipDZ = 0.0f;

    _xTran = 0.0f;
    _yTran = 0.0f;
    _zTran = 0.0f;

    _xRot = 0.0f;
    _yRot = 0.0f;
    _zRot = 0.0f;

    _xScale = 1.0f;
    _yScale = 1.0f;
    _zScale = 1.0f;

    _animateViewTimer = new QTimer(this);
    _animateViewTimer->setTimerType(Qt::PreciseTimer);
    connect(_animateViewTimer, SIGNAL(timeout()), this, SLOT(animateViewChange()));
    connect(this, SIGNAL(rotationsSet()), _animateViewTimer, SLOT(stop()));

    _animateFitAllTimer = new QTimer(this);
    _animateFitAllTimer->setTimerType(Qt::PreciseTimer);
    connect(_animateFitAllTimer, SIGNAL(timeout()), this, SLOT(animateFitAll()));
    connect(this, SIGNAL(zoomAndPanSet()), _animateFitAllTimer, SLOT(stop()));

    _animateWindowZoomTimer = new QTimer(this);
    _animateWindowZoomTimer->setTimerType(Qt::PreciseTimer);
    connect(_animateWindowZoomTimer, SIGNAL(timeout()), this, SLOT(animateWindowZoom()));
    connect(this, SIGNAL(zoomAndPanSet()), _animateWindowZoomTimer, SLOT(stop()));

    _animateCenterScreenTimer = new QTimer(this);
    _animateCenterScreenTimer->setTimerType(Qt::PreciseTimer);
    connect(_animateCenterScreenTimer, SIGNAL(timeout()), this, SLOT(animateCenterScreen()));
    connect(this, SIGNAL(zoomAndPanSet()), _animateCenterScreenTimer, SLOT(stop()));

    _editorLayout = new QVBoxLayout(this);
    _upperLayout = new QFormLayout();
    _upperLayout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
    _upperLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    _upperLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    _editorLayout->addItem(_upperLayout);

    _editorLayout->addStretch(height());

    _lowerLayout = new QFormLayout();
    _editorLayout->addItem(_lowerLayout);
    _lowerLayout->setFormAlignment(Qt::AlignBottom | Qt::AlignRight);
    _lowerLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    _lowerLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

    _displayedObjectsIds.push_back(0);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showContextMenu(QPoint)));
}

GLWidget::~GLWidget()
{
    if (_textRenderer)
        delete _textRenderer;

    for (auto a : _meshStore)
    {
        delete a;
    }
    if (_camera)
        delete _camera;

    if (_fgShader)
        delete _fgShader;

    if (_axisShader)
        delete _axisShader;

    if (_vertexNormalShader)
        delete _vertexNormalShader;

    if (_faceNormalShader)
        delete _faceNormalShader;

    if (_shadowMappingShader)
        delete _shadowMappingShader;

    if(_skyBoxShader)
        delete _skyBoxShader;

    if(_reflectionMappingShader)
        delete _reflectionMappingShader;

    _axisVBO.destroy();
    _axisVAO.destroy();

    _bgSplitVBO.destroy();
    _bgSplitVAO.destroy();

    _bgVAO.destroy();
}

void GLWidget::updateView()
{
    update();
}

void GLWidget::setTexture(QImage img)
{
    _texImage = QGLWidget::convertToGLFormat(img); // flipped 32bit RGBA
}

void GLWidget::setTexture(const std::vector<int>& ids, const QImage& texImage)
{
    for (int id : ids)
    {
        try
        {
            TriangleMesh* mesh = _meshStore[id];
            mesh->setTexureImage(QGLWidget::convertToGLFormat(texImage));
        }
        catch (...)
        {
            std::cout << "Exception!" << std::endl;
        }
    }
}

QVector3D GLWidget::getLightPosition() const
{
    return _lightPosition;
}

void GLWidget::setLightPosition(const QVector3D& lightPosition)
{
    _lightPosition = _prevLightPosition + lightPosition;
}

QVector4D GLWidget::getSpecularLight() const
{
    return _specularLight;
}

void GLWidget::setSpecularLight(const QVector4D& specularLight)
{
    _specularLight = specularLight;
}

QVector4D GLWidget::getDiffuseLight() const
{
    return _diffuseLight;
}

void GLWidget::setDiffuseLight(const QVector4D& diffuseLight)
{
    _diffuseLight = diffuseLight;
}

QVector4D GLWidget::getAmbientLight() const
{
    return _ambientLight;
}

void GLWidget::setAmbientLight(const QVector4D& ambientLight)
{
    _ambientLight = ambientLight;
}

void GLWidget::setViewMode(ViewMode mode)
{
    if (!_animateViewTimer->isActive())
    {
        _animateViewTimer->start(5);
        _viewMode = mode;
        _slerpStep = 0.0f;
    }
}

void GLWidget::fitAll()
{
    _viewBoundingSphereDia = _boundingSphere.getRadius() * 2;

    if (!_animateFitAllTimer->isActive())
    {
        _animateFitAllTimer->start(5);
        _slerpStep = 0.0f;
    }
}

void GLWidget::beginWindowZoom()
{
    _bWindowZoomActive = true;
    setCursor(QCursor(QPixmap(":/new/prefix1/res/window-zoom-cursor.png"), 12, 12));
}

void GLWidget::endWindowZoom()
{
    _bWindowZoomActive = false;
    if (_rubberBand)
    {
        QVector3D Z(0, 0, 0); // instead of 0 for x and y we need worldPosition.x() and worldPosition.y() ....
        Z = Z.project(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

        QRect clientRect = geometry();
        QPoint clientWinCen = clientRect.center();
        QVector3D o(clientWinCen.x(), height() - clientWinCen.y(), Z.z());
        QVector3D O = o.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

        QRect zoomRect = _rubberBand->geometry();
        if(zoomRect.width() == 0 || zoomRect.height() == 0)
        {
            emit windowZoomEnded();
            return;
        }
        QPoint zoomWinCen = zoomRect.center();
        QVector3D p(zoomWinCen.x(), height() - zoomWinCen.y(), Z.z());
        QVector3D P = p.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

        double widthRatio = static_cast<double>(clientRect.width() / zoomRect.width());
        double heightRatio = static_cast<double>(clientRect.height() / zoomRect.height());
        _rubberBandZoomRatio = (heightRatio < widthRatio) ? heightRatio : widthRatio;
        _rubberBandPan = P - O;
    }
    if (!_animateWindowZoomTimer->isActive())
    {
        _animateWindowZoomTimer->start(5);
        _slerpStep = 0.0f;
    }
    emit windowZoomEnded();
}

void GLWidget::setProjection(ViewProjection proj)
{
    _projection = proj;
    resizeGL(width(), height());
}

void GLWidget::setDisplayList(const std::vector<int>& ids)
{
    _displayedObjectsIds = ids;
    _currentTranslation = _camera->getPosition();
    _boundingSphere.setCenter(0, 0, 0);

    if (_meshStore.size() == 0)
    {
        _boundingSphere.setRadius(1.0);
        _viewBoundingSphereDia = _boundingSphere.getRadius() * 2;
        _viewRange = _viewBoundingSphereDia;
        _currentViewRange = _viewRange;
        return;
    }
    else if (ids.size() == 0)
    {
        _camera->setPosition(0, 0, 0);
        _currentTranslation = _camera->getPosition();
        _boundingSphere.setRadius(1.0);
        _viewBoundingSphereDia = _boundingSphere.getRadius() * 2;
        _viewRange = _viewBoundingSphereDia;
        _currentViewRange = _viewRange;
    }
    else
    {
        _boundingSphere.setRadius(0.0);

        for (int i : _displayedObjectsIds)
        {
            try
            {
                _boundingSphere.addSphere(_meshStore.at(i)->getBoundingSphere());
            }
            catch (std::out_of_range& ex)
            {
                std::cout << ex.what() << std::endl;
            }
        }
    }

    if(_floorPlane)
    {
        _floorSize = _boundingSphere.getRadius();
        _floorCenter = _boundingSphere.getCenter();
        _lightPosition.setX(_floorSize/4);
        _lightPosition.setY(_floorSize/4);
        _lightPosition.setZ(_floorSize);
        _prevLightPosition = _lightPosition;
        _floorPlane->setPlane(_fgShader, _floorCenter, _floorSize * 5.0f, _floorSize * 5.0f, 1500, 1500, lowestModelZ() - (_floorSize* 0.05f), 1, 1);
    }

    fitAll();
    update();

    emit displayListSet();
}

void GLWidget::updateBoundingSphere()
{
    _currentTranslation = _camera->getPosition();
    _boundingSphere.setCenter(0, 0, 0);

    _boundingSphere.setRadius(0.0);

    for (int i : _displayedObjectsIds)
    {
        try
        {
            _boundingSphere.addSphere(_meshStore.at(i)->getBoundingSphere());
        }
        catch (std::out_of_range& ex)
        {
            std::cout << ex.what() << std::endl;
        }
    }

    if (_floorPlane)
    {
        _floorSize = _boundingSphere.getRadius();
        _floorCenter = _boundingSphere.getCenter();
        _lightPosition.setX(_floorSize/4);
        _lightPosition.setY(_floorSize/4);
        _lightPosition.setZ(_floorSize);
        _prevLightPosition = _lightPosition;
        _floorPlane->setPlane(_fgShader, _floorCenter, _floorSize * 5.0f, _floorSize * 5.0f, 1500, 1500, lowestModelZ() - (_floorSize* 0.05f), 1, 1);
    }

    fitAll();
    update();
}

void GLWidget::showClippingPlaneEditor(bool show)
{
    if (!_clippingPlanesEditor)
    {
        _clippingPlanesEditor = new ClippingPlanesEditor(this);
    }

    _lowerLayout->addWidget(_clippingPlanesEditor);

    show ? _clippingPlanesEditor->show() : _clippingPlanesEditor->hide();
}

void GLWidget::showAxis(bool show)
{
    _bShowAxis = show;
    update();
}

void GLWidget::showShadows(bool show)
{
    _shadowsEnabled = show;
    update();
}

void GLWidget::showEnvironment(bool show)
{
    _envMapEnabled = show;
    update();
}

void GLWidget::showSkyBox(bool show)
{
    _skyBoxEnabled = show;
    update();
}

void GLWidget::showReflections(bool show)
{
    _reflectionsEnabled = show;
    update();
}

void GLWidget::showFloor(bool show)
{
    _floorDisplayed = show;
    update();
}


void GLWidget::addToDisplay(TriangleMesh* mesh)
{
    _meshStore.push_back(mesh);
}

void GLWidget::removeFromDisplay(int index)
{
    TriangleMesh* mesh = _meshStore[index];
    _meshStore.erase(_meshStore.begin() + index);
    delete mesh;
}

void GLWidget::centerScreen(int index)
{
    _centerScreenObjectId = index;
    if (!_animateCenterScreenTimer->isActive())
    {
        _animateCenterScreenTimer->start(5);
        _slerpStep = 0.0f;
    }
}

void GLWidget::select(int id)
{
    try {
        _meshStore.at(id)->select();
    }
    catch (std::exception& ex) {
        std::cout << "Exception raised in GLWidget::select\n" << ex.what() << std::endl;
    }
}

void GLWidget::deselect(int id)
{
    try {
        _meshStore.at(id)->deselect();
    }
    catch (std::exception& ex) {
        std::cout << "Exception raised in GLWidget::select\n" << ex.what() << std::endl;
    }
}

TriangleMesh* GLWidget::loadSTLMesh(QString fileName)
{
    makeCurrent();
    STLMesh* mesh = new STLMesh(_fgShader, fileName);
    if (mesh)
        addToDisplay(mesh);
    return mesh;
}

TriangleMesh* GLWidget::loadOBJMesh(QString fileName)
{
    makeCurrent();
    std::unique_ptr<ObjMesh> obj = ObjMesh::load(_fgShader, fileName.toLocal8Bit().data());
    TriangleMesh* mesh = static_cast<TriangleMesh*>(obj.release());
    if (mesh)
        addToDisplay(mesh);
    return mesh;
}

void GLWidget::setMaterialProps(const std::vector<int>& ids, const GLMaterialProps& mat)
{
    for (int id : ids)
    {
        try
        {
            TriangleMesh* mesh = _meshStore[id];
            mesh->setAmbientMaterial(mat.ambientMaterial);
            mesh->setDiffuseMaterial(mat.diffuseMaterial);
            mesh->setSpecularMaterial(mat.specularMaterial);
            mesh->setEmmissiveMaterial(mat.emmissiveMaterial);
            mesh->setSpecularReflectivity(mat.specularReflectivity);
            mesh->setShininess(mat.shininess);
            mesh->setOpacity(mat.opacity);
            mesh->enableTexture(mat.bHasTexture);
        }
        catch (...)
        {
            std::cout << "Exception!" << std::endl;
        }
    }
}

void GLWidget::setTransformation(const std::vector<int>& ids, const QMatrix4x4& mat)
{
    for (int id : ids)
    {
        try
        {
            TriangleMesh* mesh = _meshStore[id];
            mesh->setTransformation(mat);
        }
        catch (...)
        {
            std::cout << "Exception!" << std::endl;
        }
    }
    updateBoundingSphere();
}

void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    cout << "Vendor:   " << glGetString(GL_VENDOR) << '\n';
    cout << "OpenGL Version:  " << glGetString(GL_VERSION) << '\n';
    cout << "Shader Version:   " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n"
         << endl;

    /*
    int n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (int i = 0; i < n; i++)
    {
        const char* extension =
                (const char*)glGetStringi(GL_EXTENSIONS, i);
        printf("GL Extension %d: %s\n", i, extension);
    }
    std::cout << std::endl;
    */

    makeCurrent();

    createShaderPrograms();

    // Environment Mapping
    loadEnvMap();

    // Shadow mapping
    loadFloor();

    // reflection mapping
    loadReflectionMap();

    createGeometry();

    _textShader.bind();
    _textRenderer = new TextRenderer(&_textShader, width(), height());
    _textRenderer->Load("fonts/arial.ttf", 20);
    _axisTextRenderer = new TextRenderer(&_textShader, width(), height());
    _axisTextRenderer->Load("fonts/arialbd.ttf", 16);
    _textShader.release();

    // Set lighting information
    _fgShader->bind();
    _fgShader->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
    _fgShader->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
    _fgShader->setUniformValue("lightSource.specular", _specularLight.toVector3D());
    _fgShader->setUniformValue("lightSource.position", _lightPosition);
    _fgShader->setUniformValue("lightModel.ambient", QVector3D(0.2f, 0.2f, 0.2f));
    _fgShader->setUniformValue("texUnit", 0);
    _fgShader->setUniformValue("envMap", 1);
    _fgShader->setUniformValue("shadowMap", 2);
    _fgShader->setUniformValue("reflectionMap", 3);
    _fgShader->setUniformValue("reflectionDepthMap", 4);

    _debugShader.bind();
    _debugShader.setUniformValue("depthMap", 0);

    _viewMatrix.setToIdentity();
    glEnable(GL_DEPTH_TEST);

    glClearColor(0.0f, 0.0f, 0.0f, 1.f);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GLWidget::createShaderPrograms()
{
    // Foreground objects shader program
    // per fragment lighting
    if (!_fgShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/twoside_per_fragment.vert"))
    {
        qDebug() << "Error in vertex shader:" << _fgShader->log();
    }
    if (!_fgShader->addShaderFromSourceFile(QOpenGLShader::Geometry, "shaders/twoside_per_fragment.geom"))
    {
        qDebug() << "Error in geometry shader:" << _fgShader->log();
    }
    if (!_fgShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/twoside_per_fragment.frag"))
    {
        qDebug() << "Error in fragment shader:" << _fgShader->log();
    }
    if (!_fgShader->link())
    {
        qDebug() << "Error linking shader program:" << _fgShader->log();
    }

    // Axis
    if (!_axisShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/axis.vert"))
    {
        qDebug() << "Error in vertex shader:" << _axisShader->log();
    }
    if (!_axisShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/axis.frag"))
    {
        qDebug() << "Error in geometry shader:" << _axisShader->log();
    }
    if (!_axisShader->link())
    {
        qDebug() << "Error linking shader program:" << _axisShader->log();
    }

    // Vertex Normal
    if (!_vertexNormalShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/vertex_normal.vert"))
    {
        qDebug() << "Error in vertex shader:" << _vertexNormalShader->log();
    }
    if (!_vertexNormalShader->addShaderFromSourceFile(QOpenGLShader::Geometry, "shaders/vertex_normal.geom"))
    {
        qDebug() << "Error in geometry shader:" << _vertexNormalShader->log();
    }
    if (!_vertexNormalShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/vertex_normal.frag"))
    {
        qDebug() << "Error in fragment shader:" << _vertexNormalShader->log();
    }
    if (!_vertexNormalShader->link())
    {
        qDebug() << "Error linking shader program:" << _vertexNormalShader->log();
    }

    // Face Normal
    if (!_faceNormalShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/face_normal.vert"))
    {
        qDebug() << "Error in vertex shader:" << _faceNormalShader->log();
    }
    if (!_faceNormalShader->addShaderFromSourceFile(QOpenGLShader::Geometry, "shaders/face_normal.geom"))
    {
        qDebug() << "Error in geometry shader:" << _faceNormalShader->log();
    }
    if (!_faceNormalShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/face_normal.frag"))
    {
        qDebug() << "Error in fragment shader:" << _faceNormalShader->log();
    }
    if (!_faceNormalShader->link())
    {
        qDebug() << "Error linking shader program:" << _faceNormalShader->log();
    }

    // Shadow mapping
    if (!_shadowMappingShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/shadow_mapping_depth.vert"))
    {
        qDebug() << "Error in vertex shader:" << _shadowMappingShader->log();
    }
    if (!_shadowMappingShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/shadow_mapping_depth.frag"))
    {
        qDebug() << "Error in fragment shader:" << _shadowMappingShader->log();
    }
    if (!_shadowMappingShader->link())
    {
        qDebug() << "Error linking shader program:" << _shadowMappingShader->log();
    }

    // Sky Box
    if (!_skyBoxShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/skybox.vert"))
    {
        qDebug() << "Error in vertex shader:" << _skyBoxShader->log();
    }
    if (!_skyBoxShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/skybox.frag"))
    {
        qDebug() << "Error in fragment shader:" << _skyBoxShader->log();
    }
    if (!_skyBoxShader->link())
    {
        qDebug() << "Error linking shader program:" << _skyBoxShader->log();
    }

    //_reflectionMappingShader
    if (!_reflectionMappingShader->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/reflection_mapping.vert"))
    {
        qDebug() << "Error in vertex shader:" << _reflectionMappingShader->log();
    }
    if (!_reflectionMappingShader->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/reflection_mapping.frag"))
    {
        qDebug() << "Error in fragment shader:" << _reflectionMappingShader->log();
    }
    if (!_reflectionMappingShader->link())
    {
        qDebug() << "Error linking shader program:" << _reflectionMappingShader->log();
    }

    // Text shader program
    if (!_textShader.addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/text.vert"))
    {
        qDebug() << "Error in vertex shader:" << _textShader.log();
    }
    if (!_textShader.addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/text.frag"))
    {
        qDebug() << "Error in fragment shader:" << _textShader.log();
    }
    if (!_textShader.link())
    {
        qDebug() << "Error linking shader program:" << _textShader.log();
    }

    // Background gradient shader program
    if (!_bgShader.addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/background.vert"))
    {
        qDebug() << "Error in vertex shader:" << _bgShader.log();
    }
    if (!_bgShader.addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/background.frag"))
    {
        qDebug() << "Error in fragment shader:" << _bgShader.log();
    }
    if (!_bgShader.link())
    {
        qDebug() << "Error linking shader program:" << _bgShader.log();
    }

    // Background split shader program
    if (!_bgSplitShader.addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/splitScreen.vert"))
    {
        qDebug() << "Error in vertex shader:" << _bgSplitShader.log();
    }
    if (!_bgSplitShader.addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/splitScreen.frag"))
    {
        qDebug() << "Error in fragment shader:" << _bgSplitShader.log();
    }
    if (!_bgSplitShader.link())
    {
        qDebug() << "Error linking shader program:" << _bgSplitShader.log();
    }

    //_debugShader
    // Background split shader program
    if (!_debugShader.addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/debug_quad.vert"))
    {
        qDebug() << "Error in vertex shader:" << _debugShader.log();
    }
    if (!_debugShader.addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/debug_quad_depth.frag"))
    {
        qDebug() << "Error in fragment shader:" << _debugShader.log();
    }
    if (!_debugShader.link())
    {
        qDebug() << "Error linking shader program:" << _debugShader.log();
    }
}

void GLWidget::createGeometry()
{
    _meshStore.push_back(new Cube(_fgShader, 100.0f));
    _meshStore.push_back(new Sphere(_fgShader, 75.0f, 50.0f, 50.0f));
    _meshStore.push_back(new Cylinder(_fgShader, 60.0f, 100.0f, 100.0f, 50.0f));
    _meshStore.push_back(new Cone(_fgShader, 60.0f, 100.0f, 100.0f, 50.0f));
    _meshStore.push_back(new Torus(_fgShader, 50.0f, 25.0f, 100.0f, 100.0f));
    _meshStore.push_back(new Teapot(_fgShader, 35.0f, 50, glm::translate(mat4(1.0f), vec3(0.0f, 15.0f, 25.0f))));
    _meshStore.push_back(new KleinBottle(_fgShader, 30.0f, 150.0f, 150.0f));
    _meshStore.push_back(new Figure8KleinBottle(_fgShader, 30.0f, 150.0f, 150.0f));
    _meshStore.push_back(new BoySurface(_fgShader, 60.0f, 250.0f, 250.0f));
    _meshStore.push_back(new TwistedTriaxial(_fgShader, 110.0f, 150.0f, 150.0f));
    _meshStore.push_back(new SteinerSurface(_fgShader, 150.0f, 150.0f, 150.0f));
    _meshStore.push_back(new AppleSurface(_fgShader, 7.5f, 150.0f, 150.0f));
    _meshStore.push_back(new DoubleCone(_fgShader, 35.0f, 150.0f, 150.0f));
    _meshStore.push_back(new BentHorns(_fgShader, 15.0f, 150.0f, 150.0f));
    _meshStore.push_back(new Folium(_fgShader, 75.0f, 150.0f, 150.0f));
    _meshStore.push_back(new LimpetTorus(_fgShader, 35.0f, 150.0f, 150.0f));
    _meshStore.push_back(new SaddleTorus(_fgShader, 30.0f, 150.0f, 150.0f));
    _meshStore.push_back(new BowTie(_fgShader, 40.0f, 150.0f, 150.0f));
    _meshStore.push_back(new TriaxialTritorus(_fgShader, 45.0f, 150.0f, 150.0f));
    _meshStore.push_back(new TriaxialHexatorus(_fgShader, 45.0f, 150.0f, 150.0f));
    _meshStore.push_back(new VerrillMinimal(_fgShader, 25.0f, 150.0f, 150.0f));
    _meshStore.push_back(new Horn(_fgShader, 30.0f, 150.0f, 150.0f));
    _meshStore.push_back(new Crescent(_fgShader, 30.0f, 150.0f, 150.0f));
    _meshStore.push_back(new ConeShell(_fgShader, 45.0f, 150.0f, 150.0f));
    _meshStore.push_back(new Periwinkle(_fgShader, 40.0f, 150.0f, 150.0f));
    _meshStore.push_back(new TopShell(_fgShader, Point(-50, 0, 0), 35.0f, 250.0f, 150.0f));
    _meshStore.push_back(new WrinkledPeriwinkle(_fgShader, 45.0f, 150.0f, 150.0f));
    _meshStore.push_back(new SpindleShell(_fgShader, 25.0f, 150.0f, 150.0f));
    _meshStore.push_back(new TurretShell(_fgShader, 20.0f, 250.0f, 150.0f));
    _meshStore.push_back(new TwistedPseudoSphere(_fgShader, 50.0f, 150.0f, 150.0f));
    _meshStore.push_back(new BreatherSurface(_fgShader, 15.0f, 150.0f, 150.0f));

    Spring* spring = new Spring(_fgShader, 10.0f, 30.0f, 10.0f, 2.0f, 150.0f, 150.0f);
    _meshStore.push_back(spring);
    _springEditor = new SpringEditor(spring, this);
    _upperLayout->addWidget(_springEditor);

    SuperToroid* storoid = new SuperToroid(_fgShader, 50, 25, 1, 1, 150.0f, 150.0f);
    _meshStore.push_back(storoid);
    _superToroidEditor = new SuperToroidEditor(storoid, this);
    _upperLayout->addWidget(_superToroidEditor);

    SuperEllipsoid* sellipsoid = new SuperEllipsoid(_fgShader, 50, 1.0, 1.0, 1.0, 1.0, 1.0, 150.0f, 150.0f);
    _meshStore.push_back(sellipsoid);
    _superEllipsoidEditor = new SuperEllipsoidEditor(sellipsoid, this);
    _upperLayout->addWidget(_superEllipsoidEditor);

    GraysKlein* gklein = new GraysKlein(_fgShader, 30.0f, 150.0f, 150.0f);
    _meshStore.push_back(gklein);
    _graysKleinEditor = new GraysKleinEditor(gklein, this);
    _upperLayout->addWidget(_graysKleinEditor);

    SphericalHarmonic* sph = new SphericalHarmonic(_fgShader, 30.0f, 150.0f, 150.0f);
    _meshStore.push_back(sph);
    _sphericalHarmonicsEditor = new SphericalHarmonicsEditor(sph, this);
    _upperLayout->addWidget(_sphericalHarmonicsEditor);

#ifdef WIN32
    STLMesh* mesh = new STLMesh(_fgShader, QString("D:/work/progs/qt5/ModelViewer-1.0/data/Logo.stl"));
#else
    STLMesh* mesh = new STLMesh(_fgShader, QString("/home/sharjith/work/progs/qt/stlviewer/data/Logo.stl"));
#endif
    if (mesh && mesh->loaded())
        _meshStore.push_back(mesh);
}

void GLWidget::loadFloor()
{
    // configure depth map FBO
    // -----------------------
    // create depth texture
    if (_shadowMap == 0)
    {
        glGenTextures(1, &_shadowMap);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, _shadowMap);
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        // attach depth texture as FBO's depth buffer
        glGenFramebuffers(1, &_shadowMapFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _shadowMapFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowMap, 0);
        unsigned long status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Frame buffer creation failed!" << std::endl;
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    }

    // Floor texture
    if (!_texBuffer.load(QString("textures/envmap/Granite#3577.jpg")))
    { // Load first image from file
        qWarning("Could not read image file, using single-color instead.");
        QImage dummy(128, 128, static_cast<QImage::Format>(5));
        dummy.fill(Qt::white);
        _texBuffer = dummy;
    }
    else
    {
        _texImage = QGLWidget::convertToGLFormat(_texBuffer); // flipped 32bit RGBA
    }

    _floorSize = _boundingSphere.getRadius();
    _floorCenter = _boundingSphere.getCenter();
    _lightPosition.setZ(_floorSize);
    _floorPlane = new Plane(_fgShader, _floorCenter, _floorSize * 5.0f, _floorSize * 5.0f, 1500, 1500, -_floorSize - (_floorSize* 0.05f), 1, 1);

    _floorPlane->setAmbientMaterial(QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
    _floorPlane->setDiffuseMaterial(QVector4D(1.0f, 1.0f, 1.0f, 1.0f));
    _floorPlane->setSpecularMaterial(QVector4D(0.5f, 0.5f, 0.5f, 1.0f));
    _floorPlane->setShininess(32.0f);
    _floorPlane->enableTexture(true);
    _floorPlane->setTexureImage(_texImage);
    //_floorPlane->setOpacity(0.95f);
}

void GLWidget::loadReflectionMap()
{
    // configure reflection map FBO
    // -----------------------
    // create reflection texture
    if (_reflectionMap == 0)
    {
        // Reflection Texture
        glGenTextures(1, &_reflectionMap);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, _reflectionMap);
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _shadowWidth * 4.0f, _shadowHeight * 4.0f, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _shadowWidth * 4.0f, _shadowHeight * 4.0f, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        //glTexImage2D(GL_TEXTURE_2D, 0, 3, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        // Attach color texture as FBO's color buffer
        glGenFramebuffers(1, &_reflectionMapFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _reflectionMapFBO);
        // Set "renderedTexture" as our colour attachement #0
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, _reflectionMap, 0);
        unsigned long status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Frame buffer _reflectionMapFBO creation failed!" << std::endl;
    }
    // create reflection depth texture
    if(_reflectionDepthMap == 0)
    {
        // Reflection Depth Texture
        glGenTextures(1, &_reflectionDepthMap);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, _reflectionDepthMap);
        //glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);
        //glTexImage2D(GL_TEXTURE_2D, 0, 3, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        
        // Attach depth texture as FBO's depth buffer
        glGenFramebuffers(1, &_reflectionMapDBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _reflectionMapDBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _reflectionDepthMap, 0);
        unsigned long status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Frame buffer _reflectionMapDBO creation failed!" << std::endl;
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
}

void GLWidget::loadEnvMap()
{

    /*vector<QString> faces
    {
        QString("textures/envmap/lposx.png"),
                QString("textures/envmap/lnegx.png"),
                QString("textures/envmap/lposz.png"),
                QString("textures/envmap/lnegz.png"),
                QString("textures/envmap/lposy.png"),
                QString("textures/envmap/lnegy.png")
    };*/

    // Env Map

    vector<QString> faces
    {
        QString("textures/envmap/miramar_ft.png"),
                QString("textures/envmap/miramar_bk.png"),
                QString("textures/envmap/miramar_rt.png"),
                QString("textures/envmap/miramar_lf.png"),
                QString("textures/envmap/miramar_up.png"),
                QString("textures/envmap/miramar_dn.png")
    };


    glGenTextures(1, &_environmentMap);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

    for (unsigned int i = 0; i < faces.size(); i++)
    {
        if (!_texBuffer.load(faces.at(i)))
        { // Load first image from file
            qWarning("Could not read image file, using single-color instead.");
            QImage dummy(128, 128, static_cast<QImage::Format>(5));
            dummy.fill(Qt::white);
            _texBuffer = dummy;
        }
        else
        {
            _texImage = QGLWidget::convertToGLFormat(_texBuffer); // flipped 32bit RGBA
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, _texImage.width(), _texImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, _texImage.bits());
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

    _skyBox = new Cube(_skyBoxShader, 1);
    _skyBoxShader->bind();
    _skyBoxShader->setUniformValue("skybox", 1);
}

void GLWidget::resizeGL(int width, int height)
{
    float w = (float)width;
    float h = (float)height;

    glViewport(0, 0, w, h);
    _viewportMatrix = QMatrix4x4(w / 2, 0.0f, 0.0f, 0.0f,
                                 0.0f, h / 2, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f,
                                 w / 2 + 0, h / 2 + 0, 0.0f, 1.0f);

    _projectionMatrix.setToIdentity();
    _camera->setScreenSize(w, h);
    _camera->setViewRange(_viewRange);
    if (_projection == ViewProjection::ORTHOGRAPHIC)
    {
        _camera->setProjection(GLCamera::ProjectionType::ORTHOGRAPHIC);
    }
    else
    {
        _camera->setProjection(GLCamera::ProjectionType::PERSPECTIVE);
    }
    _projectionMatrix = _camera->getProjectionMatrix();

    // Resize the text frame
    _textRenderer->setWidth(width);
    _textRenderer->setHeight(height);
    QMatrix4x4 projection;
    projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)));
    _textShader.bind();
    _textShader.setUniformValue("projection", projection);
    _textShader.release();

    update();
}

void GLWidget::paintGL()
{
    try
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);


        gradientBackground(_bgTopColor.redF(), _bgTopColor.greenF(), _bgTopColor.blueF(), _bgTopColor.alphaF(),
                           _bgBotColor.redF(), _bgBotColor.greenF(), _bgBotColor.blueF(), _bgBotColor.alphaF());

        //gradientBackground(0.8515625f, 0.8515625f, 0.8515625f, 1.0f,
        //                 0.8515625f, 0.8515625f, 0.8515625f, 1.0f);

        _modelMatrix.setToIdentity();
        if (_bMultiView)
        {
            // Top View
            _projectionMatrix.setToIdentity();
            _viewMatrix.setToIdentity();
            _modelMatrix.setToIdentity();
            //glViewport(0, height() / 2, width() / 2, height() / 2);
            _camera->setScreenSize(width() / 2, height() / 2);
            glViewport(0, 0, width() / 2, height() / 2);
            _camera->setView(GLCamera::ViewProjection::TOP_VIEW);
            _projectionMatrix = _camera->getProjectionMatrix();
            _viewMatrix = _camera->getViewMatrix();
            render();
            _textRenderer->RenderText("Top", -50, 5, 1.6f, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

            // Front View
            _projectionMatrix.setToIdentity();
            _viewMatrix.setToIdentity();
            _modelMatrix.setToIdentity();
            glViewport(0, height() / 2, width() / 2, height() / 2);
            _camera->setView(GLCamera::ViewProjection::FRONT_VIEW);
            _projectionMatrix = _camera->getProjectionMatrix();
            _viewMatrix = _camera->getViewMatrix();
            render();
            _textRenderer->RenderText("Front", -50, 5, 1.6f, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

            // Left View
            _projectionMatrix.setToIdentity();
            _viewMatrix.setToIdentity();
            _modelMatrix.setToIdentity();
            glViewport(width() / 2, height() / 2, width() / 2, height() / 2);
            _camera->setView(GLCamera::ViewProjection::LEFT_VIEW);
            _projectionMatrix = _camera->getProjectionMatrix();
            _viewMatrix = _camera->getViewMatrix();
            render();
            _textRenderer->RenderText("Left", -50, 5, 1.6f, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

            // Isometric View
            _projectionMatrix.setToIdentity();
            _viewMatrix.setToIdentity();
            _modelMatrix.setToIdentity();
            glViewport(width() / 2, 0, width() / 2, height() / 2);
            _camera->setView(GLCamera::ViewProjection::SE_ISOMETRIC_VIEW);
            _projectionMatrix = _camera->getProjectionMatrix();
            _viewMatrix = _camera->getViewMatrix();
            render();
            _textRenderer->RenderText("Isometric", -50, 5, 1.6f, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VTOP, TextRenderer::HAlignment::HRIGHT);

            // draw screen partitioning lines
            splitScreen();
        }
        else
        {
            QMatrix4x4 projection;
            projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(width()), static_cast<float>(height())));
            _textShader.bind();
            _textShader.setUniformValue("projection", projection);
            _textShader.release();
            glViewport(0, 0, width(), height());
            renderToShadowBuffer();
            renderToReflectionMap();
            renderToReflectionDepthMap();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            gradientBackground(_bgTopColor.redF(), _bgTopColor.greenF(), _bgTopColor.blueF(), _bgTopColor.alphaF(),
                               _bgBotColor.redF(), _bgBotColor.greenF(), _bgBotColor.blueF(), _bgBotColor.alphaF());
            render();
            drawCornerAxis();
        }

        // Text rendering
        if (_meshStore.size() != 0 && _displayedObjectsIds.size() != 0)
        {
            int num = _displayedObjectsIds.at(0);
            _textRenderer->RenderText(_meshStore.at(num)->getName().toStdString(), 4, 4, 1, glm::vec3(1.0f, 1.0f, 0.0f));
        }

        if (_meshStore.size() && _displayedObjectsIds.size() != 0)
        {
            int num = _displayedObjectsIds[0];
            // Display Harmonics Editor
            if (dynamic_cast<SphericalHarmonic*>(_meshStore.at(num)))
                _sphericalHarmonicsEditor->show();
            else
                _sphericalHarmonicsEditor->hide();

            // Display Gray's Klein Editor
            if (dynamic_cast<GraysKlein*>(_meshStore.at(num)))
                _graysKleinEditor->show();
            else
                _graysKleinEditor->hide();

            // Display Super Toroid Editor
            if (dynamic_cast<SuperToroid*>(_meshStore.at(num)))
                _superToroidEditor->show();
            else
                _superToroidEditor->hide();

            // Display Super Ellipsoid Editor
            if (dynamic_cast<SuperEllipsoid*>(_meshStore.at(num)))
                _superEllipsoidEditor->show();
            else
                _superEllipsoidEditor->hide();

            // Display Spring Editor
            if (dynamic_cast<Spring*>(_meshStore.at(num)))
                _springEditor->show();
            else
                _springEditor->hide();
        }
    }
    catch (const std::exception& ex)
    {
        std::cout << "Exception raised in GLWidget::paintGL\n" << ex.what() << std::endl;
    }


    _debugShader.bind();
    _debugShader.setUniformValue("near_plane", 1.0f);
    _debugShader.setUniformValue("far_plane", _viewRange);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _reflectionMap);
    //renderQuad();
}

void GLWidget::drawFloor()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    _fgShader->bind();
    _fgShader->setUniformValue("envMapEnabled", true);
    _fgShader->setUniformValue("reflectionMapEnabled", _reflectionsEnabled);
    _fgShader->setUniformValue("shadowSamples", 25.0f);
    _floorPlane->render();
    glDisable(GL_CULL_FACE);
    glDisable((GL_DEPTH_TEST));
}

void GLWidget::drawSkyBox()
{
    _skyBoxShader->bind();
    QMatrix4x4 projection;
    projection.perspective(65, (float)width() / (float)height(), 0.1f, 100.0f);
    QMatrix4x4 view = _viewMatrix;
    // Remove translation
    view.setColumn(3, QVector4D(0, 0, 0, 1));
    _skyBoxShader->setUniformValue("viewMatrix", view);
    _skyBoxShader->setUniformValue("projectionMatrix", projection);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL); // change depth function so depth test passes when values are equal to depth buffer's content
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    _skyBox->render();
    glDepthFunc(GL_LESS); // set depth function back to default
    glDisable((GL_DEPTH_TEST));
}

void GLWidget::drawMesh()
{
    QVector3D pos = _camera->getPosition();

    if (_clipXEnabled || _clipYEnabled || _clipZEnabled || !(_clipDX == 0 && _clipDY == 0 && _clipDZ == 0))
    {
        _fgShader->setUniformValue("sectionActive", true);
    }
    else
    {
        _fgShader->setUniformValue("sectionActive", false);
    }

    _fgShader->setUniformValue("clipPlaneX", QVector4D(_modelViewMatrix * (QVector3D(_clipXFlipped ? -1 : 1, 0, 0) + pos),
                                                       (_clipXFlipped ? -1 : 1) * pos.x() + _clipXCoeff));
    _fgShader->setUniformValue("clipPlaneY", QVector4D(_modelViewMatrix * (QVector3D(0, _clipYFlipped ? -1 : 1, 0) + pos),
                                                       (_clipYFlipped ? -1 : 1) * pos.y() + _clipYCoeff));
    _fgShader->setUniformValue("clipPlaneZ", QVector4D(_modelViewMatrix * (QVector3D(0, 0, _clipZFlipped ? -1 : 1) + pos),
                                                       (_clipZFlipped ? -1 : 1) * pos.z() + _clipZCoeff));
    _fgShader->setUniformValue("clipPlane", QVector4D(_modelViewMatrix * (QVector3D(_clipDX, _clipDY, _clipDZ) + pos),
                                                      pos.x() * _clipDX + pos.y() * _clipDY + pos.z() * _clipDZ));
    _fgShader->setUniformValue("shadowSamples", 150.0f);
    // Render
    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            try
            {
                TriangleMesh* mesh = _meshStore.at(i);
                if (mesh)
                {
                    mesh->setProg(_fgShader);
                    mesh->render();
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Exception raised in GLWidget::drawMesh\n" << ex.what() << std::endl;
            }
        }
    }
}

void GLWidget::drawVertexNormals()
{
    QVector3D pos = _camera->getPosition();
    _vertexNormalShader->bind();
    _vertexNormalShader->setUniformValue("modelViewMatrix", _modelViewMatrix);
    _vertexNormalShader->setUniformValue("projectionMatrix", _projectionMatrix);
    _vertexNormalShader->setUniformValue("clipPlaneX", QVector4D(_modelViewMatrix * (QVector3D(_clipXFlipped ? -1 : 1, 0, 0) + pos),
                                                                 (_clipXFlipped ? -1 : 1) * pos.x() + _clipXCoeff));
    _vertexNormalShader->setUniformValue("clipPlaneY", QVector4D(_modelViewMatrix * (QVector3D(0, _clipYFlipped ? -1 : 1, 0) + pos),
                                                                 (_clipYFlipped ? -1 : 1) * pos.y() + _clipYCoeff));
    _vertexNormalShader->setUniformValue("clipPlaneZ", QVector4D(_modelViewMatrix * (QVector3D(0, 0, _clipZFlipped ? -1 : 1) + pos),
                                                                 (_clipZFlipped ? -1 : 1) * pos.z() + _clipZCoeff));
    _vertexNormalShader->setUniformValue("clipPlane", QVector4D(_modelViewMatrix * (QVector3D(_clipDX, _clipDY, _clipDZ) + pos),
                                                                pos.x() * _clipDX + pos.y() * _clipDY + pos.z() * _clipDZ));
    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            if (_showVertexNormals)
            {
                TriangleMesh* mesh = _meshStore.at(i);
                _vertexNormalShader->bind();
                _vertexNormalShader->setUniformValue("modelSize", static_cast<float>(mesh->getBoundingSphere().getRadius() / 2));
                mesh->setProg(_vertexNormalShader);
                mesh->render();
            }
        }
    }
}

void GLWidget::drawFaceNormals()
{
    QVector3D pos = _camera->getPosition();
    _faceNormalShader->bind();
    _faceNormalShader->setUniformValue("modelViewMatrix", _modelViewMatrix);
    _faceNormalShader->setUniformValue("projectionMatrix", _projectionMatrix);
    _faceNormalShader->setUniformValue("clipPlaneX", QVector4D(_modelViewMatrix * (QVector3D(_clipXFlipped ? -1 : 1, 0, 0) + pos),
                                                               (_clipXFlipped ? -1 : 1) * pos.x() + _clipXCoeff));
    _faceNormalShader->setUniformValue("clipPlaneY", QVector4D(_modelViewMatrix * (QVector3D(0, _clipYFlipped ? -1 : 1, 0) + pos),
                                                               (_clipYFlipped ? -1 : 1) * pos.y() + _clipYCoeff));
    _faceNormalShader->setUniformValue("clipPlaneZ", QVector4D(_modelViewMatrix * (QVector3D(0, 0, _clipZFlipped ? -1 : 1) + pos),
                                                               (_clipZFlipped ? -1 : 1) * pos.z() + _clipZCoeff));
    _faceNormalShader->setUniformValue("clipPlane", QVector4D(_modelViewMatrix * (QVector3D(_clipDX, _clipDY, _clipDZ) + pos),
                                                              pos.x() * _clipDX + pos.y() * _clipDY + pos.z() * _clipDZ));

    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            if (_showFaceNormals)
            {
                TriangleMesh* mesh = _meshStore.at(i);
                _faceNormalShader->bind();
                _faceNormalShader->setUniformValue("modelSize", static_cast<float>(mesh->getBoundingSphere().getRadius() / 2));
                mesh->setProg(_faceNormalShader);
                mesh->render();
            }
        }
    }
}

void GLWidget::drawAxis()
{
    float size = 15;
    // Labels
    QVector3D xAxis(_viewRange / size, 0, 0);
    xAxis = xAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("X", xAxis.x(), height() - xAxis.y(), 1, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    QVector3D yAxis(0, _viewRange / size, 0);
    yAxis = yAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("Y", yAxis.x(), height() - yAxis.y(), 1, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    QVector3D zAxis(0, 0, _viewRange / size);
    zAxis = zAxis.project(_modelViewMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("Z", zAxis.x(), height() - zAxis.y(), 1, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    // Axes
    if (!_axisVAO.isCreated())
    {
        _axisVAO.create();
        _axisVAO.bind();
    }

    // Vertex Buffer
    if (!_axisVBO.isCreated())
    {
        _axisVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisVBO.create();
    }
    _axisVBO.bind();
    _axisVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    std::vector<float> vertices = {
        0, 0, 0,
        _viewRange / size, 0, 0,
        0, 0, 0,
        0, _viewRange / size, 0,
        0, 0, 0,
        0, 0, _viewRange / size };
    _axisVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

    // Color Buffer
    if (!_axisCBO.isCreated())
    {
        _axisCBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisCBO.create();
    }
    _axisCBO.bind();
    _axisCBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    std::vector<float> colors = {
        1, 0, 0,
        1, 0, 0,
        0, 1, 0,
        0, 1, 0,
        0, 0, 1,
        0, 0, 1 };
    _axisCBO.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));

    _axisShader->bind();

    _axisVBO.bind();
    _axisShader->enableAttributeArray("vertexPosition");
    _axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    _axisCBO.bind();
    _axisShader->enableAttributeArray("vertexColor");
    _axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

    _axisShader->setUniformValue("modelViewMatrix", _modelViewMatrix);
    _axisShader->setUniformValue("projectionMatrix", _projectionMatrix);

    _axisVAO.bind();
    glLineWidth(2.5);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1);

    _axisVAO.release();
    _axisShader->release();
}

void GLWidget::drawCornerAxis()
{
    glViewport(width() - width() / 10, height() - height() / 10, width() / 10, height() / 10);
    QMatrix4x4 mat = _modelViewMatrix;
    mat.setColumn(3, QVector4D(0, 0, 0, 1));
    mat.setRow(3, QVector4D(0, 0, 0, 1));

    float size = 3.5;
    // Labels
    QVector3D xAxis(_viewRange / size, 0, 0);
    xAxis = xAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("X", xAxis.x(), height() - xAxis.y(), 7, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    QVector3D yAxis(0, _viewRange / size, 0);
    yAxis = yAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("Y", yAxis.x(), height() - yAxis.y(), 7, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    QVector3D zAxis(0, 0, _viewRange / size);
    zAxis = zAxis.project(mat, _projectionMatrix, QRect(0, 0, width(), height()));
    _axisTextRenderer->RenderText("Z", zAxis.x(), height() - zAxis.y(), 7, glm::vec3(1.0f, 1.0f, 0.0f), TextRenderer::VAlignment::VBOTTOM);

    // Axes
    if (!_axisVAO.isCreated())
    {
        _axisVAO.create();
        _axisVAO.bind();
    }

    // Vertex Buffer
    if (!_axisVBO.isCreated())
    {
        _axisVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisVBO.create();
    }
    _axisVBO.bind();
    _axisVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    std::vector<float> vertices = {
        0, 0, 0,
        _viewRange / size, 0, 0,
        0, 0, 0,
        0, _viewRange / size, 0,
        0, 0, 0,
        0, 0, _viewRange / size };
    _axisVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

    // Color Buffer
    if (!_axisCBO.isCreated())
    {
        _axisCBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisCBO.create();
    }
    _axisCBO.bind();
    _axisCBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    std::vector<float> colors = {
        1, 1, 1,
        1, 1, 1,
        1, 1, 1,
        1, 1, 1,
        1, 1, 1,
        1, 1, 1 };
    _axisCBO.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));

    _axisShader->bind();

    _axisVBO.bind();
    _axisShader->enableAttributeArray("vertexPosition");
    _axisShader->setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 3);

    _axisCBO.bind();
    _axisShader->enableAttributeArray("vertexColor");
    _axisShader->setAttributeBuffer("vertexColor", GL_FLOAT, 0, 3);

    _axisShader->setUniformValue("modelViewMatrix", mat);
    _axisShader->setUniformValue("projectionMatrix", _projectionMatrix);

    _axisVAO.bind();
    glLineWidth(2.0);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1);

    _axisVAO.release();
    _axisShader->release();

    glViewport(0, 0, width(), height());
}

void GLWidget::render()
{
    //renderToShadowBuffer();
    glEnable(GL_DEPTH_TEST);

    _viewMatrix.setToIdentity();
    _viewMatrix = _camera->getViewMatrix();

    // model transformations
    /*_modelMatrix.translate(QVector3D(_xTran, _yTran, _zTran));
    _modelMatrix.rotate(_xRot, 1, 0, 0);
    _modelMatrix.rotate(_yRot, 0, 1, 0);
    _modelMatrix.rotate(_zRot, 0, 0, 1);
    _modelMatrix.scale(_xScale, _yScale, _zScale);*/

    _modelViewMatrix = _viewMatrix * _modelMatrix;

    _fgShader->bind();
    _fgShader->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
    _fgShader->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
    _fgShader->setUniformValue("lightSource.specular", _specularLight.toVector3D());
    _fgShader->setUniformValue("lightSource.position", _lightPosition);
    _fgShader->setUniformValue("lightModel.ambient", QVector3D(0.2f, 0.2f, 0.2f));
    _fgShader->setUniformValue("modelViewMatrix", _modelViewMatrix);
    _fgShader->setUniformValue("normalMatrix", _modelViewMatrix.normalMatrix());
    _fgShader->setUniformValue("projectionMatrix", _projectionMatrix);
    _fgShader->setUniformValue("viewportMatrix", _viewportMatrix);
    _fgShader->setUniformValue("Line.Width", 0.75f);
    _fgShader->setUniformValue("Line.Color", QVector4D(0.05f, 0.0f, 0.05f, 1.0f));
    _fgShader->setUniformValue("displayMode", static_cast<int>(_displayMode));
    _fgShader->setUniformValue("envMapEnabled", _envMapEnabled);
    _fgShader->setUniformValue("shadowsEnabled", _shadowsEnabled);
    _fgShader->setUniformValue("reflectionMapEnabled", false);
    _fgShader->setUniformValue("cameraPos", _camera->getPosition());
    _fgShader->setUniformValue("modelMatrix", _modelMatrix);
    _fgShader->setUniformValue("viewMatrix", _viewMatrix);
    _fgShader->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);

    glPolygonMode(GL_FRONT_AND_BACK, _displayMode == DisplayMode::WIREFRAME ? GL_LINE : GL_FILL);
    glLineWidth(_displayMode == DisplayMode::WIREFRAME ? 1.25 : 1.0);

    // Clipping Planes
    if (_clipXEnabled)
        glEnable(GL_CLIP_DISTANCE0);
    if (_clipYEnabled)
        glEnable(GL_CLIP_DISTANCE1);
    if (_clipZEnabled)
        glEnable(GL_CLIP_DISTANCE2);

    if (!(_clipDX == 0 && _clipDY == 0 && _clipDZ == 0))
    {
        glEnable(GL_CLIP_DISTANCE3);
    }

    // Mesh
    drawMesh();
    // Vertex Normal
    drawVertexNormals();
    // Face Normal
    drawFaceNormals();

    glDisable(GL_CLIP_DISTANCE0);
    glDisable(GL_CLIP_DISTANCE1);
    glDisable(GL_CLIP_DISTANCE2);
    glDisable(GL_CLIP_DISTANCE3);

    // Shadow mapped floor
    if(_displayMode == DisplayMode::REALSHADED && _floorDisplayed)
    {
        drawFloor();
    }

    if(_skyBoxEnabled)
        drawSkyBox();

    if (_bShowAxis)
        drawAxis();

    _fgShader->release();
}

void GLWidget::renderToShadowBuffer()
{
    // save current viewport
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT,viewport);
    /// Shadow Mapping
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowWidth, _shadowHeight);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _shadowMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    // 1. render depth of scene to texture (from light's perspective)
    // --------------------------------------------------------------
    QMatrix4x4 lightProjection, lightView;
    float radius = _boundingSphere.getRadius();
    float near_plane = -radius*2, far_plane = radius*2;
    lightProjection.ortho(-radius*2, radius*2, -radius*2, radius*2, near_plane, far_plane);
    lightView.lookAt(_lightPosition, QVector3D(0,0,0), QVector3D(0.0, 1.0, 0.0));
    _lightSpaceMatrix = lightProjection * lightView;
    // render scene from light's point of view
    _shadowMappingShader->bind();
    _shadowMappingShader->setUniformValue("lightSpaceMatrix", _lightSpaceMatrix);
    _shadowMappingShader->setUniformValue("model", _modelMatrix);
    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            try
            {
                TriangleMesh* mesh = _meshStore.at(i);
                if (mesh)
                {
                    mesh->setProg(_shadowMappingShader);
                    mesh->render();
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Exception raised in GLWidget::renderToShadowBuffer\n" << ex.what() << std::endl;
            }
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    // End Shadow Mapping
    // restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void GLWidget::renderToReflectionMap()
{
    // save current viewport
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    /// Shadow Mapping
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowWidth * 4.0f, _shadowHeight * 4.0f);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _reflectionMapFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // Accept fragment if it closer to the camera than the former one
    glDepthFunc(GL_LESS);
    // Cull triangles which normal is not towards the camera
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glDisable(GL_BLEND);
    // 1. render depth of scene to texture (from floor perspective)
    // --------------------------------------------------------------
    QMatrix4x4 floorProjection, floorView, floorSpaceMatrix;
    float radius = _boundingSphere.getRadius();
    float near_plane = -radius*2, far_plane = radius*2;
    floorProjection.ortho(-radius * 2.0f, radius * 2.0f, -radius * 2.0f, radius * 2.0f, near_plane, far_plane);
    QVector3D eye = QVector3D(_floorCenter.x(), _floorCenter.y(), -_floorSize);
    QVector3D center = _floorCenter;
    floorView.lookAt(eye, center, QVector3D(0.0f, -1.0f, 0.0f));
    QMatrix4x4 model;
    model.scale(0.8f, -0.8f, 0.8f);
    floorSpaceMatrix = floorProjection * floorView;
    // render scene from light's point of view
    _reflectionMappingShader->bind();
    _reflectionMappingShader->setUniformValue("lightSource.ambient", _ambientLight.toVector3D());
    _reflectionMappingShader->setUniformValue("lightSource.diffuse", _diffuseLight.toVector3D());
    _reflectionMappingShader->setUniformValue("lightSource.specular", _specularLight.toVector3D());
    _reflectionMappingShader->setUniformValue("lightSource.position", _lightPosition);
    _reflectionMappingShader->setUniformValue("lightModel.ambient", QVector3D(0.2f, 0.2f, 0.2f));

    _reflectionMappingShader->setUniformValue("displayMode", static_cast<int>(_displayMode));
    _reflectionMappingShader->setUniformValue("envMapEnabled", _envMapEnabled);
    _reflectionMappingShader->setUniformValue("cameraPos", eye);

    _reflectionMappingShader->setUniformValue("viewMatrix", floorView);
    _reflectionMappingShader->setUniformValue("normalMatrix", (model * floorView).normalMatrix());
    _reflectionMappingShader->setUniformValue("projectionMatrix", floorProjection);
    _reflectionMappingShader->setUniformValue("modelMatrix", model);
    _reflectionMappingShader->setUniformValue("reflectionSpaceMatrix", floorProjection * floorView);

    _reflectionMappingShader->setUniformValue("clipPlaneX", QVector4D((model * floorView) * (QVector3D(_clipXFlipped ? -1 : 1, 0, 0) + eye),
                                                                      (_clipXFlipped ? -1 : 1) * eye.x() + _clipXCoeff));
    _reflectionMappingShader->setUniformValue("clipPlaneY", QVector4D((model * floorView) * (QVector3D(0, _clipYFlipped ? -1 : 1, 0) + eye),
                                                                      (_clipYFlipped ? -1 : 1) * eye.y() + _clipYCoeff));
    _reflectionMappingShader->setUniformValue("clipPlaneZ", QVector4D((model * floorView) * (QVector3D(0, 0, _clipZFlipped ? -1 : 1) + eye),
                                                                      (_clipZFlipped ? -1 : 1) * eye.z() + _clipZCoeff));
    _reflectionMappingShader->setUniformValue("clipPlane", QVector4D((model * floorView) * (QVector3D(_clipDX, _clipDY, _clipDZ) + eye),
                                                                     eye.x() * _clipDX + eye.y() * _clipDY + eye.z() * _clipDZ));

    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            try
            {
                TriangleMesh* mesh = _meshStore.at(i);
                if (mesh)
                {
                    mesh->setProg(_reflectionMappingShader);
                    mesh->render();
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Exception raised in GLWidget::renderToReflectionMap\n" << ex.what() << std::endl;
            }
        }
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    // End Shadow Mapping
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_BLEND);
    // restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void GLWidget::renderToReflectionDepthMap()
{
    // save current viewport
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    /// Reflection Depth Mapping
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowWidth, _shadowHeight);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _reflectionMapDBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    // 1. render depth of scene to texture (from floor perspective)
    // --------------------------------------------------------------

    QMatrix4x4 floorProjection, floorView, floorSpaceMatrix;
    float radius = _boundingSphere.getRadius();
    float near_plane = -radius*2, far_plane = radius*2;
    floorProjection.ortho(-radius * 2.0f, radius * 2.0f, -radius * 2.0f, radius * 2.0f, near_plane, far_plane);
    QVector3D eye = QVector3D(_floorCenter.x(), _floorCenter.y(), -_floorSize);
    QVector3D center = _floorCenter;
    floorView.lookAt(eye, center, QVector3D(0.0f, -1.0f, 0.0f));
    QMatrix4x4 model;
    model.scale(0.8f, -0.8f, 0.8f);
    floorSpaceMatrix = floorProjection * floorView;
    // render scene from light's point of view
    _shadowMappingShader->bind();
    _shadowMappingShader->setUniformValue("lightSpaceMatrix", floorSpaceMatrix);
    _shadowMappingShader->setUniformValue("model", model);
    if (_meshStore.size() != 0)
    {
        for (int i : _displayedObjectsIds)
        {
            try
            {
                TriangleMesh* mesh = _meshStore.at(i);
                if (mesh)
                {
                    mesh->setProg(_shadowMappingShader);
                    mesh->render();
                }
            }
            catch (const std::exception& ex)
            {
                std::cout << "Exception raised in GLWidget::renderToReflectionDepthMap\n" << ex.what() << std::endl;
            }
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    // End Depth Mapping
    // restore viewport
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void GLWidget::gradientBackground(float top_r, float top_g, float top_b, float top_a,
                                  float bot_r, float bot_g, float bot_b, float bot_a)
{
    glViewport(0, 0, width(), height());
    if (!_bgVAO.isCreated())
    {
        _bgVAO.create();
    }

    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    _bgShader.bind();

    _bgShader.setUniformValue("top_color", QVector4D(top_r, top_g, top_b, top_a));
    _bgShader.setUniformValue("bot_color", QVector4D(bot_r, bot_g, bot_b, bot_a));

    _bgVAO.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_DEPTH_TEST);

    _bgVAO.release();
    _bgShader.release();
}

void GLWidget::splitScreen()
{
    if (!_bgSplitVAO.isCreated())
    {
        _bgSplitVAO.create();
        _bgSplitVAO.bind();
    }

    if (!_bgSplitVBO.isCreated())
    {
        _bgSplitVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _bgSplitVBO.create();
        _bgSplitVBO.bind();
        _bgSplitVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);

        static const std::vector<float> vertices = {
            -static_cast<float>(width()) / 2,
            0,
            static_cast<float>(width()) / 2,
            0,
            0,
            -static_cast<float>(height()) / 2,
            0,
            static_cast<float>(height()) / 2,
        };

        _bgSplitVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

        _bgSplitShader.bind();
        _bgSplitShader.enableAttributeArray("vertexPosition");
        _bgSplitShader.setAttributeBuffer("vertexPosition", GL_FLOAT, 0, 2);

        _bgSplitVBO.release();
    }

    glViewport(0, 0, width(), height());

    glDisable(GL_DEPTH_TEST);

    _bgSplitVAO.bind();
    glLineWidth(0.5);
    glDrawArrays(GL_LINES, 0, 4);
    glLineWidth(1);

    glEnable(GL_DEPTH_TEST);

    _bgSplitVAO.release();
    _bgSplitShader.release();
}

void GLWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() & Qt::LeftButton)
    {        
        _leftButtonPoint.setX(e->x());
        _leftButtonPoint.setY(e->y());

        if (!(e->modifiers() & Qt::ControlModifier) && !_bWindowZoomActive)
        {
            // Selection
            mouseSelect(QPoint(e->x(), e->y()));
        }

        if (!_rubberBand)
        {
            _rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
            _rubberBand->setStyle(QStyleFactory::create("Fusion"));
        }
        _rubberBand->setGeometry(QRect(_leftButtonPoint, QSize()));
        _rubberBand->show();
    }

    if (e->button() & Qt::RightButton)
    {
        _rightButtonPoint.setX(e->x());
        _rightButtonPoint.setY(e->y());
    }

    if (e->button() & Qt::MiddleButton)
    {
        _middleButtonPoint.setX(e->x());
        _middleButtonPoint.setY(e->y());
    }
}

void GLWidget::mouseReleaseEvent(QMouseEvent* e)
{
    setCursor(QCursor(Qt::ArrowCursor));
    if (e->button() & Qt::LeftButton)
    {
        _rubberBand->hide();
        if (_bWindowZoomActive)
        {
            endWindowZoom();
        }
    }

    if (e->button() & Qt::RightButton)
    {        
    }

    if (e->button() & Qt::MiddleButton)
    {
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent* e)
{
    _animateViewTimer->stop();

    QPoint downPoint(e->x(), e->y());
    if (e->buttons() == Qt::LeftButton)
    {
        if (e->modifiers() == Qt::NoModifier)
        {
            _rubberBand->setGeometry(QRect(_leftButtonPoint, e->pos()).normalized());
        }
        if (_bWindowZoomActive)
        {
            setCursor(QCursor(QPixmap(":/new/prefix1/res/window-zoom-cursor.png"), 12, 12));
        }
        else if (e->modifiers() & Qt::ControlModifier)
        {
            QPoint rotate = _leftButtonPoint - downPoint;

            _camera->rotateX(rotate.y() / 2.0);
            _camera->rotateY(rotate.x() / 2.0);
            _currentRotation = QQuaternion::fromRotationMatrix(_camera->getViewMatrix().toGenericMatrix<3, 3>());
            _leftButtonPoint = downPoint;
            setCursor(QCursor(QPixmap(":/new/prefix1/res/rotatecursor.png")));
        }
    }

    if (e->buttons() == Qt::RightButton && e->modifiers() & Qt::ControlModifier)
    {
        QVector3D Z(0, 0, 0); // instead of 0 for x and y we need worldPosition.x() and worldPosition.y() ....
        Z = Z.project(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
        QVector3D p1(downPoint.x(), height() - downPoint.y(), Z.z());
        QVector3D O = p1.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
        QVector3D p2(_rightButtonPoint.x(), height() - _rightButtonPoint.y(), Z.z());
        QVector3D P = p2.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
        QVector3D OP = P - O;
        _camera->move(OP.x(), OP.y(), OP.z());
        _currentTranslation = _camera->getPosition();

        _rightButtonPoint = downPoint;
        setCursor(QCursor(QPixmap(":/new/prefix1/res/pancursor.png")));
    }

    if (e->buttons() == Qt::MiddleButton && e->modifiers() & Qt::ControlModifier)
    {
        if (downPoint.x() > _middleButtonPoint.x() || downPoint.y() < _middleButtonPoint.y())
            _viewRange /= 1.05f;
        else
            _viewRange *= 1.05f;
        if (_viewRange < 0.05)
            _viewRange = 0.05f;
        if (_viewRange > 50000.0)
            _viewRange = 50000.0f;
        _currentViewRange = _viewRange;

        resizeGL(width(), height());

        _middleButtonPoint = downPoint;
        setCursor(QCursor(QPixmap(":/new/prefix1/res/zoomcursor.png")));
    }

    update();
}

void GLWidget::wheelEvent(QWheelEvent* e)
{
    QPoint numDegrees = e->angleDelta() / 8;
    QPoint numSteps = numDegrees / 15;
    float zoomStep = numSteps.y();
    float zoomFactor = abs(zoomStep) + 0.05;

    if (zoomStep < 0)
        _viewRange *= zoomFactor;
    else
        _viewRange /= zoomFactor;

    if (_viewRange < 0.05)
        _viewRange = 0.05f;
    if (_viewRange > 500000.0)
        _viewRange = 500000.0f;
    _currentViewRange = _viewRange;

    resizeGL(width(), height());

    update();
}

void GLWidget::animateViewChange()
{
    if (_viewMode == ViewMode::TOP)
    {
        setRotations(0.0f, 0.0f, 0.0f);
    }
    if (_viewMode == ViewMode::BOTTOM)
    {
        setRotations(0.0f, -180.0f, 0.0f);
    }
    if (_viewMode == ViewMode::LEFT)
    {
        setRotations(0.0f, -90.0f, 90.0f);
    }
    if (_viewMode == ViewMode::RIGHT)
    {
        setRotations(0.0f, -90.0f, -90.0f);
    }
    if (_viewMode == ViewMode::FRONT)
    {
        setRotations(0.0f, -90.0f, 0.0f);
    }
    if (_viewMode == ViewMode::BACK)
    {
        setRotations(0.0f, -90.0f, 180.0f);
    }
    if (_viewMode == ViewMode::ISOMETRIC)
    {
        setRotations(-45.0f, -54.7356f, 0.0f);
    }
    if (_viewMode == ViewMode::DIMETRIC)
    {
        setRotations(-14.1883f, -73.9639f, -0.148236f);
    }
    if (_viewMode == ViewMode::TRIMETRIC)
    {
        setRotations(-32.5829f, -61.4997f, -0.877613f);
    }

    resizeGL(width(), height());
}

void GLWidget::animateFitAll()
{
    setZoomAndPan(_viewBoundingSphereDia, -_currentTranslation + _boundingSphere.getCenter());
    resizeGL(width(), height());
}

void GLWidget::animateWindowZoom()
{
    setZoomAndPan(_currentViewRange / _rubberBandZoomRatio, _rubberBandPan);
    resizeGL(width(), height());
}

void GLWidget::animateCenterScreen()
{
    TriangleMesh* mesh = _meshStore.at(_centerScreenObjectId);
    if (mesh)
    {
        BoundingSphere sph = mesh->getBoundingSphere();
        setZoomAndPan(sph.getRadius() * 2, -_currentTranslation + sph.getCenter());
        resizeGL(width(), height());
    }
}

void GLWidget::convertClickToRay(const QPoint& pixel, QVector3D& orig, QVector3D& dir)
{
    QVector3D Z(0, 0, 0); // instead of 0 for x and y we need worldPosition.x() and worldPosition.y() ....
    Z = Z.project(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

    QVector3D p(pixel.x(), height() - pixel.y(), -1.0f);
    QVector3D P = p.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));

    orig = QVector3D(P.x(), P.y(), P.z());
    QVector3D viewDir = _camera->getViewDir();
    dir = viewDir;
}


int GLWidget::mouseSelect(const QPoint& pixel)
{
    int id = -1;

    if (!_displayedObjectsIds.size())
        return id;

    QVector3D rayPos, rayDir;
    QVector3D intPoint;
    convertClickToRay(pixel, rayPos, rayDir);

    for (int i : _displayedObjectsIds)
    {
        bool intersects = _meshStore.at(i)->intersectsWithRay(rayPos, rayDir, intPoint);
        if (intersects)
        {
            id = i;
            break;
        }
    }

    /*qDebug() << "RayPos :" << rayPos;
    qDebug() << "RayDir :" << rayDir;
    qDebug() << "Inter Point: " << intPoint;
    std::cout << "Selected id: " << id << std::endl;*/

    emit objectSelectionChanged(id);
    return id;
}

void GLWidget::setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir)
{
    _camera->setView(viewPos, viewDir, upDir, rightDir);
    emit viewSet();
}

void GLWidget::setRotations(float xRot, float yRot, float zRot)
{
    // Rotation
    QQuaternion targetRotation = QQuaternion::fromEulerAngles(yRot, zRot, xRot); //Pitch, Yaw, Roll
    QQuaternion curRot = QQuaternion::slerp(_currentRotation, targetRotation, _slerpStep += _slerpFrac);

    // Translation
    QVector3D curPos = _currentTranslation - (_slerpStep * _currentTranslation) + (_boundingSphere.getCenter() * _slerpStep);

    // Set camera vectors
    QMatrix4x4 rotMat = QMatrix4x4(curRot.toRotationMatrix());
    QVector3D viewDir = -rotMat.row(2).toVector3D();
    QVector3D upDir = rotMat.row(1).toVector3D();
    QVector3D rightDir = rotMat.row(0).toVector3D();
    _camera->setView(curPos, viewDir, upDir, rightDir);

    // Set zoom
    float scaleStep = (_currentViewRange - _viewBoundingSphereDia) * _slerpFrac;
    _viewRange -= scaleStep;

    if (qFuzzyCompare(_slerpStep, 1.0f))
    {
        // Set camera vectors
        QMatrix4x4 rotMat = QMatrix4x4(curRot.toRotationMatrix());
        QVector3D viewDir = -rotMat.row(2).toVector3D();
        QVector3D upDir = rotMat.row(1).toVector3D();
        QVector3D rightDir = rotMat.row(0).toVector3D();
        _camera->setView(curPos, viewDir, upDir, rightDir);

        // Set all defaults
        _currentRotation = QQuaternion::fromRotationMatrix(_camera->getViewMatrix().toGenericMatrix<3, 3>());
        _currentTranslation = _camera->getPosition();
        _currentViewRange = _viewRange;
        _viewMode = ViewMode::NONE;
        _slerpStep = 0.0f;

        //_animateViewTimer->stop();
        emit rotationsSet();
    }
}

void GLWidget::setZoomAndPan(float zoom, QVector3D pan)
{
    _slerpStep += _slerpFrac;

    // Translation
    QVector3D curPos = pan * _slerpFrac;
    _camera->move(curPos.x(), curPos.y(), curPos.z());

    // Set zoom
    float scaleStep = (_currentViewRange - zoom) * _slerpFrac;
    _viewRange -= scaleStep;

    if (qFuzzyCompare(_slerpStep, 1.0f))
    {
        // Set all defaults
        _currentTranslation = _camera->getPosition();
        _currentViewRange = _viewRange;
        _viewMode = ViewMode::NONE;
        _slerpStep = 0.0f;

        // position the camera for rotation center
        /*
        _viewMatrix = _camera->getViewMatrix();
        _projectionMatrix = _camera->getProjectionMatrix();
        QVector3D Z(0, 0, 0); // instead of 0 for x and y we need worldPosition.x() and worldPosition.y() ....
        Z = Z.project(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
        QPoint point = geometry().center();
        QVector3D p(point.x(), height() - point.y(), Z.z());
        QVector3D P = p.unproject(_viewMatrix * _modelMatrix, _projectionMatrix, QRect(0, 0, width(), height()));
        _camera->setPosition(P.x(), P.y(), P.z());
        */

        // Stop the animation
        emit zoomAndPanSet();
    }
}

void GLWidget::closeEvent(QCloseEvent* event)
{
    if (_sphericalHarmonicsEditor)
    {
        _sphericalHarmonicsEditor->hide();
        _sphericalHarmonicsEditor->close();
    }

    if (_graysKleinEditor)
    {
        _graysKleinEditor->hide();
        _graysKleinEditor->close();
    }

    if (_superToroidEditor)
    {
        _superToroidEditor->hide();
        _superToroidEditor->close();
    }

    if (_superEllipsoidEditor)
    {
        _superEllipsoidEditor->hide();
        _superEllipsoidEditor->close();
    }

    if (_springEditor)
    {
        _springEditor->hide();
        _springEditor->close();
    }
    event->accept();
}

QColor GLWidget::getBgBotColor() const
{
    return _bgBotColor;
}

void GLWidget::setBgBotColor(const QColor &bgBotColor)
{
    _bgBotColor = bgBotColor;
    update();
}

QColor GLWidget::getBgTopColor() const
{
    return _bgTopColor;
}

void GLWidget::setBgTopColor(const QColor &bgTopColor)
{
    _bgTopColor = bgTopColor;
    update();
}

BoundingSphere GLWidget::getBoundingSphere() const
{
    return _boundingSphere;
}

std::vector<int> GLWidget::getDisplayedObjectsIds() const
{
    return _displayedObjectsIds;
}

void GLWidget::setShowFaceNormals(bool showFaceNormals)
{
    _showFaceNormals = showFaceNormals;
}

bool GLWidget::isFaceNormalsShown() const
{
    return _showFaceNormals;
}

bool GLWidget::isVertexNormalsShown() const
{
    return _showVertexNormals;
}

void GLWidget::setShowVertexNormals(bool showVertexNormals)
{
    _showVertexNormals = showVertexNormals;
}

bool GLWidget::isShaded() const
{
    return _displayMode == DisplayMode::SHADED;
}

void GLWidget::setDisplayMode(DisplayMode mode)
{
    _displayMode = mode;
}

float GLWidget::getZScale() const
{
    return _zScale;
}

void GLWidget::setZScale(const float& zScale)
{
    _zScale = zScale;
}

float GLWidget::getYScale() const
{
    return _yScale;
}

void GLWidget::setYScale(const float& yScale)
{
    _yScale = yScale;
}

float GLWidget::getXScale() const
{
    return _xScale;
}

void GLWidget::setXScale(const float& xScale)
{
    _xScale = xScale;
}

float GLWidget::getZRot() const
{
    return _zRot;
}

void GLWidget::setZRot(const float& zRot)
{
    _zRot = zRot;
}

float GLWidget::getYRot() const
{
    return _yRot;
}

void GLWidget::setYRot(const float& yRot)
{
    _yRot = yRot;
}

float GLWidget::getXRot() const
{
    return _xRot;
}

void GLWidget::setXRot(const float& xRot)
{
    _xRot = xRot;
}

float GLWidget::getZTran() const
{
    return _zTran;
}

void GLWidget::setZTran(const float& zTran)
{
    _zTran = zTran;
}

float GLWidget::getYTran() const
{
    return _yTran;
}

void GLWidget::setYTran(const float& yTran)
{
    _yTran = yTran;
}

float GLWidget::getXTran() const
{
    return _xTran;
}

void GLWidget::setXTran(const float& xTran)
{
    _xTran = xTran;
}

float GLWidget::lowestModelZ()
{
    float lowestZ = std::numeric_limits<float>::max();
    for (int i : _displayedObjectsIds)
    {
        try
        {
            TriangleMesh* mesh = _meshStore.at(i);
            float z = mesh->getLowestZValue();
            if (z < lowestZ)
                lowestZ = z;
        }
        catch (const std::exception& ex)
        {
            std::cout << "Exception raised in GLWidget::lowestModelZ\n" << ex.what() << std::endl;
            lowestZ = -_boundingSphere.getRadius();
        }
    }
    return lowestZ;
}

void GLWidget::renderQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void GLWidget::showContextMenu(const QPoint &pos)
{
    if(QApplication::keyboardModifiers() != Qt::ControlModifier)
    {
    // Create menu and insert some actions
    QMenu myMenu;
    QListWidget* listWidgetModel = _viewer->getListModel();
    if (listWidgetModel->selectedItems().count() != 0 && _displayedObjectsIds.size() != 0)
    {
        QList<QListWidgetItem*> selectedItems = listWidgetModel->selectedItems();
        if(selectedItems.count() <= 1 && selectedItems.at(0)->checkState() == Qt::Checked)
            myMenu.addAction("Center Object List", this, SLOT(centerDisplayList()));

        myMenu.addAction("Object Properties", this, SLOT(showPropertiesPage()));
        myMenu.addAction("Transformations", this, SLOT(showTransformationsPage()));
        myMenu.addAction("Hide", this, SLOT(hideSelectedItem()));
        myMenu.addAction("Delete", this, SLOT(deleteSelectedItem()));
    }
    else
    {
        myMenu.addAction("Bagkground Color", this, SLOT(setBackgroundColor()));
    }
    // Show context menu at handling position
    myMenu.exec(mapToGlobal(pos));
    }
}

void GLWidget::centerDisplayList()
{
    QListWidget* listWidgetModel = _viewer->getListModel();
    if(listWidgetModel)
    {
        _viewer->showObjectDisplayList();
        listWidgetModel->scrollToItem(listWidgetModel->selectedItems().at(0));
    }
}

void GLWidget::deleteSelectedItem()
{
    _viewer->deleteSelectedItems();
}

void GLWidget::hideSelectedItem()
{
    _viewer->hideSelectedItems();
}

void GLWidget::showPropertiesPage()
{
    _viewer->showObjectsPropertiesPage();
}

void GLWidget::showTransformationsPage()
{
    _viewer->showTransformationsPage();
}

#include "BackgroundColor.h"
void GLWidget::setBackgroundColor()
{
    BackgroundColor bgCol(this);
    bgCol.exec();
}
