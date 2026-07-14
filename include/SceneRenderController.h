#pragma once

#include "PunctualLights.h"
#include "RenderEnums.h"
#include "ShaderProgram.h"

#include <QColor>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QQuaternion>
#include <QString>
#include <QVector3D>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// SceneRenderController
//
// Owns all GPU render-pipeline state: shader programs, IBL/environment maps,
// shadow map, transmission and SSS buffers, utility render geometry, and the
// render settings flags that gate each pass.
//
// Public API:
//   - Lifecycle: initialize(), cleanupGLResources()
//   - Shader init: initShaders(shaderPath)
//   - Geometry init: initFullscreenTriangle(), initWhiteTexture(),
//                    initAxisGeometry(extent),
//                    initDebugOverlayGeometry(vertices),
//                    initViewCubeLabelGeometry()
//   - FBO management: initTransmissionBuffer(), cleanupTransmissionBuffer(),
//                     initSSSBuffer(), cleanupSSSBuffer(),
//                     ensureShadowMap(defaultFBO)
//   - IBL pipeline: generateIBL(), convertEquirectToCubemap(), etc.
//   - Draw helpers: drawFullscreenTriangle(), renderQuad(), renderConversionCube()
//   - Accessors: typed getters/setters for all render state
//   - Static helpers: ViewCubeStyle, buildViewCubeLabelFaces(), computeFloorDepthBias()
// ---------------------------------------------------------------------------
class SceneRenderController : public QOpenGLFunctions_4_5_Core
{
public:
    // ---- Lifecycle ---------------------------------------------------------
    bool initialize();

    // Deletes all owned GL resources. Must be called while a GL context is
    // current (typically from ViewportWidget's destructor after makeCurrent()).
    void cleanupGLResources();

    // ---- Shader init -------------------------------------------------------
    void initShaders(const QString& shaderPath);

    // ---- Geometry init -----------------------------------------------------
    void initFullscreenTriangle();
    void initWhiteTexture();

    // Creates / updates the axis-widget geometry buffers with the given half-extent.
    void initAxisGeometry(float extent);

    // Creates / reallocates the debug overlay bounding-box VAO/VBO with given vertex data.
    void initDebugOverlayGeometry(const std::vector<float>& vertices);

    // Creates the ViewCube label quad VAO/VBO (geometry only; textures uploaded by caller).
    void initViewCubeLabelGeometry();

    // Calls glGenTextures and stores the result in _environmentMap.
    void createEnvironmentMapTexture();

    // ---- FBO init / resize -------------------------------------------------
    void initTransmissionBuffer(int width, int height);
    void cleanupTransmissionBuffer();
    void initSSSBuffer(int width, int height);
    void cleanupSSSBuffer();

    // ---- Shadow map --------------------------------------------------------
    void ensureShadowMap(GLuint defaultFBO);

    // ---- Draw helpers ------------------------------------------------------
    void drawFullscreenTriangle();
    void renderQuad();
    void renderConversionCube();

    // ---- IBL pipeline ------------------------------------------------------
    void   generateIBL(GLuint defaultFBO);
    bool   convertEquirectToCubemap(const QString& filePath, GLuint defaultFBO);
    bool   convertEquirectToCubemapQuad(const QString& filePath, GLuint defaultFBO);
    GLuint loadPresetEnvMap(const QString& hdrFilePath, GLuint defaultFBO);
    bool   generatePresetIBLMaps(GLuint sourceCubemap,
                                  GLuint& outIrradianceMap,
                                  GLuint& outPrefilterMap,
                                  GLuint& outSheenPrefilterMap,
                                  GLuint defaultFBO);
    void   generateCubemapMipmaps(GLuint cubemapTexture,
                                   int viewportWidth, int viewportHeight,
                                   GLuint defaultFBO);

    // ========================================================================
    // Accessors
    // ========================================================================

    // ---- Shader programs ---------------------------------------------------
    ShaderProgram* bgShader()               const { return _bgShader.get(); }
    ShaderProgram* bgSplitShader()          const { return _bgSplitShader.get(); }
    ShaderProgram* fgShader()               const { return _fgShader.get(); }
    ShaderProgram* fgFlatShader()           const { return _fgFlatShader.get(); }
    ShaderProgram* wireframeShader()        const { return _wireframeShader.get(); }
    ShaderProgram* axisShader()             const { return _axisShader.get(); }
    ShaderProgram* vertexNormalShader()     const { return _vertexNormalShader.get(); }
    ShaderProgram* faceNormalShader()       const { return _faceNormalShader.get(); }
    ShaderProgram* shadowMappingShader()    const { return _shadowMappingShader.get(); }
    ShaderProgram* skyBoxShader()           const { return _skyBoxShader.get(); }
    ShaderProgram* gridShader()             const { return _gridShader.get(); }
    ShaderProgram* irradianceShader()       const { return _irradianceShader.get(); }
    ShaderProgram* prefilterShader()        const { return _prefilterShader.get(); }
    ShaderProgram* sheenPrefilterShader()   const { return _sheenPrefilterShader.get(); }
    ShaderProgram* brdfShader()             const { return _brdfShader.get(); }
    ShaderProgram* lightCubeShader()        const { return _lightCubeShader.get(); }
    ShaderProgram* viewCubeShader()         const { return _viewCubeShader.get(); }
    ShaderProgram* viewCubeLabelShader()    const { return _viewCubeLabelShader.get(); }
    ShaderProgram* clippingPlaneShader()    const { return _clippingPlaneShader.get(); }
    ShaderProgram* clippedMeshShader()      const { return _clippedMeshShader.get(); }
    ShaderProgram* selectionShader()        const { return _selectionShader.get(); }
    ShaderProgram* equirectToCubeShader()   const { return _equirectToCubeShader.get(); }
    ShaderProgram* equirectToCubeQuadShader() const { return _equirectToCubeQuadShader.get(); }
    ShaderProgram* downsampleShader()       const { return _downsampleShader.get(); }
    ShaderProgram* textShader()             const { return _textShader.get(); }
    ShaderProgram* debugShader()            const { return _debugShader.get(); }

