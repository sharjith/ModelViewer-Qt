#include "SceneRenderController.h"
#include "PathUtils.h"

#include "stb_image.h"
#include "HdrImageLoader.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMatrix3x3>
#include <QVector2D>
#include <cmath>
#include <algorithm>
#include <iostream>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool SceneRenderController::initialize()
{
    return initializeOpenGLFunctions();
}

void SceneRenderController::releaseGpuResources()
{
    const bool shareContexts = IGpuContextResource::contextsAreShared();

    if (!shareContexts)
    {
    // Textures — IBL, environment, LUT, capping, debug/white placeholders.
    // Every handle is zeroed immediately after deleting it (previously only
    // _viewCubeLabelTextures was) - several recreation guards elsewhere
    // (e.g. generateIBL()'s `if (_charlieLUTTexture == 0)`) rely on seeing
    // zero here to know they need to reload, and a stale non-zero handle
    // silently skipped reloading on the second+ recreation before this fix.
    glDeleteTextures(1, &_environmentMap); _environmentMap = 0;
    glDeleteTextures(1, &_shadowMap); _shadowMap = 0;
    glDeleteTextures(1, &_irradianceMap); _irradianceMap = 0;
    glDeleteTextures(1, &_prefilterMap); _prefilterMap = 0;
    glDeleteTextures(1, &_sheenPrefilterMap); _sheenPrefilterMap = 0;
    glDeleteTextures(1, &_studioEnvironmentMap); _studioEnvironmentMap = 0;
    glDeleteTextures(1, &_studioIrradianceMap); _studioIrradianceMap = 0;
    glDeleteTextures(1, &_studioPrefilterMap); _studioPrefilterMap = 0;
    glDeleteTextures(1, &_studioSheenPrefilterMap); _studioSheenPrefilterMap = 0;
    glDeleteTextures(1, &_outdoorEnvironmentMap); _outdoorEnvironmentMap = 0;
    glDeleteTextures(1, &_outdoorIrradianceMap); _outdoorIrradianceMap = 0;
    glDeleteTextures(1, &_outdoorPrefilterMap); _outdoorPrefilterMap = 0;
    glDeleteTextures(1, &_outdoorSheenPrefilterMap); _outdoorSheenPrefilterMap = 0;
    glDeleteTextures(1, &_officeEnvironmentMap); _officeEnvironmentMap = 0;
    glDeleteTextures(1, &_officeIrradianceMap); _officeIrradianceMap = 0;
    glDeleteTextures(1, &_officePrefilterMap); _officePrefilterMap = 0;
    glDeleteTextures(1, &_officeSheenPrefilterMap); _officeSheenPrefilterMap = 0;
    glDeleteTextures(1, &_brdfLUTTexture); _brdfLUTTexture = 0;
    glDeleteTextures(1, &_charlieLUTTexture); _charlieLUTTexture = 0;
    glDeleteTextures(1, &_sheenELUTTexture); _sheenELUTTexture = 0;
    glDeleteTextures(1, &_whiteTexture); _whiteTexture = 0;
    glDeleteTextures(1, &_debugNeutralTex); _debugNeutralTex = 0;
    glDeleteTextures(1, &_debugNormalTex); _debugNormalTex = 0;
    glDeleteTextures(1, &_debugBlackTex); _debugBlackTex = 0;
    }

    // Deliberately still dropped even in the shared-context path: this is
    // cheap and the current call sites rebuild it unconditionally.
    glDeleteTextures(1, &_cappingTexture); _cappingTexture = 0;
    for (GLuint& labelTexture : _viewCubeLabelTextures)
    {
        if (labelTexture != 0)
        {
            glDeleteTextures(1, &labelTexture);
            labelTexture = 0;
        }
    }

    // Framebuffers and renderbuffers
    if (_skyboxFBO != 0)
    {
        glDeleteFramebuffers(1, &_skyboxFBO);
        _skyboxFBO = 0;
    }
    if (_shadowMapFBO != 0)
    {
        glDeleteFramebuffers(1, &_shadowMapFBO);
        _shadowMapFBO = 0;
    }
    glDeleteRenderbuffers(1, &_skyboxDepthBuffer);
    _skyboxDepthBuffer = 0;

    // Qt-managed geometry wrappers
    _axisVBO.destroy();
    _axisVAO.destroy();
    _bgSplitVBO.destroy();
    _bgSplitVAO.destroy();
    _bgVAO.destroy();

    // Fullscreen triangle
    if (_fsTriVAO != 0)
    {
        glDeleteBuffers(1, &_fsTriVBO);
        glDeleteVertexArrays(1, &_fsTriVAO);
        _fsTriVAO = 0;
        _fsTriVBO = 0;
    }
    _fsTriInitialized = false;

    // Quad
    if (_quadVAO != 0)
    {
        glDeleteBuffers(1, &_quadVBO);
        glDeleteVertexArrays(1, &_quadVAO);
        _quadVAO = 0;
        _quadVBO = 0;
    }

    // Conversion cube
    if (_conversionCubeVAO != 0)
    {
        glDeleteBuffers(1, &_conversionCubeVBO);
        glDeleteVertexArrays(1, &_conversionCubeVAO);
        _conversionCubeVAO = 0;
        _conversionCubeVBO = 0;
    }

    // ViewCube label geometry
    if (_viewCubeLabelVAO != 0)
    {
        glDeleteBuffers(1, &_viewCubeLabelVBO);
        glDeleteVertexArrays(1, &_viewCubeLabelVAO);
        _viewCubeLabelVAO = 0;
        _viewCubeLabelVBO = 0;
    }

    // Debug overlay
    if (_debugOverlayBoxVAO != 0)
    {
        glDeleteBuffers(1, &_debugOverlayBoxVBO);
        glDeleteVertexArrays(1, &_debugOverlayBoxVAO);
        _debugOverlayBoxVAO = 0;
        _debugOverlayBoxVBO = 0;
    }

    if (!shareContexts && _punctualLights)
        _punctualLights->cleanup();

    // The shadow-map texture may survive under shared contexts, but its FBO
    // does not. Force ensureShadowMap() to rebuild the framebuffer next pass.
    _shadowMapNeedsInitialization = true;
}

