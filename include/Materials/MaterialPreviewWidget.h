#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QTimer>
#include "Material.h"
#include "ShaderProgram.h"

#include <QDateTime>

class QMouseEvent;
class QWheelEvent;
class ViewportWidget;

enum class PreviewShape { Sphere, Cube, Cylinder, Plane, Teapot };
enum class EnvMode { ViewerIBL, Studio, Outdoor, Office };
enum class TexViewMode {
    All = 0, Albedo, Metalness, Roughness, Normal, AO, Height, Opacity, Emissive,
    ClearcoatColor, ClearcoatRoughness, ClearcoatNormal,
    SheenColor, SheenRoughness,
    Transmission, IOR, Thickness,
    SpecularFactor, SpecularColor,
    Anisotropy,
    Iridescence, IridescenceThickness,
    DiffuseTransmission, DiffuseTransmissionColor
};
enum class PreviewProfile { TextureAuthoring, MaterialShowcase };

struct GpuTexCache
{
    GLuint id = 0;
    QString lastPath;
    QDateTime lastModified;   // local time or UTC; stay consistent
    qint64 lastSize = -1;

    // Track sampler parameters
    GLint lastWrapS = GL_REPEAT;
    GLint lastWrapT = GL_REPEAT;
    GLint lastMinFilter = GL_LINEAR_MIPMAP_LINEAR;
    GLint lastMagFilter = GL_LINEAR;
};

class MaterialPreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT

    struct MeshGL
    {
        GLuint vao = 0, vbo = 0, ebo = 0;
        int indexCount = 0;
    };

public:
    explicit MaterialPreviewWidget(QWidget *parent = nullptr);
    ~MaterialPreviewWidget();

    void setMaterial(const Material &mat);

    void setPreviewShape(PreviewShape s);
	void setEnvironment(EnvMode env) { _currentEnv = env; update(); }
    void setExposureEV(float ev);
	void setTextureViewMode(TexViewMode mode) { _texViewMode = mode; update(); }
	void setViewportWidget(ViewportWidget* viewportWidget) { _viewportWidget = viewportWidget; }
    void setPreviewProfile(PreviewProfile profile) { _profile = profile; update(); }

	PreviewShape currentShape() const { return _currentShape; }

    void setPreviewRotation(float pitchDeg, float yawDeg);

    void updateTextureSamplers(Material::TextureType type,
        GLint wrapS = GL_REPEAT,
		GLint wrapT = GL_REPEAT,
        GLint minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLint magFilter = GL_LINEAR,
        float aniso = 0.0f);

    void applySamplerParametersToTexture(GLuint textureId, const Material::Texture& tex);

    Material _currentMaterial = Material::METAL_ALUMINUM();
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void applyEnvPreset(EnvMode preset, PreviewProfile profile);

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e);
    void wheelEvent(QWheelEvent* e);

    void showEvent(QShowEvent* e) override;

private:
    void initSphereMesh();    
    void initCubeMesh();
    void initCylinderMesh();
    void initPlaneMesh();     
    void initTeapotMesh();    

    void destroyMesh(MeshGL& m);

    void startOverlayHint(int showMs = 3000, int fadeMs = 1500);

    void startSpin(float velPitchDegPerSec, float velYawDegPerSec);
    void stopSpin();

    bool shouldReload(const QString& path, const GpuTexCache& cache,
        GLint wrapS, GLint wrapT, GLint minFilter, GLint magFilter);

    void syncTextureFromMaterial(
        GpuTexCache& cache,
        const QString& path,
        int texUnit,
        const char* uniformSamplerName,
        const char* uniformUseName,
        bool srgb);
	void syncAllTexturesFromMaterial();

    void clearTextureCache();

private:
    class ViewportWidget* _viewportWidget = nullptr;  // For accessing environment settings
    std::unique_ptr<ShaderProgram> _shader;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;

    int indexCount = 0;

    QMatrix4x4 proj;
    QMatrix4x4 view;

    
    
    GpuTexCache _albedoTex;
	GpuTexCache _metallicTex;
	GpuTexCache _roughnessTex;
	GpuTexCache _normalTex;
	GpuTexCache _aoTex;
	GpuTexCache _opacityTex;
	GpuTexCache _emissiveTex;
	GpuTexCache _heightTex;
	GpuTexCache _sheenColorTex;
	GpuTexCache _sheenRoughnessTex;
	GpuTexCache _clearcoatColorTex;
	GpuTexCache _clearcoatRoughnessTex;
	GpuTexCache _clearcoatNormalTex;
	GpuTexCache _transmissionTex;
	GpuTexCache _iorTex;
	GpuTexCache _specularFactorTex;
	GpuTexCache _specularColorTex;
	GpuTexCache _anisotropyTex;
	GpuTexCache _iridescenceTex;
	GpuTexCache _iridescenceThicknessTex;
	GpuTexCache _diffuseTransmissionTex;
	GpuTexCache _diffuseTransmissionColorTex;
	GpuTexCache _thicknessTex;
	GpuTexCache _diffuseTex;
	GpuTexCache _specularGlossinessTex;

    MeshGL _sphere{};
    MeshGL _cube{};
    MeshGL _cylinder{};
    MeshGL _plane{};
    MeshGL _teapot{};

    PreviewShape _currentShape = PreviewShape::Sphere;
        
    float _rotX = 25.0f;   // pitch  (about X)
    float _rotY = 20.0f;   // yaw    (about Y)
    float _rotSpeed = 0.3f; // degrees per pixel

    bool _dragging = false;
    bool _rightDragging = false;
    QPoint _dragDelta;

    QPoint _lastPos;

    float _zoom = 1.0f;          // scale factor
    float _minZoom = 0.25f;       // -25%
    float _maxZoom = 2.5f;       // +250%
    float _zoomSpeed = 0.002f;    // zoom per pixel dragged vertically

    bool _overlayActive = false;
    QElapsedTimer _overlayTimer;
    QTimer _overlayUpdateTimer;
    int _overlayShowMs = 3000;     // full opacity duration
    int _overlayFadeMs = 1500;     // fade duration

    // Spin (inertia) state
    QTimer _spinTimer;
    QElapsedTimer _spinClock;   // for dt
    float _spinVelX = 0.0f;     // deg/sec (pitch velocity)
    float _spinVelY = 0.0f;     // deg/sec (yaw velocity)
    float _spinDamping = 2.0f;  // per-second damping (>0). Bigger = stops quicker
    float _spinMinSpeed = 5.0f; // deg/sec threshold to stop

	EnvMode _currentEnv = EnvMode::ViewerIBL;
	EnvMode _lastEnvMode = EnvMode::Studio;  // Track for regenerating synthetic environment maps
	float _exposureEV = 0.0f;
	TexViewMode _texViewMode = TexViewMode::All;
	PreviewProfile _profile = PreviewProfile::MaterialShowcase;

    GLuint _textOverlayTexture = 0;
    std::unique_ptr<QOpenGLShaderProgram> _overlayShader;
    GLuint _overlayVAO = 0;
    GLuint _overlayVBO = 0;
    bool _textTextureNeedsUpdate = true;

    void initializeOverlayShader();
    void createTextTexture();
    void renderTextOverlay(float alpha);

	// Environment mapping for IBL
	GLuint _envCubemap = 0;
	GLuint _irradianceMap = 0;

	void generateDefaultEnvironmentCubemap();
	void generateIrradianceMap();
	void cleanupEnvironmentMaps();
};