    // ---- IBL / environment maps --------------------------------------------
    GLuint environmentMap()           const { return _environmentMap; }
    void   setEnvironmentMap(GLuint t)      { _environmentMap = t; }

    // Reads back all 6 faces of the currently-bound scene environment cubemap
    // (_environmentMap - the one the raster skybox/reflections actually
    // sample) into CPU memory, via glGetTexImage. This works regardless of
    // how the cubemap was populated (single equirectangular HDR, 6 separate
    // face images, a strip format, ...) since it always reads the exact same
    // GPU texture the raster path shows - unlike an earlier approach that
    // cached the equirect HDR source at load time (broken for the common
    // case where the active skybox is a 6-face-image folder, which has no
    // equirect source to cache at all). outFaces[i] is faceSize*faceSize*3
    // RGB floats in GL_TEXTURE_CUBE_MAP_POSITIVE_X+i order. Returns false
    // (leaving outFaces/outFaceSize untouched) if no environment cubemap is
    // currently bound. Synchronous GPU readback - only call this when
    // starting a new path-traced session (camera-settled), not per frame.
    bool captureEnvironmentCubemapCPU(std::vector<float> outFaces[6], int& outFaceSize);

    // Same idea as captureEnvironmentCubemapCPU() above, but reading
    // irradianceMap() (the fully diffuse-convolved cubemap) instead of the
    // raw environment map. Returns false (leaving outFaces/outFaceSize
    // untouched) if no irradiance map is currently bound.
    bool captureIrradianceCubemapCPU(std::vector<float> outFaces[6], int& outFaceSize);

    // One captured mip level of prefilterMap() - see capturePrefilterCubemapCPU().
    struct PrefilterMipCPU
    {
        std::vector<float> faces[6];
        int faceSize = 0;
    };

    // Reads every mip level (0..prefilterMipLevels()-1) of prefilterMap()
    // into CPU memory, appending one PrefilterMipCPU per level to outMips
    // (mip 0 = sharpest/largest, matching GL's own mip ordering). Returns
    // false (leaving outMips untouched) if no prefilter map is currently
    // bound. Same synchronous-GPU-readback caveat as
    // captureEnvironmentCubemapCPU() - only call when starting a new
    // path-traced session, not per frame.
    bool capturePrefilterCubemapCPU(std::vector<PrefilterMipCPU>& outMips);
    bool captureSheenPrefilterCubemapCPU(std::vector<PrefilterMipCPU>& outMips);

    GLuint irradianceMap()            const { return _irradianceMap; }
    void   setIrradianceMap(GLuint t)       { _irradianceMap = t; }

    GLuint prefilterMap()             const { return _prefilterMap; }
    void   setPrefilterMap(GLuint t)        { _prefilterMap = t; }

    GLuint sheenPrefilterMap()        const { return _sheenPrefilterMap; }
    void   setSheenPrefilterMap(GLuint t)   { _sheenPrefilterMap = t; }

    unsigned int prefilterMipLevels()       const { return _prefilterMipLevels; }
    unsigned int sheenPrefilterMipLevels()  const { return _sheenPrefilterMipLevels; }

    GLuint brdfLUTTexture()           const { return _brdfLUTTexture; }
    void   setBrdfLUTTexture(GLuint t)      { _brdfLUTTexture = t; }

    GLuint charlieLUTTexture()        const { return _charlieLUTTexture; }
    void   setCharlieLUTTexture(GLuint t)   { _charlieLUTTexture = t; }

    GLuint sheenELUTTexture()         const { return _sheenELUTTexture; }
    void   setSheenELUTTexture(GLuint t)    { _sheenELUTTexture = t; }

    const QString& currentSkyboxFolder() const { return _currentSkyboxFolder; }
    void setCurrentSkyboxFolder(const QString& f) { _currentSkyboxFolder = f; }