void SceneRenderController::restoreGpuResources()
{
    if (_whiteTexture == 0)
        initWhiteTexture();
    if (_debugNeutralTex == 0 || _debugNormalTex == 0 || _debugBlackTex == 0)
        initDebugPlaceholderTextures();
}

void SceneRenderController::initDebugPlaceholderTextures()
{
    // Moved from ViewportWidget::initializeGL()'s makeDebugTex lambda -
    // previously never appeared in cleanupGLResources()'s delete list at
    // all, leaking 3 textures on every context recreation.
    auto makeDebugTex = [this](const GLubyte rgba[4]) -> GLuint {
        GLuint texId = 0;
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return texId;
    };

    const GLubyte white[4]         = { 255, 255, 255, 255 };
    const GLubyte neutralNormal[4] = { 128, 128, 255, 255 };
    const GLubyte black[4]         = {   0,   0,   0, 255 };

    _debugNeutralTex = makeDebugTex(white);
    _debugNormalTex  = makeDebugTex(neutralNormal);
    _debugBlackTex   = makeDebugTex(black);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void SceneRenderController::initAxisGeometry(float extent)
{
    if (!_axisVAO.isCreated())
    {
        _axisVAO.create();
        _axisVAO.bind();
    }

    if (!_axisVBO.isCreated())
    {
        _axisVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisVBO.create();
    }
    _axisVBO.bind();
    _axisVBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    const std::vector<float> vertices = {
        0.0f, 0.0f, 0.0f,   extent, 0.0f,   0.0f,
        0.0f, 0.0f, 0.0f,   0.0f,   extent, 0.0f,
        0.0f, 0.0f, 0.0f,   0.0f,   0.0f,   extent,
    };
    _axisVBO.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));

    if (!_axisCBO.isCreated())
    {
        _axisCBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
        _axisCBO.create();
    }
    _axisCBO.bind();
    _axisCBO.setUsagePattern(QOpenGLBuffer::StaticDraw);
    const std::vector<float> colors = {
        1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f,
    };
    _axisCBO.allocate(colors.data(), static_cast<int>(colors.size() * sizeof(float)));
}

void SceneRenderController::initDebugOverlayGeometry(const std::vector<float>& vertices)
{
    if (_debugOverlayBoxVAO == 0)
        glGenVertexArrays(1, &_debugOverlayBoxVAO);
    if (_debugOverlayBoxVBO == 0)
        glGenBuffers(1, &_debugOverlayBoxVBO);

    glBindVertexArray(_debugOverlayBoxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _debugOverlayBoxVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(),
                 GL_DYNAMIC_DRAW);
}

void SceneRenderController::initViewCubeLabelGeometry()
{
    if (_viewCubeLabelVAO == 0)
        glGenVertexArrays(1, &_viewCubeLabelVAO);
    if (_viewCubeLabelVBO == 0)
        glGenBuffers(1, &_viewCubeLabelVBO);
}

void SceneRenderController::createEnvironmentMapTexture()
{
    glGenTextures(1, &_environmentMap);
}

bool SceneRenderController::captureEnvironmentCubemapCPU(std::vector<float> outFaces[6], int& outFaceSize)
{
    if (_environmentMap == 0)
        return false;

    GLint prevBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prevBinding);

    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(prevBinding));
        return false;
    }

    outFaceSize = w; // cubemap faces are always square
    for (int i = 0; i < 6; ++i)
    {
        outFaces[i].resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, GL_FLOAT, outFaces[i].data());
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(prevBinding));
    return true;
}

bool SceneRenderController::captureIrradianceCubemapCPU(std::vector<float> outFaces[6], int& outFaceSize)
{
    if (_irradianceMap == 0)
        return false;

    GLint prevBinding = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prevBinding);

    glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);

    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(prevBinding));
        return false;
    }

    outFaceSize = w;
    for (int i = 0; i < 6; ++i)
    {
        outFaces[i].resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, GL_FLOAT, outFaces[i].data());
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(prevBinding));
    return true;
}

bool SceneRenderController::captureCubemapMipChainCPU(GLuint texture, unsigned int mipLevels, std::vector<PrefilterMipCPU>& outMips)
{
    if (texture == 0 || mipLevels == 0)
        return false;

    GLint prevBinding = 0;
    this->glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prevBinding);

    this->glBindTexture(GL_TEXTURE_CUBE_MAP, texture);

    std::vector<SceneRenderController::PrefilterMipCPU> mips;
    mips.reserve(mipLevels);
    for (unsigned int mip = 0; mip < mipLevels; ++mip)
    {
        GLint w = 0, h = 0;
        this->glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, static_cast<GLint>(mip), GL_TEXTURE_WIDTH, &w);
        this->glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, static_cast<GLint>(mip), GL_TEXTURE_HEIGHT, &h);
        if (w <= 0 || h <= 0)
            break; // ran past the texture's actual allocated mip chain

        SceneRenderController::PrefilterMipCPU mipData;
        mipData.faceSize = w;
        for (int i = 0; i < 6; ++i)
        {
            mipData.faces[i].resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
            this->glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, static_cast<GLint>(mip), GL_RGB, GL_FLOAT, mipData.faces[i].data());
        }
        mips.push_back(std::move(mipData));
    }

    this->glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(prevBinding));

    if (mips.empty())
        return false;

    outMips = std::move(mips);
    return true;
}

bool SceneRenderController::capturePrefilterCubemapCPU(std::vector<PrefilterMipCPU>& outMips)
{
    return captureCubemapMipChainCPU(_prefilterMap, _prefilterMipLevels, outMips);
}

