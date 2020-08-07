#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QtGui>
#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>
#include <QImage>
#include <QColor>

#include <math.h>
#include "GLCamera.h"
#include "BoundingSphere.h"

/* Custom OpenGL Viewer Widget */

class TextRenderer;
class TriangleMesh;
class SphericalHarmonicsEditor;
class SuperToroidEditor;
class SuperEllipsoidEditor;
class SpringEditor;
class ClippingPlanesEditor;
class GraysKleinEditor;
class STLMesh;

struct GLMaterialProps
{
    QVector4D ambientMaterial;
    QVector4D diffuseMaterial;
    QVector4D specularMaterial;
    QVector4D specularReflectivity;
    QVector4D emmissiveMaterial;
    GLfloat   shininess;
    GLfloat   opacity;

    GLboolean bHasTexture;
};

enum class ViewMode { TOP, BOTTOM, LEFT, RIGHT, FRONT, BACK, ISOMETRIC, DIMETRIC, TRIMETRIC, NONE };
enum class ViewProjection { ORTHOGRAPHIC, PERSPECTIVE };
enum class DisplayMode { SHADED, WIREFRAME, WIRESHADED };

class GLWidget : public QOpenGLWidget, QOpenGLFunctions_4_5_Core
{
    friend class ClippingPlanesEditor;
    Q_OBJECT
public:
    GLWidget(QWidget *parent = 0, const char *name = 0);
    ~GLWidget();
    void updateView();
    void setTexture(QImage img);

    void resizeView(int w, int h) { resizeGL(w, h); }
    void setViewMode(ViewMode mode);
    void setProjection(ViewProjection proj);

    void setMultiView(bool active) { _bMultiView = active; }

    void fitAll();

    void beginWindowZoom();
    void endWindowZoom();

    void setDisplayList(const std::vector<int>& ids);
    void updateBoundingSphere();
    int getModelNum() const
    {
        return _modelNum;
    }

    void showClippingPlaneEditor(bool show);
    void showAxis(bool show);

    std::vector<TriangleMesh*> getMeshStore() const { return _meshStore; }

    void addToDisplay(TriangleMesh*);
    void removeFromDisplay(int index);
    void centerScreen(int index);
    void select(int id);
    void deselect(int id);

    TriangleMesh* loadSTLMesh(QString fileName);
    TriangleMesh* loadOBJMesh(QString fileName);

    void setMaterialProps(const std::vector<int>& ids, const GLMaterialProps& mat);
    void setTransformation(const std::vector<int>& ids, const QMatrix4x4& mat);
    void setTexture(const std::vector<int>& ids, const QImage& texImage);

public:
    GLfloat getXTran() const;
    void setXTran(const GLfloat &xTran);

    GLfloat getYTran() const;
    void setYTran(const GLfloat &yTran);

    GLfloat getZTran() const;
    void setZTran(const GLfloat &zTran);

    GLfloat getXRot() const;
    void setXRot(const GLfloat &xRot);

    GLfloat getYRot() const;
    void setYRot(const GLfloat &yRot);

    GLfloat getZRot() const;
    void setZRot(const GLfloat &zRot);

    GLfloat getXScale() const;
    void setXScale(const GLfloat &xScale);

    GLfloat getYScale() const;
    void setYScale(const GLfloat &yScale);

    GLfloat getZScale() const;
    void setZScale(const GLfloat &zScale);

    void drawAxis();
    void drawCornerAxis();
    
    QVector4D getAmbientLight() const;
    void setAmbientLight(const QVector4D &ambientLight);

    QVector4D getDiffuseLight() const;
    void setDiffuseLight(const QVector4D &diffuseLight);

    QVector4D getSpecularLight() const;
    void setSpecularLight(const QVector4D &specularLight);

    QVector3D getLightPosition() const;
    void setLightPosition(const QVector3D &lightPosition);

    bool isShaded() const;
    void setDisplayMode(DisplayMode mode);

    bool isVertexNormalsShown() const;
    void setShowVertexNormals(bool showVertexNormals);

    bool isFaceNormalsShown() const;
    void setShowFaceNormals(bool showFaceNormals);

    void drawVertexNormals();
    void drawFaceNormals();
    
    void drawMesh();

    std::vector<int> getDisplayedObjectsIds() const;

signals:
    void windowZoomEnded();
    void rotationsSet();
    void zoomAndPanSet();
    void viewSet();
    void displayListSet();

public slots:
    void animateViewChange();
    void animateFitAll();
    void animateWindowZoom();    
    void animateCenterScreen();

protected:
    void initializeGL();
    void resizeGL(int width, int height);
    void paintGL();