    // Preset environment maps
    GLuint studioEnvironmentMap()               const { return _studioEnvironmentMap; }
    void   setStudioEnvironmentMap(GLuint t)          { _studioEnvironmentMap = t; }
    GLuint studioIrradianceMap()               const { return _studioIrradianceMap; }
    void   setStudioIrradianceMap(GLuint t)           { _studioIrradianceMap = t; }
    GLuint studioPrefilterMap()                const { return _studioPrefilterMap; }
    void   setStudioPrefilterMap(GLuint t)            { _studioPrefilterMap = t; }
    GLuint studioSheenPrefilterMap()           const { return _studioSheenPrefilterMap; }
    void   setStudioSheenPrefilterMap(GLuint t)       { _studioSheenPrefilterMap = t; }

    GLuint outdoorEnvironmentMap()             const { return _outdoorEnvironmentMap; }
    void   setOutdoorEnvironmentMap(GLuint t)         { _outdoorEnvironmentMap = t; }
    GLuint outdoorIrradianceMap()              const { return _outdoorIrradianceMap; }
    void   setOutdoorIrradianceMap(GLuint t)          { _outdoorIrradianceMap = t; }
    GLuint outdoorPrefilterMap()               const { return _outdoorPrefilterMap; }
    void   setOutdoorPrefilterMap(GLuint t)           { _outdoorPrefilterMap = t; }
    GLuint outdoorSheenPrefilterMap()          const { return _outdoorSheenPrefilterMap; }
    void   setOutdoorSheenPrefilterMap(GLuint t)      { _outdoorSheenPrefilterMap = t; }

    GLuint officeEnvironmentMap()              const { return _officeEnvironmentMap; }
    void   setOfficeEnvironmentMap(GLuint t)          { _officeEnvironmentMap = t; }
    GLuint officeIrradianceMap()               const { return _officeIrradianceMap; }
    void   setOfficeIrradianceMap(GLuint t)           { _officeIrradianceMap = t; }
    GLuint officePrefilterMap()                const { return _officePrefilterMap; }
    void   setOfficePrefilterMap(GLuint t)            { _officePrefilterMap = t; }
    GLuint officeSheenPrefilterMap()           const { return _officeSheenPrefilterMap; }
    void   setOfficeSheenPrefilterMap(GLuint t)       { _officeSheenPrefilterMap = t; }

    GLuint skyboxFBO()         const { return _skyboxFBO; }
    GLuint skyboxDepthBuffer() const { return _skyboxDepthBuffer; }

    // ---- Shadow map --------------------------------------------------------
    GLuint       shadowMap()           const { return _shadowMap; }
    GLuint       shadowMapFBO()        const { return _shadowMapFBO; }
    unsigned int shadowWidth()         const { return _shadowWidth; }
    void         setShadowWidth(unsigned int w)  { _shadowWidth = w; }
    unsigned int shadowHeight()        const { return _shadowHeight; }
    void         setShadowHeight(unsigned int h) { _shadowHeight = h; }
    float        shadowFarDist()       const { return _shadowFarDist; }
    void         setShadowFarDist(float d)   { _shadowFarDist = d; }
    float        shadowFrustumExtentW() const { return _shadowFrustumExtentW; }
    void         setShadowFrustumExtentW(float e) { _shadowFrustumExtentW = e; }
    float        shadowFrustumExtentH() const { return _shadowFrustumExtentH; }
    void         setShadowFrustumExtentH(float e) { _shadowFrustumExtentH = e; }
    bool         shadowMapNeedsInitialization() const { return _shadowMapNeedsInitialization; }
    void         setShadowMapNeedsInitialization(bool v) { _shadowMapNeedsInitialization = v; }

    // ---- Transmission buffer -----------------------------------------------
    GLuint transmissionFBO()              const { return _transmissionFBO; }
    GLuint transmissionColorTexture()     const { return _transmissionColorTexture; }
    GLuint transmissionDepthTexture()     const { return _transmissionDepthTexture; }
    int    transmissionTextureWidth()     const { return _transmissionTextureWidth; }
    int    transmissionTextureHeight()    const { return _transmissionTextureHeight; }
    int    transmissionMipLevels()        const { return _transmissionMipLevels; }
    bool   transmissionEnabled()          const { return _transmissionEnabled; }
    void   setTransmissionEnabled(bool v)       { _transmissionEnabled = v; }

    // ---- SSS buffer --------------------------------------------------------
    GLuint sssFBO()            const { return _sssFBO; }
    GLuint sssCaptureTexture() const { return _sssCaptureTexture; }
    GLuint sssDepthTexture()   const { return _sssDepthTexture; }
    GLuint sssBlurFBO()        const { return _sssBlurFBO; }
    GLuint sssBlurTexture()    const { return _sssBlurTexture; }
    int    sssTextureWidth()   const { return _sssTextureWidth; }
    int    sssTextureHeight()  const { return _sssTextureHeight; }
    bool   sssEnabled()        const { return _sssEnabled; }
    void   setSssEnabled(bool v)     { _sssEnabled = v; }