bool SceneRenderController::captureSheenPrefilterCubemapCPU(std::vector<PrefilterMipCPU>& outMips)
{
    return captureCubemapMipChainCPU(_sheenPrefilterMap, _sheenPrefilterMipLevels, outMips);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

void SceneRenderController::initShaders(const QString& path)
{
    auto load = [](std::unique_ptr<ShaderProgram>& shader, const QString& name,
                   const QString& vert, const QString& frag,
                   const QString& geom = QString())
    {
        shader = std::make_unique<ShaderProgram>();
        shader->setObjectName(name);
        if (geom.isEmpty())
            shader->loadCompileAndLinkShaderFromFile(vert, frag);
        else
            shader->loadCompileAndLinkShaderFromFile(vert, frag, geom);
    };

    load(_fgShader,                 "_fgShader",                 path + "shaders/main_scene.vert",           path + "shaders/main_scene.frag");
    load(_fgFlatShader,             "_fgFlatShader",             path + "shaders/main_scene_flat.vert",      path + "shaders/main_scene.frag",              path + "shaders/main_scene_flat.geom");
    load(_wireframeShader,          "_wireframeShader",          path + "shaders/wireframe.vert",            path + "shaders/wireframe.frag");
    load(_axisShader,               "_axisShader",               path + "shaders/axis.vert",                 path + "shaders/axis.frag");
    load(_vertexNormalShader,       "_vertexNormalShader",       path + "shaders/vertex_normal.vert",        path + "shaders/vertex_normal.frag",           path + "shaders/vertex_normal.geom");
    load(_faceNormalShader,         "_faceNormalShader",         path + "shaders/face_normal.vert",          path + "shaders/face_normal.frag",             path + "shaders/face_normal.geom");
    load(_shadowMappingShader,      "_shadowMappingShader",      path + "shaders/shadow_mapping_depth.vert", path + "shaders/shadow_mapping_depth.frag");
    load(_skyBoxShader,             "_skyBoxShader",             path + "shaders/skybox.vert",               path + "shaders/skybox.frag");
    load(_gridShader,               "_gridShader",               path + "shaders/grid.vert",                 path + "shaders/grid.frag");
    load(_irradianceShader,         "_irradianceShader",         path + "shaders/fullscreen_triangle.vert",  path + "shaders/irradiance_convolution.frag");
    load(_prefilterShader,          "_prefilterShader",          path + "shaders/fullscreen_triangle.vert",  path + "shaders/prefilter.frag");
    load(_sheenPrefilterShader,     "_sheenPrefilterShader",     path + "shaders/fullscreen_triangle.vert",  path + "shaders/prefilter_charlie.frag");
    load(_brdfShader,               "_brdfShader",               path + "shaders/brdf.vert",                 path + "shaders/brdf.frag");
    load(_textShader,               "_textShader",               path + "shaders/text.vert",                 path + "shaders/text.frag");
    load(_bgShader,                 "_bgShader",                 path + "shaders/background.vert",           path + "shaders/background.frag");
    load(_bgSplitShader,            "_bgSplitShader",            path + "shaders/splitScreen.vert",          path + "shaders/splitScreen.frag");
    load(_lightCubeShader,          "_lightCubeShader",          path + "shaders/light_cube.vert",           path + "shaders/light_cube.frag");
    load(_viewCubeShader,           "_viewCubeShader",           path + "shaders/viewcube.vert",             path + "shaders/viewcube.frag");
    load(_viewCubeLabelShader,      "_viewCubeLabelShader",      path + "shaders/viewcube_label.vert",       path + "shaders/viewcube_label.frag");
    load(_clippingPlaneShader,      "_clippingPlaneShader",      path + "shaders/clipping_plane.vert",       path + "shaders/clipping_plane.frag");
    load(_clippedMeshShader,        "_clippedMeshShader",        path + "shaders/clipped_mesh.vert",         path + "shaders/clipped_mesh.frag");
    load(_selectionShader,          "_selectionShader",          path + "shaders/selection.vert",            path + "shaders/selection.frag");
    load(_equirectToCubeShader,     "_equirectToCubeShader",     path + "shaders/equirect_to_cube.vert",     path + "shaders/equirect_to_cube.frag");
    load(_equirectToCubeQuadShader, "_equirectToCubeQuadShader", path + "shaders/equirect_to_cube_quad.vert",path + "shaders/equirect_to_cube_quad.frag");
    load(_downsampleShader,         "_downsampleShader",         path + "shaders/downsample_cubemap.vert",   path + "shaders/downsample_cubemap.frag");
    load(_debugShader,              "_debugShader",              path + "shaders/debug_quad.vert",           path + "shaders/debug_quad_depth.frag");
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void SceneRenderController::initFullscreenTriangle()
{
    const float verts[6] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f
    };

    glGenVertexArrays(1, &_fsTriVAO);
    glGenBuffers(1, &_fsTriVBO);

    glBindVertexArray(_fsTriVAO);
    glBindBuffer(GL_ARRAY_BUFFER, _fsTriVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    _fsTriInitialized = true;
}

void SceneRenderController::initWhiteTexture()
{
    unsigned char white[] = { 255, 255, 255, 255 };
    glGenTextures(1, &_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, _whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// ---------------------------------------------------------------------------
// Transmission FBO
// ---------------------------------------------------------------------------

void SceneRenderController::initTransmissionBuffer(int width, int height)
{
    if (_transmissionTextureWidth == width && _transmissionTextureHeight == height
        && _transmissionFBO != 0)
        return;

    _transmissionTextureWidth  = width;
    _transmissionTextureHeight = height;

    if (_transmissionFBO != 0)
        glDeleteFramebuffers(1, &_transmissionFBO);
    if (_transmissionColorTexture != 0)
        glDeleteTextures(1, &_transmissionColorTexture);
    if (_transmissionDepthTexture != 0)
        glDeleteTextures(1, &_transmissionDepthTexture);

    glGenFramebuffers(1, &_transmissionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _transmissionFBO);

    glGenTextures(1, &_transmissionColorTexture);
    glBindTexture(GL_TEXTURE_2D, _transmissionColorTexture);
    int maxDim  = std::max(_transmissionTextureWidth, _transmissionTextureHeight);
    int numMips = static_cast<int>(std::floor(std::log2(maxDim))) + 1;
    glTexStorage2D(GL_TEXTURE_2D, numMips, GL_RGBA32F, _transmissionTextureWidth, _transmissionTextureHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,    numMips - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,   GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,   GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,       GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,       GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _transmissionColorTexture, 0);

    glGenTextures(1, &_transmissionDepthTexture);
    glBindTexture(GL_TEXTURE_2D, _transmissionDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                 _transmissionTextureWidth, _transmissionTextureHeight,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _transmissionDepthTexture, 0);

    GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        qWarning() << "Transmission FBO incomplete! Status:" << status;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// SSS FBOs
// ---------------------------------------------------------------------------

void SceneRenderController::initSSSBuffer(int width, int height)
{
    if (_sssTextureWidth == width && _sssTextureHeight == height && _sssFBO != 0)
        return;

    _sssTextureWidth  = width;
    _sssTextureHeight = height;

    cleanupSSSBuffer();

    glGenFramebuffers(1, &_sssFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _sssFBO);

    glGenTextures(1, &_sssCaptureTexture);
    glBindTexture(GL_TEXTURE_2D, _sssCaptureTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, _sssTextureWidth, _sssTextureHeight,
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _sssCaptureTexture, 0);

    glGenTextures(1, &_sssDepthTexture);
    glBindTexture(GL_TEXTURE_2D, _sssDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, _sssTextureWidth, _sssTextureHeight,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _sssDepthTexture, 0);

    {
        GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, drawBufs);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "SSS capture FBO incomplete! Status:" << status;
    }

    glGenFramebuffers(1, &_sssBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, _sssBlurFBO);

    glGenTextures(1, &_sssBlurTexture);
    glBindTexture(GL_TEXTURE_2D, _sssBlurTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, _sssTextureWidth, _sssTextureHeight,
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _sssBlurTexture, 0);

    {
        GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, drawBufs);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            qWarning() << "SSS blur FBO incomplete! Status:" << status;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// Transmission FBO cleanup
// ---------------------------------------------------------------------------

void SceneRenderController::cleanupTransmissionBuffer()
{
    if (_transmissionFBO != 0)
    {
        glDeleteFramebuffers(1, &_transmissionFBO);
        _transmissionFBO = 0;
    }
    if (_transmissionColorTexture != 0)
    {
        glDeleteTextures(1, &_transmissionColorTexture);
        _transmissionColorTexture = 0;
    }
    if (_transmissionDepthTexture != 0)
    {
        glDeleteTextures(1, &_transmissionDepthTexture);
        _transmissionDepthTexture = 0;
    }
}

// ---------------------------------------------------------------------------
// Shadow map
// ---------------------------------------------------------------------------

void SceneRenderController::ensureShadowMap(GLuint defaultFBO)
{
    if (_shadowMap == 0 || _shadowMapNeedsInitialization)
    {
        if (_shadowMap != 0)
        {
            glDeleteTextures(1, &_shadowMap);
            _shadowMap = 0;
        }
        glGenTextures(1, &_shadowMap);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, _shadowMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                     _shadowWidth, _shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

        if (_shadowMapFBO != 0)
        {
            glDeleteFramebuffers(1, &_shadowMapFBO);
            _shadowMapFBO = 0;
        }
        glGenFramebuffers(1, &_shadowMapFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _shadowMapFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadowMap, 0);
        unsigned long status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Frame buffer creation failed!" << std::endl;
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFBO);
        glActiveTexture(GL_TEXTURE0);
    }
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------

void SceneRenderController::drawFullscreenTriangle()
{
    if (!_fsTriInitialized)
    {
        qWarning() << "Fullscreen triangle not initialized!";
        return;
    }
    glBindVertexArray(_fsTriVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void SceneRenderController::renderQuad()
{
    if (_quadVAO == 0)
    {
        float quadVertices[] = {
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        glGenVertexArrays(1, &_quadVAO);
        glGenBuffers(1, &_quadVBO);
        glBindVertexArray(_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void SceneRenderController::renderConversionCube()
{
    if (_conversionCubeVAO == 0)
    {
        float vertices[] = {
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &_conversionCubeVAO);
        glGenBuffers(1, &_conversionCubeVBO);
        glBindBuffer(GL_ARRAY_BUFFER, _conversionCubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindVertexArray(_conversionCubeVAO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    glBindVertexArray(_conversionCubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// IBL pipeline
// ---------------------------------------------------------------------------

void SceneRenderController::setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex)
{
    auto setM = [prog](const QVector3D& U, const QVector3D& V, const QVector3D& W) {
        QMatrix3x3 m;
        m(0, 0) = U.x(); m(1, 0) = U.y(); m(2, 0) = U.z();
        m(0, 1) = V.x(); m(1, 1) = V.y(); m(2, 1) = V.z();
        m(0, 2) = W.x(); m(1, 2) = W.y(); m(2, 2) = W.z();
        prog->setUniformValue("faceBasis", m);
    };

    // Basis vectors with 90° X-axis rotation applied
    switch (faceIndex)
    {
    case 0: setM(QVector3D(0,1,0), QVector3D(0,0,1), QVector3D(1,0,0)); break; // +X
    case 1: setM(QVector3D(0,-1,0), QVector3D(0,0,1), QVector3D(-1,0,0)); break; // -X
    case 2: setM(QVector3D(1,0,0), QVector3D(0,-1,0), QVector3D(0,0,-1)); break; // +Y
    case 3: setM(QVector3D(1,0,0), QVector3D(0,1,0), QVector3D(0,0,1)); break; // -Y
    case 4: setM(QVector3D(1,0,0), QVector3D(0,0,1), QVector3D(0,-1,0)); break; // +Z
    case 5: setM(QVector3D(-1,0,0), QVector3D(0,0,1), QVector3D(0,1,0)); break; // -Z
    }
}

void SceneRenderController::generateIBL(GLuint defaultFBO)
{
    unsigned int captureFBO = 0;
    unsigned int captureRBO = 0;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);
    auto cleanupFBO = [&]() {
        glDeleteFramebuffers(1, &captureFBO);
        glDeleteRenderbuffers(1, &captureRBO);
    };

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_CULL_FACE);

    // ==== IRRADIANCE PASS ====
    if (_irradianceMap)
        glDeleteTextures(1, &_irradianceMap);
    glGenTextures(1, &_irradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);

    constexpr int irradianceSize = 64;
    for (unsigned int i = 0; i < 6; ++i)
    {
        if (_skyBoxTextureHDRI)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
                irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
        else
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                irradianceSize, irradianceSize, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceSize, irradianceSize);

    _irradianceShader->bind();
    _irradianceShader->setUniformValue("environmentMap", 1);
    _irradianceShader->setUniformValue("resolution", QVector2D(irradianceSize, irradianceSize));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);

    glViewport(0, 0, irradianceSize, irradianceSize);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for (unsigned int i = 0; i < 6; ++i)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _irradianceMap, 0);

        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
        {
            qWarning() << "Irradiance FBO incomplete at face" << i << "Status:" << fboStatus;
            continue;
        }
        _irradianceShader->bind();
        setIBLFaceBasis(_irradianceShader.get(), i);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        drawFullscreenTriangle();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // ==== PREFILTER PASS ====
    if (_prefilterMap)
        glDeleteTextures(1, &_prefilterMap);
    glGenTextures(1, &_prefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _prefilterMap);

    constexpr int prefilterSize = 512;
    unsigned int maxMipLevels = static_cast<unsigned int>(std::log2(prefilterSize)) + 1;
    constexpr unsigned int effectiveMipLevels = 5;
    _prefilterMipLevels = effectiveMipLevels;

    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipSize = static_cast<unsigned int>(prefilterSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i)
        {
            if (_skyBoxTextureHDRI)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                    mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
            else
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB16F,
                    mipSize, mipSize, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, _anisotropicFilteringLevel);

    GLint envMapWidth = 512;
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &envMapWidth);

    _prefilterShader->bind();
    _prefilterShader->setUniformValue("environmentMap", 1);
    _prefilterShader->setUniformValue("environmentMapResolution", static_cast<float>(envMapWidth));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth  = static_cast<unsigned int>(prefilterSize * std::pow(0.5, mip));
        unsigned int mipHeight = mipWidth;

        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (mip < effectiveMipLevels)
            ? (float)mip / (float)(effectiveMipLevels - 1)
            : 1.0f;
        _prefilterShader->bind();
        _prefilterShader->setUniformValue("roughness", roughness);
        _prefilterShader->setUniformValue("resolution", QVector2D(mipWidth, mipHeight));

        for (unsigned int i = 0; i < 6; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _prefilterMap, mip);

            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
            {
                qWarning() << "Prefilter FBO incomplete at mip" << mip << "face" << i
                           << "Status:" << fboStatus;
                continue;
            }
            _prefilterShader->bind();
            setIBLFaceBasis(_prefilterShader.get(), i);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            drawFullscreenTriangle();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // ==== SHEEN PREFILTER PASS ====
    if (_sheenPrefilterMap)
        glDeleteTextures(1, &_sheenPrefilterMap);
    glGenTextures(1, &_sheenPrefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _sheenPrefilterMap);

    constexpr int sheenPrefilterSize = 256;
    constexpr int sheenEffectiveMipLevels = 5;
    const unsigned int sheenMaxMipLevels =
        static_cast<unsigned int>(std::log2(sheenPrefilterSize)) + 1;

    for (unsigned int mip = 0; mip < sheenMaxMipLevels; ++mip)
    {
        unsigned int mipSize = static_cast<unsigned int>(sheenPrefilterSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i)
        {
            if (_skyBoxTextureHDRI)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                    mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
            else
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB16F,
                    mipSize, mipSize, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, _anisotropicFilteringLevel);

    _sheenPrefilterShader->bind();
    _sheenPrefilterShader->setUniformValue("environmentMap", 1);
    _sheenPrefilterShader->setUniformValue("environmentMapResolution", static_cast<float>(envMapWidth));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for (unsigned int mip = 0; mip < sheenMaxMipLevels; ++mip)
    {
        unsigned int mipWidth  = static_cast<unsigned int>(sheenPrefilterSize * std::pow(0.5, mip));
        unsigned int mipHeight = mipWidth;

        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (mip < static_cast<unsigned int>(sheenEffectiveMipLevels))
            ? static_cast<float>(mip) / static_cast<float>(sheenEffectiveMipLevels - 1)
            : 1.0f;
        _sheenPrefilterShader->bind();
        _sheenPrefilterShader->setUniformValue("roughness", roughness);
        _sheenPrefilterShader->setUniformValue("resolution", QVector2D(mipWidth, mipHeight));

        for (unsigned int i = 0; i < 6; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _sheenPrefilterMap, mip);

            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
            {
                qWarning() << "Sheen prefilter FBO incomplete at mip" << mip << "face" << i
                           << "Status:" << fboStatus;
                continue;
            }
            _sheenPrefilterShader->bind();
            setIBLFaceBasis(_sheenPrefilterShader.get(), i);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawFullscreenTriangle();
        }
    }
    _sheenPrefilterMipLevels = sheenEffectiveMipLevels;

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // ==== BRDF LUT PASS ====
    if (_brdfLUTTexture)
        glDeleteTextures(1, &_brdfLUTTexture);
    glGenTextures(1, &_brdfLUTTexture);

    constexpr int lutTextureSize = 512;
    glBindTexture(GL_TEXTURE_2D, _brdfLUTTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, lutTextureSize, lutTextureSize, 0, GL_RGB, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, lutTextureSize, lutTextureSize);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _brdfLUTTexture, 0);

    glViewport(0, 0, lutTextureSize, lutTextureSize);
    _brdfShader->bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // Bind pre-computed IBL data to texture units
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _irradianceMap);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _prefilterMap);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, _brdfLUTTexture);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, _sheenPrefilterMap);

    // Load Khronos LUT textures
    auto resolveKhronosLUTPath = [](const QString& fileName) -> QString {
        const QString dataCandidate = QDir(PathUtils::getDataDirectory()).absoluteFilePath(
            "textures/khronos/" + fileName);
        if (QFileInfo::exists(dataCandidate))
            return dataCandidate;
        const QString sourceCandidate = QDir(QDir::currentPath()).absoluteFilePath(
            "textures/khronos/" + fileName);
        if (QFileInfo::exists(sourceCandidate))
            return sourceCandidate;
        return dataCandidate;
    };

    auto loadKhronosLUT = [this, &resolveKhronosLUTPath](const QString& fileName, GLuint& texId) {
        const QString filePath = resolveKhronosLUTPath(fileName);
        QImage image(filePath);
        if (image.isNull())
        {
            qWarning() << "Failed to load Khronos LUT texture:" << filePath;
            if (texId != 0) { glDeleteTextures(1, &texId); texId = 0; }
            return;
        }
        QImage glImage = image.convertToFormat(QImage::Format_RGBA8888);
        if (texId != 0)
            glDeleteTextures(1, &texId);
        glGenTextures(1, &texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, glImage.width(), glImage.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    };

    if (_charlieLUTTexture == 0)
        loadKhronosLUT("lut_charlie.png", _charlieLUTTexture);
    if (_sheenELUTTexture == 0)
        loadKhronosLUT("lut_sheen_E.png", _sheenELUTTexture);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, _charlieLUTTexture);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, _sheenELUTTexture);

    cleanupFBO();
}

bool SceneRenderController::convertEquirectToCubemap(const QString& filePath, GLuint defaultFBO)
{
    int imgWidth, imgHeight, channels;
    float* data = HdrImageLoader::load(filePath.toStdString(), imgWidth, imgHeight, channels, true);

    if (!data || imgWidth != 2 * imgHeight)
    {
        qWarning() << "Invalid equirectangular HDR file:" << filePath;
        if (data) stbi_image_free(data);
        return false;
    }

    size_t totalPixels = imgWidth * imgHeight * channels;
    int invalidCount = 0;
    for (size_t i = 0; i < totalPixels; i++)
    {
        if (!std::isfinite(data[i]) || data[i] < 0.0f)
        {
            data[i] = (i > 0 && std::isfinite(data[i - 1])) ? data[i - 1] : 0.001f;
            invalidCount++;
        }
        else if (data[i] > 65504.0f)
        {
            data[i] = 65504.0f;
            invalidCount++;
        }
    }
    if (invalidCount > 0)
        qDebug() << "Fixed" << invalidCount << "invalid pixels in" << filePath;

    GLuint equirectTexture;
    glGenTextures(1, &equirectTexture);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, imgWidth, imgHeight, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    int cubeSize = 1 << static_cast<int>(std::log2(std::min(imgWidth / 4, 2048)));
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    for (int mip = 0; mip < static_cast<int>(std::log2(cubeSize)) + 1; ++mip)
    {
        int mipSize = cubeSize >> mip;
        for (int i = 0; i < 6; ++i)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }

    GLuint framebuffer, depthBuffer;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &depthBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    _equirectToCubeShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    _equirectToCubeShader->setUniformValue("equirectangularMap", 0);

    glViewport(0, 0, cubeSize, cubeSize);

    QMatrix4x4 captureViews[] = {
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, {-1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 1, 0}, {0, 0, 1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0,-1, 0}, {0, 0,-1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0, 1}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0,-1}, {0,-1, 0}); return m; }()
    };

    QMatrix4x4 captureProjection;
    captureProjection.perspective(90.0f, 1.0f, 0.1f, 10.0f);
    _equirectToCubeShader->setUniformValue("projection", captureProjection);

    for (int i = 0; i < 6; ++i)
    {
        _equirectToCubeShader->setUniformValue("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _environmentMap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderConversionCube();
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &depthBuffer);
    glDeleteTextures(1, &equirectTexture);
    return true;
}

bool SceneRenderController::convertEquirectToCubemapQuad(const QString& filePath, GLuint defaultFBO)
{
    int imgWidth, imgHeight, channels;
    stbi_set_flip_vertically_on_load(true);
    float* data = stbi_loadf(filePath.toStdString().c_str(), &imgWidth, &imgHeight, &channels, 0);

    if (!data || imgWidth != 2 * imgHeight)
    {
        qWarning() << "Invalid equirectangular HDR file:" << filePath;
        if (data) stbi_image_free(data);
        return false;
    }

    GLuint equirectTexture;
    glGenTextures(1, &equirectTexture);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, imgWidth, imgHeight, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    int cubeSize = 1 << static_cast<int>(std::log2(std::min(imgWidth / 4, 2048)));
    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    for (int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
            cubeSize, cubeSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }

    float quadVertices[] = { -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f };
    unsigned int quadIndices[] = { 0, 1, 2, 0, 2, 3 };

    GLuint quadVAO, quadVBO, quadEBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glGenBuffers(1, &quadEBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint framebuffer, depthBuffer;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &depthBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    _equirectToCubeQuadShader->bind();
    glViewport(0, 0, cubeSize, cubeSize);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    _equirectToCubeQuadShader->setUniformValue("equirectangularMap", 0);

    for (int i = 0; i < 6; ++i)
    {
        _equirectToCubeQuadShader->setUniformValue("faceIndex", i);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, _environmentMap, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            qWarning() << "Framebuffer incomplete for face" << i;
            continue;
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(quadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, _environmentMap);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &depthBuffer);
    glDeleteTextures(1, &equirectTexture);
    glDeleteBuffers(1, &quadVBO);
    glDeleteBuffers(1, &quadEBO);
    glDeleteVertexArrays(1, &quadVAO);
    return true;
}

GLuint SceneRenderController::loadPresetEnvMap(const QString& hdrFilePath, GLuint defaultFBO)
{
    int imgWidth, imgHeight, channels;
    stbi_set_flip_vertically_on_load(true);
    float* data = stbi_loadf(hdrFilePath.toStdString().c_str(), &imgWidth, &imgHeight, &channels, 0);

    if (!data || imgWidth != 2 * imgHeight)
    {
        qWarning() << "Failed to load HDR file:" << hdrFilePath;
        if (data) stbi_image_free(data);
        return 0;
    }

    GLuint equirectTexture;
    glGenTextures(1, &equirectTexture);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, imgWidth, imgHeight, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    GLuint cubemap;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);

    int cubeSize = 512;
    for (int mip = 0; mip < static_cast<int>(std::log2(cubeSize)) + 1; ++mip)
    {
        int mipSize = cubeSize >> mip;
        for (int i = 0; i < 6; ++i)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint framebuffer, depthBuffer;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &depthBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    _equirectToCubeShader->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectTexture);
    _equirectToCubeShader->setUniformValue("equirectangularMap", 0);
    glViewport(0, 0, cubeSize, cubeSize);

    QMatrix4x4 captureViews[] = {
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, {-1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 1, 0}, {0, 0, 1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0,-1, 0}, {0, 0,-1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0, 1}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0,-1}, {0,-1, 0}); return m; }()
    };
    QMatrix4x4 captureProjection;
    captureProjection.perspective(90.0f, 1.0f, 0.1f, 10.0f);
    _equirectToCubeShader->setUniformValue("projection", captureProjection);

    for (int i = 0; i < 6; ++i)
    {
        _equirectToCubeShader->setUniformValue("view", captureViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderConversionCube();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &depthBuffer);
    glDeleteTextures(1, &equirectTexture);
    return cubemap;
}