    void render();

    void mousePressEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *);
    void wheelEvent(QWheelEvent *);
    void closeEvent(QCloseEvent *event);

private:
    DisplayMode _displayMode;
    bool _bWindowZoomActive;
    int _modelNum;
    QImage _texImage, _texBuffer;
    TextRenderer* _textRenderer;
    TextRenderer* _axisTextRenderer;
    QString _modelName;

    QVector3D _currentTranslation;
    QQuaternion _currentRotation;
    GLfloat _slerpStep;
    GLfloat _slerpFrac;

    GLfloat _currentViewRange;
    GLfloat _scaleFrac;

    GLfloat _viewRange;
    float _viewBoundingSphereDia;
    GLfloat _FOV;

    bool _bLeftButtonDown;
    QPoint _leftButtonPoint;

    bool _bRightButtonDown;
    QPoint _rightButtonPoint;

    bool _bMiddleButtonDown;
    QPoint _middleButtonPoint;

    QRubberBand* _rubberBand;
    QVector3D _rubberBandPan;
    GLfloat _rubberBandZoomRatio;

    bool _bMultiView;

    bool _bShowAxis;

    GLfloat _clipXCoeff;
    GLfloat _clipYCoeff;
    GLfloat _clipZCoeff;

    GLfloat _clipDX;
    GLfloat _clipDY;
    GLfloat _clipDZ;

    bool _clipXEnabled;
    bool _clipYEnabled;
    bool _clipZEnabled;

    bool _clipXFlipped;
    bool _clipYFlipped;
    bool _clipZFlipped;

    bool _showVertexNormals;
    bool _showFaceNormals;

    GLfloat _xTran;
    GLfloat _yTran;
    GLfloat _zTran;

    GLfloat _xRot;
    GLfloat _yRot;
    GLfloat _zRot;

    GLfloat _xScale;
    GLfloat _yScale;
    GLfloat _zScale;

    QVector4D _ambientLight;
    QVector4D _diffuseLight;
    QVector4D _specularLight;

    QVector3D _lightPosition;

    QMatrix4x4 _projectionMatrix, _viewMatrix, _modelMatrix;
    QMatrix4x4 _modelViewMatrix;
    QMatrix4x4 _viewportMatrix;

    QOpenGLShaderProgram*     _fgShader;
    QOpenGLShaderProgram*     _axisShader;
    QOpenGLShaderProgram*     _vertexNormalShader;
    QOpenGLShaderProgram*     _faceNormalShader;

    QOpenGLShaderProgram     _textShader;

    GLuint                   _texture;

    QOpenGLShaderProgram     _bgShader;
    QOpenGLVertexArrayObject _bgVAO;

    QOpenGLShaderProgram     _bgSplitShader;
    QOpenGLVertexArrayObject _bgSplitVAO;
    QOpenGLBuffer _bgSplitVBO;

    QOpenGLVertexArrayObject _axisVAO;
    QOpenGLBuffer _axisVBO;
    QOpenGLBuffer _axisCBO;

    std::vector<TriangleMesh*> _meshStore;
    std::vector<int> _displayedObjectsIds;
    int _centerScreenObjectId;


    QVBoxLayout* _editorLayout;
    QFormLayout* _lowerLayout;
    QFormLayout* _upperLayout;

    SphericalHarmonicsEditor* _sphericalHarmonicsEditor;
    SuperToroidEditor* _superToroidEditor;
    SuperEllipsoidEditor* _superEllipsoidEditor;
    SpringEditor* _springEditor;
    GraysKleinEditor*		_graysKleinEditor;
    ClippingPlanesEditor*			_clippingPlanesEditor;

    ViewMode _viewMode;
    ViewProjection _projection;

    GLCamera* _camera;

    QTimer* _animateViewTimer;
    QTimer* _animateFitAllTimer;
    QTimer* _animateWindowZoomTimer;
    QTimer* _animateCenterScreenTimer;

    BoundingSphere _boundingSphere;

private:

    void createShaderPrograms();
    void createGeometry();

    void setRotations(GLfloat xRot, GLfloat yRot, GLfloat zRot);
    void setZoomAndPan(GLfloat zoom, QVector3D pan);
    void setView(QVector3D viewPos, QVector3D viewDir, QVector3D upDir, QVector3D rightDir);

    void gradientBackground(float top_r, float top_g, float top_b, float top_a,
                            float bot_r, float bot_g, float bot_b, float bot_a);

    void splitScreen();
};

#endif