    // ---- Debug / utility textures ------------------------------------------
    GLuint debugNeutralTex()     const { return _debugNeutralTex; }
    void   setDebugNeutralTex(GLuint t)  { _debugNeutralTex = t; }
    GLuint debugNormalTex()      const { return _debugNormalTex; }
    void   setDebugNormalTex(GLuint t)   { _debugNormalTex = t; }
    GLuint debugBlackTex()       const { return _debugBlackTex; }
    void   setDebugBlackTex(GLuint t)    { _debugBlackTex = t; }
    int    globalDebugChannel()  const { return _globalDebugChannel; }
    void   setGlobalDebugChannel(int ch) { _globalDebugChannel = ch; }
    GLuint whiteTexture()        const { return _whiteTexture; }

    // ---- Utility render geometry -------------------------------------------
    // Mutable references allow glGen*/glDelete* and direct VAO/VBO calls.
    GLuint& debugOverlayBoxVAO() { return _debugOverlayBoxVAO; }
    GLuint  debugOverlayBoxVAO() const { return _debugOverlayBoxVAO; }
    GLuint& debugOverlayBoxVBO() { return _debugOverlayBoxVBO; }

    QOpenGLVertexArrayObject& bgVAO()      { return _bgVAO; }
    QOpenGLVertexArrayObject& bgSplitVAO() { return _bgSplitVAO; }
    QOpenGLBuffer&            bgSplitVBO() { return _bgSplitVBO; }

    QOpenGLVertexArrayObject& axisVAO() { return _axisVAO; }
    QOpenGLBuffer&            axisVBO() { return _axisVBO; }
    QOpenGLBuffer&            axisCBO() { return _axisCBO; }

    GLuint conversionCubeVAO() const { return _conversionCubeVAO; }
    GLuint quadVAO()           const { return _quadVAO; }
    GLuint fsTriVAO()          const { return _fsTriVAO; }

    // ---- ViewCube label textures/geometry ----------------------------------
    std::array<unsigned int, 6>&       viewCubeLabelTextures()       { return _viewCubeLabelTextures; }
    const std::array<unsigned int, 6>& viewCubeLabelTextures() const { return _viewCubeLabelTextures; }
    GLuint viewCubeLabelVAO() const { return _viewCubeLabelVAO; }
    GLuint viewCubeLabelVBO() const { return _viewCubeLabelVBO; }

    // ---- Punctual lights GPU buffer ----------------------------------------
    PunctualLights*       punctualLights()       { return _punctualLights.get(); }
    const PunctualLights* punctualLights() const { return _punctualLights.get(); }
    void initLights()                { _punctualLights = std::make_unique<PunctualLights>(); }

    // ---- Capping -----------------------------------------------------------
    bool   cappingEnabled()       const { return _cappingEnabled; }
    void   setCappingEnabled(bool v)    { _cappingEnabled = v; }
    GLuint cappingTexture()       const { return _cappingTexture; }
    void   setCappingTexture(GLuint t)  { _cappingTexture = t; }

    // ---- Clipping plane state ----------------------------------------------
    bool  yzClippingEnabled()             const { return _clipYZEnabled; }
    void  setYZClippingEnabled(bool v)          { _clipYZEnabled = v; }
    bool  zxClippingEnabled()             const { return _clipZXEnabled; }
    void  setZXClippingEnabled(bool v)          { _clipZXEnabled = v; }
    bool  xyClippingEnabled()             const { return _clipXYEnabled; }
    void  setXYClippingEnabled(bool v)          { _clipXYEnabled = v; }

    bool  clippingXFlipped()              const { return _clipXFlipped; }
    void  setClippingXFlipped(bool v)           { _clipXFlipped = v; }
    bool  clippingYFlipped()              const { return _clipYFlipped; }
    void  setClippingYFlipped(bool v)           { _clipYFlipped = v; }
    bool  clippingZFlipped()              const { return _clipZFlipped; }
    void  setClippingZFlipped(bool v)           { _clipZFlipped = v; }

    float clippingXCoeff()                const { return _clipXCoeff; }
    void  setClippingXCoeff(float v)            { _clipXCoeff = v; }
    float clippingYCoeff()                const { return _clipYCoeff; }
    void  setClippingYCoeff(float v)            { _clipYCoeff = v; }
    float clippingZCoeff()                const { return _clipZCoeff; }
    void  setClippingZCoeff(float v)            { _clipZCoeff = v; }

    float clipDX()                        const { return _clipDX; }
    void  setClipDX(float v)                    { _clipDX = v; }
    float clipDY()                        const { return _clipDY; }
    void  setClipDY(float v)                    { _clipDY = v; }
    float clipDZ()                        const { return _clipDZ; }
    void  setClipDZ(float v)                    { _clipDZ = v; }

    // ---- Render settings ---------------------------------------------------
    bool isOpenGLInitialized() const { return _openGLInitialized; }
    void setOpenGLInitialized(bool v) { _openGLInitialized = v; }

    bool       envMapEnabled()           const { return _envMapEnabled; }
    void       setEnvMapEnabled(bool v)        { _envMapEnabled = v; }

    bool       shadowsEnabled()          const { return _shadowsEnabled; }
    void       setShadowsEnabled(bool v)       { _shadowsEnabled = v; }

    bool       selfShadowsEnabled()      const { return _selfShadowsEnabled; }
    void       setSelfShadowsEnabled(bool v)   { _selfShadowsEnabled = v; }