bool SceneRenderController::generatePresetIBLMaps(GLuint sourceCubemap,
                                                   GLuint& outIrradianceMap,
                                                   GLuint& outPrefilterMap,
                                                   GLuint& outSheenPrefilterMap,
                                                   GLuint defaultFBO)
{
    if (!sourceCubemap) return false;

    unsigned int captureFBO, captureRBO;
    glGenFramebuffers(1, &captureFBO);
    glGenRenderbuffers(1, &captureRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

    // Irradiance map
    if (outIrradianceMap) glDeleteTextures(1, &outIrradianceMap);
    glGenTextures(1, &outIrradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outIrradianceMap);

    constexpr int irradianceSize = 64;
    for (unsigned int i = 0; i < 6; ++i)
    {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F,
            irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    _irradianceShader->bind();
    _irradianceShader->setUniformValue("environmentMap", 1);
    _irradianceShader->setUniformValue("resolution", QVector2D(irradianceSize, irradianceSize));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glViewport(0, 0, irradianceSize, irradianceSize);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceSize, irradianceSize);

    for (unsigned int i = 0; i < 6; ++i)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, outIrradianceMap, 0);
        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
        {
            qWarning() << "Irradiance FBO incomplete at face" << i;
            continue;
        }
        _irradianceShader->bind();
        setIBLFaceBasis(_irradianceShader.get(), i);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawFullscreenTriangle();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // Prefilter map
    if (outPrefilterMap) glDeleteTextures(1, &outPrefilterMap);
    glGenTextures(1, &outPrefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outPrefilterMap);

    constexpr int prefilterSize = 512;
    unsigned int maxMipLevels = static_cast<unsigned int>(std::log2(prefilterSize)) + 1;
    constexpr unsigned int effectiveMipLevels = 5;

    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipSize = static_cast<unsigned int>(prefilterSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLint envMapWidth = 512;
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &envMapWidth);

    _prefilterShader->bind();
    _prefilterShader->setUniformValue("environmentMap", 1);
    _prefilterShader->setUniformValue("environmentMapResolution", static_cast<float>(envMapWidth));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
    {
        unsigned int mipWidth  = static_cast<unsigned int>(prefilterSize * std::pow(0.5, mip));
        unsigned int mipHeight = mipWidth;
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (mip < effectiveMipLevels)
            ? (float)mip / (float)(effectiveMipLevels - 1)
            : 1.0f;
        _prefilterShader->bind();
        _prefilterShader->setUniformValue("roughness", roughness);
        _prefilterShader->setUniformValue("resolution", QVector2D(mipWidth, mipHeight));

        for (unsigned int i = 0; i < 6; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, outPrefilterMap, mip);
            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
            {
                qWarning() << "Prefilter FBO incomplete at mip" << mip << "face" << i;
                continue;
            }
            _prefilterShader->bind();
            setIBLFaceBasis(_prefilterShader.get(), i);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawFullscreenTriangle();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);

    // Sheen prefilter map
    if (outSheenPrefilterMap) glDeleteTextures(1, &outSheenPrefilterMap);
    glGenTextures(1, &outSheenPrefilterMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outSheenPrefilterMap);

    constexpr int sheenPrefilterSize = 256;
    constexpr int sheenEffectiveMipLevels = 5;
    const unsigned int sheenMaxMipLevels =
        static_cast<unsigned int>(std::log2(sheenPrefilterSize)) + 1;

    for (unsigned int mip = 0; mip < sheenMaxMipLevels; ++mip)
    {
        unsigned int mipSize = static_cast<unsigned int>(sheenPrefilterSize * std::pow(0.5, mip));
        for (unsigned int i = 0; i < 6; ++i)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, GL_RGB32F,
                mipSize, mipSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &envMapWidth);

    _sheenPrefilterShader->bind();
    _sheenPrefilterShader->setUniformValue("environmentMap", 1);
    _sheenPrefilterShader->setUniformValue("environmentMapResolution", static_cast<float>(envMapWidth));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, sourceCubemap);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

    for (unsigned int mip = 0; mip < sheenMaxMipLevels; ++mip)
    {
        unsigned int mipWidth  = static_cast<unsigned int>(sheenPrefilterSize * std::pow(0.5, mip));
        unsigned int mipHeight = mipWidth;
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (mip < static_cast<unsigned int>(sheenEffectiveMipLevels))
            ? static_cast<float>(mip) / static_cast<float>(sheenEffectiveMipLevels - 1)
            : 1.0f;
        _sheenPrefilterShader->bind();
        _sheenPrefilterShader->setUniformValue("roughness", roughness);
        _sheenPrefilterShader->setUniformValue("resolution", QVector2D(mipWidth, mipHeight));

        for (unsigned int i = 0; i < 6; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, outSheenPrefilterMap, mip);
            GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fboStatus != GL_FRAMEBUFFER_COMPLETE)
            {
                qWarning() << "Sheen prefilter FBO incomplete at mip" << mip << "face" << i;
                continue;
            }
            _sheenPrefilterShader->bind();
            setIBLFaceBasis(_sheenPrefilterShader.get(), i);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawFullscreenTriangle();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);
    return true;
}