    bool       reflectionsEnabled()      const { return _reflectionsEnabled; }
    void       setReflectionsEnabled(bool v)   { _reflectionsEnabled = v; }

    GroundMode groundMode()              const { return _groundMode; }
    void       setGroundMode(GroundMode m)     { _groundMode = m; }

    bool       floorTextureDisplayed()   const { return _floorTextureDisplayed; }
    void       setFloorTextureDisplayed(bool v) { _floorTextureDisplayed = v; }

    bool       skyBoxEnabled()           const { return _skyBoxEnabled; }
    void       setSkyBoxEnabled(bool v)        { _skyBoxEnabled = v; }

    int        skyBoxBlurPercent()       const { return _skyBoxBlurPercent; }
    void       setSkyBoxBlurPercent(int p)     { _skyBoxBlurPercent = p; }

    const std::vector<QString>& skyBoxFaces() const { return _skyBoxFaces; }
    void setSkyBoxFaces(const std::vector<QString>& f) { _skyBoxFaces = f; }

    float      skyBoxFOV()               const { return _skyBoxFOV; }
    void       setSkyBoxFOV(float v)           { _skyBoxFOV = v; }

    float      skyBoxZRotation()         const { return _skyBoxZRotation; }
    void       setSkyBoxZRotation(float v)     { _skyBoxZRotation = v; }

    bool       skyBoxTextureHDRI()       const { return _skyBoxTextureHDRI; }
    void       setSkyBoxTextureHDRI(bool v)    { _skyBoxTextureHDRI = v; }

    bool       gammaCorrection()         const { return _gammaCorrection; }
    void       setGammaCorrection(bool v)      { _gammaCorrection = v; }

    float      screenGamma()             const { return _screenGamma; }
    void       setScreenGamma(float v)         { _screenGamma = v; }

    bool       hdrToneMapping()          const { return _hdrToneMapping; }
    void       setHdrToneMapping(bool v)       { _hdrToneMapping = v; }

    HDRToneMapMode toneMappingMode()     const { return _toneMappingMode; }
    void setToneMappingMode(HDRToneMapMode m)  { _toneMappingMode = m; }

    float      envMapExposure()          const { return _envMapExposure; }
    void       setEnvMapExposure(float v)      { _envMapExposure = v; }

    float      iblExposure()             const { return _iblExposure; }
    void       setIblExposure(float v)         { _iblExposure = v; }

    bool       lowResEnabled()           const { return _lowResEnabled; }
    void       setLowResEnabled(bool v)        { _lowResEnabled = v; }

    bool       sectionCapsSuppressedDuringInteraction() const { return _sectionCapsSuppressedDuringInteraction; }
    void       setSectionCapsSuppressedDuringInteraction(bool v) { _sectionCapsSuppressedDuringInteraction = v; }

    bool       dynamicCappingEnabled()   const { return _dynamicCappingEnabled; }
    void       setDynamicCappingEnabled(bool v) { _dynamicCappingEnabled = v; }

    float      anisotropicFilteringLevel() const { return _anisotropicFilteringLevel; }
    void       setAnisotropicFilteringLevel(float v) { _anisotropicFilteringLevel = v; }

    bool       useDefaultLights()        const { return _useDefaultLights; }
    void       setUseDefaultLights(bool v)     { _useDefaultLights = v; }

    bool       usePunctualLights()       const { return _usePunctualLights; }
    void       setUsePunctualLights(bool v)    { _usePunctualLights = v; }

    bool       useIBL()                  const { return _useIBL; }
    void       setUseIBL(bool v)               { _useIBL = v; }

    // ---- Background & rendering mode ---------------------------------------
    QColor       bgTopColor()                  const { return _bgTopColor; }
    void         setBgTopColor(const QColor& c)      { _bgTopColor = c; }
    QColor       bgBotColor()                  const { return _bgBotColor; }
    void         setBgBotColor(const QColor& c)      { _bgBotColor = c; }
    int          gradientStyle()               const { return _gradientStyle; }
    void         setGradientStyle(int s)             { _gradientStyle = s; }
    int          bgStyleIndex()               const { return _bgStyleIndex; }
    void         setBgStyleIndex(int s)             { _bgStyleIndex = s; }
    RenderingMode renderingMode()              const { return _renderingMode; }
    void         setRenderingMode(RenderingMode m)   { _renderingMode = m; }
    bool         backfaceCulling()            const { return _backfaceCulling; }
    void         setBackfaceCulling(bool v)          { _backfaceCulling = v; }

    // ---- Floor texture / offset --------------------------------------------
    float        floorTexRepeatS()             const { return _floorTexRepeatS; }
    void         setFloorTexRepeatS(float v)         { _floorTexRepeatS = v; }
    float        floorTexRepeatT()             const { return _floorTexRepeatT; }
    void         setFloorTexRepeatT(float v)         { _floorTexRepeatT = v; }
    float        floorOffsetPercent()          const { return _floorOffsetPercent; }
    void         setFloorOffsetPercent(float v)      { _floorOffsetPercent = v; }