void SceneRenderController::generateCubemapMipmaps(GLuint cubemapTexture,
                                                    int viewportWidth, int viewportHeight,
                                                    GLuint defaultFBO)
{
    GLint baseSize = 0;
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glGetTextureLevelParameteriv(cubemapTexture, 0, GL_TEXTURE_WIDTH, &baseSize);

    int maxMipLevels = static_cast<int>(std::log2(baseSize)) + 1;

    GLuint mipmapFBO, mipmapRBO;
    glGenFramebuffers(1, &mipmapFBO);
    glGenRenderbuffers(1, &mipmapRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, mipmapFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, mipmapRBO);

    QMatrix4x4 captureProjection;
    captureProjection.perspective(90.0f, 1.0f, 0.1f, 10.0f);

    QMatrix4x4 captureViews[] = {
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, {-1, 0, 0}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 1, 0}, {0, 0, 1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0,-1, 0}, {0, 0,-1}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0, 1}, {0,-1, 0}); return m; }(),
        []() { QMatrix4x4 m; m.lookAt({0,0,0}, { 0, 0,-1}, {0,-1, 0}); return m; }()
    };

    _downsampleShader->bind();
    _downsampleShader->setUniformValue("projection", captureProjection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    _downsampleShader->setUniformValue("sourceMap", 0);

    for (int mip = 1; mip < maxMipLevels; ++mip)
    {
        int mipSize = baseSize >> mip;
        glBindRenderbuffer(GL_RENDERBUFFER, mipmapRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
        glViewport(0, 0, mipSize, mipSize);
        _downsampleShader->setUniformValue("currentMipLevel", mip - 1);

        for (int face = 0; face < 6; ++face)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemapTexture, mip);
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                qWarning() << "Framebuffer incomplete at mip" << mip << "face" << face;
                continue;
            }
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            _downsampleShader->setUniformValue("view", captureViews[face]);
            renderConversionCube();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glDeleteFramebuffers(1, &mipmapFBO);
    glDeleteRenderbuffers(1, &mipmapRBO);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    glViewport(0, 0, viewportWidth, viewportHeight);

    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void SceneRenderController::cleanupSSSBuffer()
{
    if (_sssFBO != 0)            { glDeleteFramebuffers(1, &_sssFBO);            _sssFBO            = 0; }
    if (_sssCaptureTexture != 0) { glDeleteTextures(1, &_sssCaptureTexture);     _sssCaptureTexture = 0; }
    if (_sssDepthTexture != 0)   { glDeleteTextures(1, &_sssDepthTexture);       _sssDepthTexture   = 0; }
    if (_sssBlurFBO != 0)        { glDeleteFramebuffers(1, &_sssBlurFBO);        _sssBlurFBO        = 0; }
    if (_sssBlurTexture != 0)    { glDeleteTextures(1, &_sssBlurTexture);        _sssBlurTexture    = 0; }
}

// ---------------------------------------------------------------------------
// ViewCube / floor static helpers
// ---------------------------------------------------------------------------

const SceneRenderController::ViewCubeStyle SceneRenderController::kViewCubeStyle;

std::array<SceneRenderController::ViewCubeLabelFace, 6>
SceneRenderController::buildViewCubeLabelFaces(
    const QString& top, const QString& front, const QString& left,
    const QString& bottom, const QString& rear, const QString& right,
    const QQuaternion& axisRotation)
{
    // Viewer convention is Z-up:
    // Top = +Z, Bottom = -Z, Front = -Y, Rear = +Y, Left = -X, Right = +X.
    std::array<ViewCubeLabelFace, 6> faces = {{
        { top,    QVector3D( 1.0f,  0.0f, 0.0f), QVector3D( 0.0f,  1.0f, 0.0f), QVector3D( 0.0f,  0.0f,  1.0f) },
        { bottom, QVector3D( 1.0f,  0.0f, 0.0f), QVector3D( 0.0f, -1.0f, 0.0f), QVector3D( 0.0f,  0.0f, -1.0f) },
        { front,  QVector3D( 1.0f,  0.0f, 0.0f), QVector3D( 0.0f,  0.0f, 1.0f), QVector3D( 0.0f, -1.0f,  0.0f) },
        { rear,   QVector3D(-1.0f,  0.0f, 0.0f), QVector3D( 0.0f,  0.0f, 1.0f), QVector3D( 0.0f,  1.0f,  0.0f) },
        { left,   QVector3D( 0.0f, -1.0f, 0.0f), QVector3D( 0.0f,  0.0f, 1.0f), QVector3D(-1.0f,  0.0f,  0.0f) },
        { right,  QVector3D( 0.0f,  1.0f, 0.0f), QVector3D( 0.0f,  0.0f, 1.0f), QVector3D( 1.0f,  0.0f,  0.0f) }
    }};

    for (ViewCubeLabelFace& face : faces)
    {
        face.right  = axisRotation.rotatedVector(face.right).normalized();
        face.up     = axisRotation.rotatedVector(face.up).normalized();
        face.normal = axisRotation.rotatedVector(face.normal).normalized();
    }
    return faces;
}

float SceneRenderController::computeFloorDepthBias(float workspaceExtent, float floorSize)
{
    const float extentBias       = std::max(workspaceExtent, 0.0f) * 1.0e-5f;
    const float floorFallbackBias = std::max(floorSize, 0.0f) * 1.0e-8f;
    return std::clamp(std::max(extentBias, floorFallbackBias), 1.0e-6f, 1.0e-4f);
}