    // ---- Default light -----------------------------------------------------
    QVector4D    defaultLightColor()           const { return _defaultLightColor; }
    void         setDefaultLightColor(const QVector4D& c) { _defaultLightColor = c; }
    QVector3D    lightOffset()                 const { return _lightOffset; }
    void         setLightOffset(const QVector3D& v)  { _lightOffset = v; }
    bool         showLights()                  const { return _showLights; }
    void         setShowLights(bool v)               { _showLights = v; }

    // ---- Clipping-plane hatch ----------------------------------------------
    ClippingPlaneHatchMode hatchMode()         const { return _hatchMode; }
    void setHatchMode(ClippingPlaneHatchMode m)      { _hatchMode = m; }
    HatchPattern hatchPattern()                const { return _hatchPattern; }
    void setHatchPattern(HatchPattern p)             { _hatchPattern = p; }
    int          hatchTiling()                 const { return _hatchTiling; }
    void         setHatchTiling(int v)               { _hatchTiling = v; }
    float        hatchThickness()              const { return _hatchThickness; }
    void         setHatchThickness(float v)          { _hatchThickness = v; }
    float        hatchIntensity()              const { return _hatchIntensity; }
    void         setHatchIntensity(float v)          { _hatchIntensity = v; }
    int          hatchLayers()                 const { return _hatchLayers; }
    void         setHatchLayers(int v)               { _hatchLayers = v; }
    QVector3D    hatchLineColor()              const { return _hatchLineColor; }
    void         setHatchLineColor(const QVector3D& c) { _hatchLineColor = c; }
    QString      hatchTexturePath()            const { return _hatchTexturePath; }
    void         setHatchTexturePath(const QString& p) { _hatchTexturePath = p; }

    // ---- Debug overlay -----------------------------------------------------
    bool           showVertexNormals()        const { return _showVertexNormals; }
    void           setShowVertexNormals(bool v)     { _showVertexNormals = v; }

    bool           showFaceNormals()          const { return _showFaceNormals; }
    void           setShowFaceNormals(bool v)       { _showFaceNormals = v; }

    bool           showBoundingBox()          const { return _showBoundingBox; }
    void           setShowBoundingBox(bool v)       { _showBoundingBox = v; }

    bool           debugOverlayEnabled()      const { return _debugOverlayEnabled; }
    void           setDebugOverlayEnabled(bool v)   { _debugOverlayEnabled = v; }

    DebugOverlayMode debugOverlayMode()       const { return _debugOverlayMode; }
    void setDebugOverlayMode(DebugOverlayMode m)    { _debugOverlayMode = m; }

    bool           debugBoundingBoxAvailable()      const { return _debugBoundingBoxAvailable; }
    void           setDebugBoundingBoxAvailable(bool v)   { _debugBoundingBoxAvailable = v; }

    bool           debugVertexNormalsAvailable()    const { return _debugVertexNormalsAvailable; }
    void           setDebugVertexNormalsAvailable(bool v) { _debugVertexNormalsAvailable = v; }

    bool           debugFaceNormalsAvailable()      const { return _debugFaceNormalsAvailable; }
    void           setDebugFaceNormalsAvailable(bool v)   { _debugFaceNormalsAvailable = v; }

    // ========================================================================
    // ViewCube / floor static helpers
    // ========================================================================

    struct ViewCubeStyle
    {
        QVector3D baseFaceColor         = QVector3D(0.92f, 0.92f, 0.92f);
        QVector3D primaryFaceColor      = QVector3D(0.90f, 0.76f, 0.10f);
        QVector3D hoverFaceColor        = QVector3D(0.74f, 0.96f, 0.18f);
        QColor    labelTextColor        = QColor(60, 60, 60, 235);
        float     baseAmbient           = 0.45f;
        float     baseDiffuse           = 0.55f;
        float     primaryAmbient        = 0.38f;
        float     primaryDiffuse        = 0.62f;
        float     hoverAmbient          = 0.75f;
        float     hoverDiffuse          = 0.25f;
        float     perspectiveScale      = 0.90f;
        float     orthographicScale     = 1.12f;
        float     orthographicHalfHeight = 1.2f;
        float     eyeDistance           = 3.6f;
        int       minViewportSize       = 96;
        int       maxViewportSize       = 160;
        int       viewportPadding       = 18;
        int       labelTextureSize      = 256;
        int       labelFontPixelSize    = 76;
        float     labelFaceOffset       = 0.501f;
        float     labelFaceScale        = 0.68f;
    };

    struct ViewCubeLabelFace
    {
        QString   text;
        QVector3D right;
        QVector3D up;
        QVector3D normal;
    };

    static const ViewCubeStyle kViewCubeStyle;

    static std::array<ViewCubeLabelFace, 6> buildViewCubeLabelFaces(
        const QString& top, const QString& front, const QString& left,
        const QString& bottom, const QString& rear, const QString& right,
        const QQuaternion& axisRotation);

    static float computeFloorDepthBias(float workspaceExtent, float floorSize);

private:
    static void setIBLFaceBasis(QOpenGLShaderProgram* prog, int faceIndex);
    bool captureCubemapMipChainCPU(GLuint texture, unsigned int mipLevels, std::vector<PrefilterMipCPU>& outMips);

    // ---- Shader programs ---------------------------------------------------
    std::unique_ptr<ShaderProgram> _bgShader;
    std::unique_ptr<ShaderProgram> _bgSplitShader;
    std::unique_ptr<ShaderProgram> _fgShader;
    std::unique_ptr<ShaderProgram> _fgFlatShader;
    std::unique_ptr<ShaderProgram> _wireframeShader;
    std::unique_ptr<ShaderProgram> _axisShader;
    std::unique_ptr<ShaderProgram> _vertexNormalShader;
    std::unique_ptr<ShaderProgram> _faceNormalShader;
    std::unique_ptr<ShaderProgram> _shadowMappingShader;
    std::unique_ptr<ShaderProgram> _skyBoxShader;
    std::unique_ptr<ShaderProgram> _gridShader;
    std::unique_ptr<ShaderProgram> _irradianceShader;
    std::unique_ptr<ShaderProgram> _prefilterShader;
    std::unique_ptr<ShaderProgram> _sheenPrefilterShader;
    std::unique_ptr<ShaderProgram> _brdfShader;
    std::unique_ptr<ShaderProgram> _lightCubeShader;
    std::unique_ptr<ShaderProgram> _viewCubeShader;
    std::unique_ptr<ShaderProgram> _viewCubeLabelShader;
    std::unique_ptr<ShaderProgram> _clippingPlaneShader;
    std::unique_ptr<ShaderProgram> _clippedMeshShader;
    std::unique_ptr<ShaderProgram> _selectionShader;
    std::unique_ptr<ShaderProgram> _equirectToCubeShader;
    std::unique_ptr<ShaderProgram> _equirectToCubeQuadShader;
    std::unique_ptr<ShaderProgram> _downsampleShader;
    std::unique_ptr<ShaderProgram> _textShader;
    std::unique_ptr<ShaderProgram> _debugShader;

    // ---- IBL / environment maps --------------------------------------------
    unsigned int _environmentMap        = 0;
    unsigned int _irradianceMap         = 0;
    unsigned int _prefilterMap          = 0;
    unsigned int _sheenPrefilterMap     = 0;
    unsigned int _prefilterMipLevels    = 5;
    unsigned int _sheenPrefilterMipLevels = 5;
    unsigned int _brdfLUTTexture        = 0;
    unsigned int _charlieLUTTexture     = 0;
    unsigned int _sheenELUTTexture      = 0;
    QString      _currentSkyboxFolder;

    unsigned int _studioEnvironmentMap      = 0;
    unsigned int _studioIrradianceMap       = 0;
    unsigned int _studioPrefilterMap        = 0;
    unsigned int _studioSheenPrefilterMap   = 0;

    unsigned int _outdoorEnvironmentMap     = 0;
    unsigned int _outdoorIrradianceMap      = 0;
    unsigned int _outdoorPrefilterMap       = 0;
    unsigned int _outdoorSheenPrefilterMap  = 0;

    unsigned int _officeEnvironmentMap      = 0;
    unsigned int _officeIrradianceMap       = 0;
    unsigned int _officePrefilterMap        = 0;
    unsigned int _officeSheenPrefilterMap   = 0;

    unsigned int _skyboxFBO          = 0;
    unsigned int _skyboxDepthBuffer  = 0;

    // ---- Shadow map --------------------------------------------------------
    unsigned int _shadowMap                    = 0;
    unsigned int _shadowMapFBO                 = 0;
    unsigned int _shadowWidth                  = 0;
    unsigned int _shadowHeight                 = 0;
    float        _shadowFarDist                = 1.0f;
    float        _shadowFrustumExtentW         = -1.0f;
    float        _shadowFrustumExtentH         = -1.0f;
    bool         _shadowMapNeedsInitialization = true;

    // ---- Transmission buffer -----------------------------------------------
    unsigned int _transmissionFBO           = 0;
    unsigned int _transmissionColorTexture  = 0;
    unsigned int _transmissionDepthTexture  = 0;
    int          _transmissionTextureWidth  = 0;
    int          _transmissionTextureHeight = 0;
    int          _transmissionMipLevels     = 0;
    bool         _transmissionEnabled       = true;

    // ---- SSS buffer --------------------------------------------------------
    unsigned int _sssFBO            = 0;
    unsigned int _sssCaptureTexture = 0;
    unsigned int _sssDepthTexture   = 0;
    unsigned int _sssBlurFBO        = 0;
    unsigned int _sssBlurTexture    = 0;
    int          _sssTextureWidth   = 0;
    int          _sssTextureHeight  = 0;
    bool         _sssEnabled        = false;

    // ---- Debug / utility textures ------------------------------------------
    unsigned int _debugNeutralTex   = 0;
    unsigned int _debugNormalTex    = 0;
    unsigned int _debugBlackTex     = 0;
    int          _globalDebugChannel = 0;
    unsigned int _whiteTexture      = 0;

    // ---- Utility render geometry -------------------------------------------
    unsigned int _debugOverlayBoxVAO = 0;
    unsigned int _debugOverlayBoxVBO = 0;

    QOpenGLVertexArrayObject _bgVAO;
    QOpenGLVertexArrayObject _bgSplitVAO;
    QOpenGLBuffer            _bgSplitVBO;

    QOpenGLVertexArrayObject _axisVAO;
    QOpenGLBuffer            _axisVBO;
    QOpenGLBuffer            _axisCBO;

    unsigned int _conversionCubeVAO = 0;
    unsigned int _conversionCubeVBO = 0;

    unsigned int _quadVAO = 0;
    unsigned int _quadVBO = 0;

    unsigned int _fsTriVAO         = 0;
    unsigned int _fsTriVBO         = 0;
    bool         _fsTriInitialized = false;

    std::array<unsigned int, 6> _viewCubeLabelTextures = { 0, 0, 0, 0, 0, 0 };
    unsigned int _viewCubeLabelVAO = 0;
    unsigned int _viewCubeLabelVBO = 0;

    // ---- Punctual lights GPU buffer ----------------------------------------
    std::unique_ptr<PunctualLights> _punctualLights;

    // ---- Capping -----------------------------------------------------------
    bool         _cappingEnabled = false;
    unsigned int _cappingTexture = 0;

    // ---- Clipping plane state ----------------------------------------------
    bool  _clipYZEnabled  = false;
    bool  _clipZXEnabled  = false;
    bool  _clipXYEnabled  = false;
    bool  _clipXFlipped   = false;
    bool  _clipYFlipped   = false;
    bool  _clipZFlipped   = false;
    float _clipXCoeff     = 0.0f;
    float _clipYCoeff     = 0.0f;
    float _clipZCoeff     = 0.0f;
    float _clipDX         = 0.0f;
    float _clipDY         = 0.0f;
    float _clipDZ         = 0.0f;

    // ---- Render settings ---------------------------------------------------
    bool         _openGLInitialized                  = false;
    bool         _envMapEnabled                      = false;
    bool         _shadowsEnabled                     = false;
    bool         _selfShadowsEnabled                 = false;
    bool         _reflectionsEnabled                 = false;
    GroundMode   _groundMode                         = GroundMode::None;
    bool         _floorTextureDisplayed              = false;
    bool         _skyBoxEnabled                      = false;
    int          _skyBoxBlurPercent                  = 0;
    std::vector<QString> _skyBoxFaces;
    float        _skyBoxFOV                          = 0.0f;
    float        _skyBoxZRotation                    = 0.0f;
    bool         _skyBoxTextureHDRI                  = false;
    bool         _gammaCorrection                    = false;
    float        _screenGamma                        = 2.2f;
    bool         _hdrToneMapping                     = false;
    HDRToneMapMode _toneMappingMode                  = HDRToneMapMode::KhronosPbrNeutral;
    float        _envMapExposure                     = 1.0f;
    float        _iblExposure                        = 1.0f;
    bool         _lowResEnabled                      = false;
    bool         _sectionCapsSuppressedDuringInteraction = false;
    bool         _dynamicCappingEnabled              = false;
    float        _anisotropicFilteringLevel          = 16.0f;
    bool         _useDefaultLights                   = false;
    bool         _usePunctualLights                  = false;
    bool         _useIBL                             = true;

    // ---- Background & rendering mode ---------------------------------------
    QColor        _bgTopColor;
    QColor        _bgBotColor;
    int           _gradientStyle                     = 0;
    int           _bgStyleIndex                      = 0; // 0=Gradient, 1=Solid
    RenderingMode _renderingMode                     = RenderingMode::ADS_BLINN_PHONG;
    bool          _backfaceCulling                   = false;

    // ---- Floor texture / offset --------------------------------------------
    float         _floorTexRepeatS                   = 1.0f;
    float         _floorTexRepeatT                   = 1.0f;
    float         _floorOffsetPercent                = 0.0f;

    // ---- Default light -----------------------------------------------------
    QVector4D     _defaultLightColor                 = { 1.0f, 1.0f, 1.0f, 1.0f };
    QVector3D     _lightOffset;
    bool          _showLights                        = false;

    // ---- Clipping-plane hatch ----------------------------------------------
    ClippingPlaneHatchMode _hatchMode                = ClippingPlaneHatchMode::PROCEDURAL;
    HatchPattern           _hatchPattern             = HatchPattern::DIAGONAL_45;
    int                    _hatchTiling              = 50;
    float                  _hatchThickness           = 0.05f;
    float                  _hatchIntensity           = 1.0f;
    int                    _hatchLayers              = 3;
    QVector3D              _hatchLineColor           = QVector3D(0.0f, 0.0f, 0.0f);
    QString                _hatchTexturePath;

    // ---- Debug overlay -----------------------------------------------------
    bool             _showVertexNormals          = false;
    bool             _showFaceNormals            = false;
    bool             _showBoundingBox            = false;
    bool             _debugOverlayEnabled        = false;
    DebugOverlayMode _debugOverlayMode           = DebugOverlayMode::BoundingBox;
    bool             _debugBoundingBoxAvailable  = true;
    bool             _debugVertexNormalsAvailable = true;
    bool             _debugFaceNormalsAvailable  = true;
};
