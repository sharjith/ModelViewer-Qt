#version 450 core
#extension GL_OES_standard_derivatives : enable

// Adpated from https://learnopengl.com/

in vec3 v_position;
in vec3 v_normal;
in vec4 v_color;
in vec4 v_rawVertexColor;
in vec2 v_texCoord0;
in vec2 v_texCoord1;
in vec2 v_texCoord2;
in vec2 v_texCoord3;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec3 v_worldTangent;
in vec3 v_worldBitangent;
in vec3 v_reflectionPosition;
in vec3 v_reflectionNormal;
flat in vec3 v_flatNormal;
flat in vec3 v_reflectionFlatNormal;
flat in vec3 v_positionFlat;
noperspective in vec3 v_positionLinear;
in vec3 v_tangentLightPos;
in vec3 v_tangentViewPos;
in vec3 v_tangentFragPos;

in VS_OUT_SHADOW{
	vec3 FragPos;
	vec3 Normal;
	vec2 TexCoords;
	vec4 FragPosLightSpace;
	vec3 cameraPos;
	vec3 lightPos;
} fs_in_shadow;

uniform bool hasVertexColors;
uniform bool hasNegativeScale;

uniform int primitiveMode;  // 0=POINTS, 1=LINES, 2=LINE_LOOP, 3=LINE_STRIP, 4+=TRIANGLES

uniform float opacity;

// ADS light maps
uniform sampler2D texture_diffuse;
uniform sampler2D texture_specular;
uniform sampler2D texture_emissive;
uniform sampler2D texture_normal;
uniform sampler2D texture_height;
uniform sampler2D texture_opacity;
uniform bool hasDiffuseTexture = false;
uniform bool hasSpecularTexture = false;
uniform bool hasEmissiveTexture = false;
uniform bool hasNormalTexture = false;
uniform bool hasHeightTexture = false;
uniform bool hasOpacityTexture = false;
uniform bool opacityTextureInverted = false;
uniform bool floorTextureEnabled = false;

uniform samplerCube envMap;
uniform sampler2D shadowMap;
uniform float shadowSoftness; // Adjustable softness factor
uniform float lightFarPlane; // Far plane of the light's perspective
uniform int shadowMaxKernelSize;
uniform float shadowSoftnessScale;
uniform float shadowMaxSoftnessClamp;
uniform float shadowBiasMin;
uniform float shadowBiasMax;
uniform float shadowTransitionRange;
uniform float shadowGammaCorrection;
uniform float shadowSizeScale;

// Transmission map
uniform sampler2D transmissionSceneTexture;  // _transmissionColorTexture
uniform sampler2D transmissionDepthTexture;  // _transmissionDepthTexture
uniform vec2	  transmissionFramebufferSize;
uniform sampler2D sssDiffuseTexture;
uniform sampler2D sssDepthTexture;
uniform vec2      sssFramebufferSize;

uniform float floorAlpha = 0.95;
uniform float floorSpecularScale = 0.6;  // scale specular on floor [0..1]
uniform float floorFresnelDampen = 0.5;  // how much to dampen spec at normal incidence [0..1]


// IBL
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform samplerCube sheenPrefilterMap;
uniform sampler2D brdfLUT;
uniform sampler2D charlieLUT;
uniform sampler2D sheenELUT;
// Effective sheen prefilter mip count: LOD = roughness * (sheenPrefilterMipLevels - 1).
// Matches Khronos reference (u_MipCount=5), giving lod=0.4 at roughness=0.1 instead
// of lod=0.8 from textureQueryLevels(). Mip 0 (roughness=0) is the mirror reflection.
uniform int sheenPrefilterMipLevels;
// Effective GGX prefilter mip count: LOD = roughness * (prefilterMipLevels - 1).
// Replaces textureQueryLevels(prefilterMap) so LOD is driven by a known, tunable value
// uploaded from C++ (_prefilterMipLevels = log2(prefilterSize)+1, e.g. 9 for 256 px).
uniform int prefilterMipLevels;
uniform bool useIBL;
uniform bool interactionFastPath;

// material parameters
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D metallicMap;
uniform sampler2D roughnessMap;
uniform sampler2D heightMap;
uniform sampler2D aoMap;
uniform sampler2D emissiveMap;
uniform sampler2D opacityMap;
uniform bool hasAlbedoMap;
uniform bool hasMetallicMap;
uniform bool hasRoughnessMap;
uniform bool hasNormalMap;
uniform bool hasAOMap;
uniform bool hasEmissiveMap;
uniform bool hasHeightMap;
uniform float heightScale = 0.08;
uniform float clearcoatNormalScale = 1.0;
uniform bool hasOpacityMap;
uniform bool opacityMapInverted = false;
uniform int blendMode; // 0 = additive, 1 = multiplicative, 2 = overlay
uniform float alphaThreshold; // Threshold for alpha testing

// packing uniforms
uniform int   metallicChannel;
uniform int   metallicInvert;
uniform float metallicScale;
uniform float metallicBias;

uniform int   roughnessChannel;
uniform int   roughnessInvert;
uniform float roughnessScale;
uniform float roughnessBias;

uniform int   aoChannel;
uniform int   aoInvert;
uniform float aoScale;
uniform float aoBias;

uniform int   opacityChannel;
uniform int   opacityInvert;
uniform float opacityScale;
uniform float opacityBias;

uniform bool twoSided;

// Debug channel isolation (TextureDebugPanel dropdown).
// 0 = normal rendering.  Non-zero = replace fragColor with raw channel value.
// IDs 1-9 are geometry/vertex channels; IDs 10+ match GPU texture unit indices.
//   1=UV0  2=UV1  3=GeoNormal  4=GeoTangent  5=GeoBitangent  6=TangentW  7=ShadingNormal  8=Alpha  9=VertexColor
//   10=Albedo/Diffuse  11=Metallic  12=Emissive  13=NormalMap  14=Height  15=Opacity  16=Roughness  17=AO
//   18=ClearcoatStrength  19=ClearcoatRoughness  20=ClearcoatNormal
//   21=SpecularStrength  22=SpecularColor  23=AnisotropicStrength  32=AnisotropicDirection
//   24=IridescenceStrength  25=IridescenceThickness  26=SheenColor  27=SheenRoughness
//   28=TransmissionStrength  30=VolumeThickness  34=DiffuseTransmissionStrength  35=DiffuseTransmissionColor
uniform int debugChannelOutput = 0;

// Advanced PBR Material Properties
uniform sampler2D transmissionMap;
uniform sampler2D iorMap;
uniform sampler2D sheenColorMap;
uniform sampler2D sheenRoughnessMap;
uniform sampler2D clearcoatColorMap;
uniform sampler2D clearcoatRoughnessMap;
uniform sampler2D clearcoatNormalMap;

uniform bool hasTransmissionMap = false;
uniform bool hasIORMap = false;
uniform bool hasSheenColorMap = false;
uniform bool hasSheenRoughnessMap = false;
uniform bool hasClearcoatMap = false;
uniform bool hasClearcoatRoughnessMap = false;
uniform bool hasClearcoatNormalMap = false;

// KHR_materials_specular
uniform sampler2D specularFactorMap;
uniform sampler2D specularColorMap;
uniform bool hasSpecularFactorMap = false;
uniform bool hasSpecularColorMap = false;

// KHR_materials_pbrSpecularGlossiness
uniform sampler2D diffuseMap;
uniform sampler2D specularGlossinessMap;
uniform bool hasDiffuseMap = false;
uniform bool hasSpecularGlossinessMap = false;
// Flag to indicate this material uses spec-glossiness workflow
uniform bool useSpecularGlossiness = false;
uniform vec3 diffuseFactor;
uniform vec3 specularColor;
uniform float glossinessFactor;

// KHR_materials_anisotropy
uniform sampler2D anisotropyMap;
uniform bool hasAnisotropyMap = false;

// KHR_materials_iridescence
uniform sampler2D iridescenceMap;
uniform sampler2D iridescenceThicknessMap;
uniform bool hasIridescenceMap = false;
uniform bool hasIridescenceThicknessMap = false;

// KHR_materials_diffuse_transmission
uniform sampler2D diffuseTransmissionMap;
uniform sampler2D diffuseTransmissionColorMap;
uniform bool hasDiffuseTransmissionMap = false;
uniform bool hasDiffuseTransmissionColorMap = false;

// Extension-presence flags — true when the KHR extension is active on this
// material (with or without a texture).  Used in the debug block to gate
// extension channels behind a checkerboard when the extension is absent,
// mirroring Khronos viewer's compile-time #ifdef MATERIAL_XXX gating.
uniform bool extClearcoat    = false;
uniform bool extSheen        = false;
uniform bool extTransmission = false;
uniform bool extSpecular     = false;
uniform bool extAnisotropy   = false;
uniform bool extIridescence  = false;
uniform bool extVolume       = false;
uniform bool extDiffuseTrans = false;

// KHR_materials_volume
uniform sampler2D thicknessMap;
uniform bool hasThicknessMap = false;
uniform bool hasThicknessAlpha = false;

// KHR_materials_scatter
uniform vec3 multiScatterColor;
uniform bool hasVolumeScattering;
uniform float sssObjectId = 0.0;

// SSS capture pass: when true, only hasVolumeScattering surfaces are drawn and they
// output raw linear diffuse irradiance (no tone mapping / gamma / alpha handling).
// Set from C++ before renderToSSSBuffer(); cleared for the normal render pass.
uniform bool sssCapture = false;

struct TextureTransform
{
	vec2 scale;
	vec2 offset;
	float rotation;
	int texCoordIndex;  // Which of the 4 texCoords to use
};

// Transform uniforms
uniform TextureTransform albedoTexTransform;
uniform TextureTransform metallicTexTransform;
uniform TextureTransform roughnessTexTransform;
uniform TextureTransform normalTexTransform;
uniform TextureTransform heightTexTransform;
uniform TextureTransform aoTexTransform;
uniform TextureTransform opacityTexTransform;
uniform TextureTransform emissiveTexTransform;
uniform TextureTransform transmissionTexTransform;
uniform TextureTransform iorTexTransform;
uniform TextureTransform sheenColorTexTransform;
uniform TextureTransform sheenRoughnessTexTransform;
uniform TextureTransform clearcoatTexTransform;
uniform TextureTransform clearcoatRoughnessTexTransform;
uniform TextureTransform clearcoatNormalTexTransform;
uniform TextureTransform specularFactorTexTransform;
uniform TextureTransform specularColorTexTransform;
uniform TextureTransform anisotropyTexTransform;
uniform TextureTransform iridescenceTexTransform;
uniform TextureTransform iridescenceThicknessTexTransform;
uniform TextureTransform diffuseTransmissionTexTransform;
uniform TextureTransform diffuseTransmissionColorTexTransform;
uniform TextureTransform thicknessTexTransform;
uniform TextureTransform diffuseTexTransform;
uniform TextureTransform specularGlossinessTexTransform;

// Legacy ADS texture transforms
uniform TextureTransform diffuseTextureTransform;
uniform TextureTransform specularTextureTransform;
uniform TextureTransform emissiveTextureTransform;
uniform TextureTransform normalTextureTransform;
uniform TextureTransform heightTextureTransform;
uniform TextureTransform opacityTextureTransform;

uniform bool isGLTFMaterial;

uniform bool envMapEnabled;
uniform mat3 envMapRotationMatrix;
uniform bool shadowsEnabled;
uniform bool selfShadowsEnabled;
uniform vec3 cameraPos;
uniform vec3 cameraDir;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;
uniform mat4 projectionMatrix;
uniform mat4 inverseProjectionMatrix;  // precomputed on CPU — avoids per-fragment inverse()
uniform bool sectionActive;
uniform int displayMode;
uniform int renderingMode;
uniform int shadingNormalMode; // 0=SMOOTH, 1=FLAT
uniform bool realismEnabled;
uniform bool isWireframePass;
uniform bool selected;
uniform bool selectionHighlighting;
uniform bool hovered = false;  // Is this mesh currently hovered?
uniform bool hoverHighlighting = false;  // Is hover highlighting enabled?
uniform vec3 hoverColor = vec3(1.0, 0.84, 0.0);  // Gold/yellow default hover color
uniform vec4 reflectColor;
uniform bool floorRendering;
uniform int groundMode = 1; // 0=None, 1=Floor, 2=Grid
uniform bool hdrToneMapping = false;
uniform bool gammaCorrection = false;
uniform float screenGamma = 2.2;

uniform float envMapExposure = 1.0;
uniform float iblExposure = 1.0;

// 0=KhronosPbrNeutral, 1=ACES_Narkowicz, 2=ACES_Hill, 3=AECS_Hill_Exposure_Boost,
// 4=Uncharted2, 5=Reinhard(Linear)
uniform int toneMapMode = 0;

uniform bool skyBoxEnabled;

uniform vec4 topColor;
uniform vec4 botColor;
uniform int gradientStyle;
uniform vec2 screenSize;
uniform vec3 screenCenter;
uniform float floorSize;
uniform float groundReferenceSize = 1.0;
uniform bool isReflectedPass;
uniform int worldUpAxis = 2;

struct LineInfo
{
	float Width;
	vec4 Color;
};

uniform LineInfo Line;

struct LightSource
{
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	vec3 position;
};
uniform LightSource lightSource;

struct LightModel
{
	vec3 ambient;
};
uniform LightModel lightModel;

const int LightType_Directional = 0;
const int LightType_Point = 1;
const int LightType_Spot = 2;
const int MAX_LIGHTS = 16;

uniform int lightCount;
uniform bool hasPunctualLights;
uniform bool useDefaultLights;
uniform bool usePunctualLights;

struct PunctualLight
{
	vec3 direction;
	float range;
	vec3 color;
	float intensity;
	vec3 position;
	float innerConeCos;
	float outerConeCos;
	int type;
};

layout(std140, binding = 3) uniform LightBlock
{
	PunctualLight lights[MAX_LIGHTS];
};

struct Material
{
	vec3  emission;
	vec3  ambient;
	vec3  diffuse;
	vec3  specular;
	float shininess;
	bool  metallic;
};
uniform Material material;

struct PBRLighting
{
	vec3 albedo;
	float metallic;
	float roughness;
	float normalScale;
	float ambientOcclusion;
	float occlusionStrength;
	// Advanced PBR Properties
	float transmission;
	float ior;
	vec3 sheenColor;
	float sheenRoughness;
	float clearcoat;
	float clearcoatRoughness;

	// KHR_materials_specular
	float specularFactor;
	vec3 specularColorFactor;

	// KHR_materials_anisotropy
	float anisotropyStrength;
	float anisotropyRotation;

	// KHR_materials_iridescence
	float iridescenceFactor;
	float iridescenceIor;
	float iridescenceThicknessMin;
	float iridescenceThicknessMax;

	// KHR_materials_volume
	float thicknessFactor;
	float attenuationDistance;
	vec3 attenuationColor;

	// KHR_materials_dispersion
	float dispersion;

	// KHR_materials_diffuse_transmission
	float diffuseTransmissionFactor;
	vec3 diffuseTransmissionColorFactor;

	// KHR_materials_unlit
	bool unlit;

	// KHR_emissive_strength
	float emissiveStrength;
};
uniform PBRLighting pbrLighting;

uniform int   tintMode = 1;     // 0=Off, 1=AutoGray, 2=ForceGray, 3=LerpMask
uniform float tintStrength = 1.0;
uniform float grayEpsilon = 0.02; // grayscale detection threshold in sRGB
uniform bool  useVertexColor = false; // include vtx color
uniform int   tintMaskChannel = 0; // 0=R, 1=G, 2=B, 3=A

const float PI = 3.14159265359;

layout(location = 0) out vec4 fragColor;

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

// ---- KHR Anisotropy --------------------------------------------------------
struct AnisotropyData
{
	float strength;      // Final strength (texture x uniform)
	float rotation;      // Final rotation (texture direction rotated by uniform)
	vec2 direction;      // Texture direction in [-1,1] (before rotation)
};

// ---- Primary KHR-aligned PBR path -----------------------------------------
// These structs define the internal contract for the active shading path.
// The existing uniforms/samplers remain the external interface.
struct SurfaceFrame
{
	vec3 V;      // surface -> camera
	vec3 I;      // camera -> surface
	vec3 L;      // surface -> active light
	vec3 Ng;     // geometric normal
	vec3 N;      // shaded base normal
	vec3 Ncoat;  // shaded clearcoat normal
	vec3 T;      // tangent
	vec3 B;      // bitangent
};

struct MaterialParams
{
	vec3  baseColor;
	vec3  emissive;
	vec3  F0;
	vec3  F90;
	vec3  dielectricF0;
	float metallic;
	float roughness;
	float ambientOcclusion;
	float transmission;
	float ior;
	float clearcoat;
	float clearcoatRoughness;
	float sheenRoughness;
	vec3  sheenColor;
	float anisotropyStrength;
	float anisotropyRotation;
	float iridescenceFactor;
	float iridescenceIor;
	float iridescenceThickness;
	float thickness;
	float diffuseTransmissionFactor;
	vec3  diffuseTransmissionColor;
	vec3  attenuationColor;
	float attenuationDistance;
	float dispersion;
	float specularFactor;
	vec3  specularColor;
	bool  useSpecGloss;
	bool  unlit;
};

struct LayerContributions
{
	vec3 baseDirectDiffuse;
	vec3 baseDirectSpecular;
	vec3 baseDiffuseIBL;
	vec3 baseSpecularIBL;
	vec3 sheenDirect;
	vec3 sheenIBL;
	vec3 clearcoatDirect;
	vec3 clearcoatIBL;
	vec3 transmission;
	vec3 emissive;
};

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// ---- Guard utilities -------------------------------------------------------
float	guardFactorScalar(float factor, float threshold);
vec3	guardFactorColor(vec3 factor, float threshold);

// ---- Texture UV Accessors - PBR core ---------------------------------------
vec2	getTransformedUV(int texCoordIndex, TextureTransform transform);
vec2    getAlbedoUV();
vec2    getMetallicUV();
vec2    getRoughnessUV();
vec2    getNormalUV();
vec2    getHeightUV();
vec2    getAOUV();
vec2    getOpacityUV();
vec2	getEmissiveUV();
vec2    getTransmissionUV();

// ---- Texture UV Accessors - KHR extensions ---------------------------------
vec2    getIORUV();
vec2    getSheenColorUV();
vec2    getSheenRoughnessUV();
vec2    getClearcoatUV();
vec2    getClearcoatRoughnessUV();
vec2    getClearcoatNormalUV();
vec2    getSpecularFactorUV();
vec2    getSpecularColorUV();
vec2    getAnisotropyUV();
vec2    getIridescenceUV();
vec2    getIridescenceThicknessUV();
vec2    getThicknessUV();
vec2    getDiffuseTransmissionUV();
vec2    getDiffuseTransmissionColorUV();

// ---- Texture UV Accessors - KHR pbrSpecularGlossiness ----------------------
vec2	getDiffuseUV();
vec2	getSpecularGlossinessUV();

// ---- Texture UV Accessors - Legacy ADS -------------------------------------
vec2    getDiffuseTextureUV();
vec2    getSpecularTextureUV();
vec2    getEmissiveTextureUV();
vec2    getNormalTextureUV();
vec2    getHeightTextureUV();
vec2    getOpacityTextureUV();

// ---- Shading entry points --------------------------------------------------
vec4    shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal);
vec4    calculatePBRLighting(int renderMode, float side);
vec4    calculatePBRLightingKHR(int renderMode, float side);

// ---- Flat shading normal helpers -------------------------------------------
vec3 safeNormalizeGeom(vec3 value, vec3 fallback)
{
    float len = length(value);
    return len > 1e-8 ? value / len : fallback;
}

vec3 getUnsignedViewGeometryNormal()
{
    vec3 baseN = safeNormalizeGeom(v_normal, vec3(0.0, 0.0, 1.0));
    if (shadingNormalMode == 1)
    {
        // v_flatNormal is set by main_scene_flat.geom to the true geometric face
        // normal (cross(edge0, edge1), view-space, same value for all 3 vertices).
        // This is exact and zoom-independent — no dFdx/dFdy needed.
        return safeNormalizeGeom(v_flatNormal, baseN);
    }
    return baseN;
}

vec3 getSignedViewGeometryNormal()
{
    vec3 n = getUnsignedViewGeometryNormal();
    bool front = hasNegativeScale ? !gl_FrontFacing : gl_FrontFacing;
    return front ? n : -n;
}

vec3 getUnsignedWorldGeometryNormal()
{
    vec3 baseN = safeNormalizeGeom(v_reflectionNormal, vec3(0.0, 0.0, 1.0));
    if (shadingNormalMode == 1)
    {
        // v_reflectionFlatNormal is set by the GS to the true world-space face normal.
        return safeNormalizeGeom(v_reflectionFlatNormal, baseN);
    }
    return baseN;
}

// In flat shading mode use the provoking vertex position so that view and light
// directions are constant per face, preventing smooth shading gradients on large
// triangles at extreme zoom.
vec3 getFragPosition()
{
    return (shadingNormalMode == 1) ? v_positionFlat : v_position;
}

// ---- Texture & Normal utilities --------------------------------------------
float	samplePackedChannelValue(sampler2D tex, bool hasTexture, vec2 uv,
								 int channel, int invert, float scale, float bias,
								 float fallback);
vec3    getNormalFromMap(sampler2D map);
mat3    getTBNFromMap(sampler2D map);
vec3    calcBumpedNormal(sampler2D map, vec2 texCoord);
vec2    parallaxOcclusionMapping(vec2 texCoords, vec3 viewDir, sampler2D heightMap, float heightScale);
vec2	applyParallaxMapping(vec2 baseUV, sampler2D heightMap, float heightScale, bool enabled);
// ---- Core BRDF primitives (NDF * G * F) ------------------------------------
float   distributionGGX(vec3 N, vec3 H, float roughness);
float   geometrySchlickGGX(float NdotV, float roughness);
float   geometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3    fresnelSchlick(float cosTheta, vec3 F0);
vec3    fresnelSchlick(float cosTheta, vec3 F0, vec3 F90);
vec3    fresnelSchlickIOR(float cosTheta, float ior);

// ---- KHR Sheen BRDF --------------------------------------------------------
float	D_Charlie(float roughness, float NoH);
float   distributionCharlie(vec3 N, vec3 H, float roughness);
float   geometryCharlie(float NdotV, float roughness);
float	V_Sheen(float NdotL, float NdotV, float sheenRoughness);
float	lambdaSheen(float cosTheta, float alphaG);
float	lambdaSheenNumericHelper(float x, float alphaG);
float	max3(vec3 v);
vec3    calculateSheen(vec3 N, vec3 V, vec3 L, vec3 sheenColor, float sheenRoughness);
const float kSheenStrength = 1.0;

// ---- KHR Anisotropy --------------------------------------------------------
float	V_GGX_anisotropic(float NdotL, float NdotV, float BdotV, float TdotV, float TdotL, float BdotL, float at, float ab);
float	D_GGX_anisotropic(float NdotH, float TdotH, float BdotH, float at, float ab);
AnisotropyData decodeAnisotropyTexture(
	vec3 texelRGB,                           // Raw texture data
	float uniformStrength,                   // Uniform strength parameter
	float uniformRotation,                   // Uniform rotation in radians
	bool hasTexture
);

// ---- KHR Iridescence -------------------------------------------------------
vec3	evalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness, vec3 baseF0);
vec3	evalIridescence(float outsideIOR, float eta2, float cosTheta1, float thinFilmThickness, vec3 baseF0, vec3 baseF90);
vec3    computeIridescentFresnel(float cosTheta,
								 vec3 baseF0,
								 vec3 baseF90,
								 float iridescenceFactor,
								 float iridescenceIor,
								 float iridescenceThickness);

// ---- KHR Transmission & Volume ---------------------------------------------
vec3    calculateTransmissionKHR(vec3 normal, vec3 view, vec3 pointToLight, float alphaRoughness, vec3 baseColor, float ior);
vec3	calculateVolumeAttenuation(vec3 transmittedLight, float distance, float thickness, vec3 attenuationColor, float attenuationDistance);
float	applyIorToRoughness(float roughness, float ior);
vec3	getVolumeTransmissionRay(vec3 n, vec3 v, float thickness, float ior, mat4 modelMatrix);
vec3	getTransmissionSample(vec2 fragCoord, float roughness, float ior);
vec3	getIBLVolumeRefraction(vec3 n, vec3 v, float perceptualRoughness, vec3 baseColor,
							   vec3 position, mat4 modelMatrix, float ior, float thickness,
							   vec3 attenuationColor, float attenuationDistance);
vec3	getIBLVolumeRefractionPerChannel(vec3 n, vec3 v, float perceptualRoughness, vec3 baseColor,
                                         vec3 position, mat4 modelMatrix, vec3 iors, float thickness,
                                         vec3 attenuationColor, float attenuationDistance);

// ---- KHR Volume Scatter ----------------------------------------------------
vec3	multiToSingleScatter();
vec3    sampleCapturedSSSDiffuse(float attenuationDistance, vec3 diffuseColor);

// ---- Shadows ---------------------------------------------------------------
float   calculateShadowVariableKernel(vec4 fragPosLightSpace, vec3 fragPos, vec3 lightPos);

// ---- Background ------------------------------------------------------------
vec2    calculateBackgroundUV();
vec3    calculateBackgroundColor();

// ---- Tone mapping & colour space -------------------------------------------
vec3 uncharted2ToneMapping(vec3 color);
vec3 applyToneMapping(vec3 color);
vec3 sRGBToLinear(vec3 c);
vec3 linearTosRGB(vec3 c);
float sRGBSaturation(vec3 c);

// ---- Miscellaneous utilities -----------------------------------------------
float pickChannel(vec4 v, int ch, int invertFlag, float scale, float bias);
float sampleOpacityMap(vec2 uv);
float sampleFallbackOpacity(vec2 uv);
float readMaskChannel(vec4 texel, int channel);

// ---- KHR PBR path - setup & materials -------------------------------------
SurfaceFrame  buildSurfaceFrame(float side, vec2 normalUV, vec2 clearcoatNormalUV, vec3 lightDirection);
MaterialParams gatherMaterialParams();
vec3  computeDielectricF0(float ior, float specularFactor, vec3 specularColor, bool useSpecGloss, vec3 specGlossSpecular);
vec3  computeF90(float metallic, float specularFactor);
float computeVolumeScaleFactor();
float computeVolumeThickness(float thickness);
vec3  computeBaseColor(vec2 uv,
					   vec3 matBaseColor_linear,
					   sampler2D albedoTex,
					   bool hasAlbedoTex,
					   vec3 vertexColor_linear,
					   bool useVertexColor);

// ---- KHR PBR path - IBL helpers --------------------------------------------
vec3  transformNormalForIBL(vec3 normal);
vec3  toPrefilterDirection(vec3 v);
void  buildAnisotropyBasis(in SurfaceFrame frame, in MaterialParams params, out vec3 anisotropicT, out vec3 anisotropicB);
vec3  sampleAnisotropicSpecularIBL(in SurfaceFrame frame, in MaterialParams params);
float computeSheenScaling(float Ndot, float sheenRoughness);
float computeCharlieBRDF(float Ndot, float sheenRoughness);
vec3  computeIBLGGXFresnelFromSample(float NdotV, float roughness, vec2 f_ab, vec3 F0, float specularWeight);
vec3  computeAnisotropicSpecularLobe(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir);
vec3  calculateBackgroundColor();

// ---- KHR PBR path - punctual & direct lighting ----------------------------
void evaluatePunctualLight(in PunctualLight light, out vec3 lightDir, out vec3 lightIntensity);
void evaluateBaseDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor, out vec3 diffuseOut, out vec3 specularOut);
vec3 evaluateSheenDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor);
void evaluateBaseIBL(in SurfaceFrame frame, in MaterialParams params, out vec3 diffuseIBLOut, out vec3 specularIBLOut);
vec3 evaluateClearcoatDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor);
vec3 evaluateClearcoatIBL(in SurfaceFrame frame, in MaterialParams params);
vec3 evaluateSheenIBL(in SurfaceFrame frame, in MaterialParams params);

// ---- KHR PBR path - layer composition --------------------------------------
LayerContributions makeEmptyLayerContributions();
vec3 sampleMappedNormal(sampler2D map, vec2 texCoord, float normalScale, vec3 Ng, vec3 T, vec3 B);
vec3 composeBaseLayer(in MaterialParams params, in LayerContributions layers);
vec3 composeLayeredPBR(in SurfaceFrame frame, in MaterialParams params, in LayerContributions layers);

float floorRadius = floorSize * 0.5; // Adjust radius based on floor size
float fadeStart = floorRadius * 0.65;   // Start fading 
float fadeEnd = floorRadius * 1.025;     // Fully faded

vec2 groundPlaneCoords(vec3 worldPos)
{
    return (worldUpAxis == 1)
        ? (worldPos.xz - screenCenter.xz)
        : (worldPos.xy - screenCenter.xy);
}

float gridLineMask(vec2 planePos, float cellSize, float lineWidthPixels)
{
	vec2 scaled = planePos / max(cellSize, 1e-4);
	vec2 derivative = max(fwidth(scaled), vec2(1e-4));
	vec2 grid = abs(fract(scaled - 0.5) - 0.5) / derivative;
	float line = min(grid.x, grid.y);
	return 1.0 - clamp(line / max(lineWidthPixels, 1e-4), 0.0, 1.0);
}

vec4 computeGridOverlayColor()
{
	vec2 planePos = groundPlaneCoords(v_position);
	float sceneScale = max(groundReferenceSize, 1.0);
	float majorCell = max(sceneScale / 12.0, 0.25);
	float minorCell = majorCell / 10.0;

	float majorMask = gridLineMask(planePos, majorCell, 1.55);
	float minorMask = gridLineMask(planePos, minorCell, 1.20);

	float radialGridDistance = length(planePos);
	float farBlend = smoothstep(sceneScale * 0.18, sceneScale * 0.82, radialGridDistance);
	float minorVisibility = max(1.0 - farBlend, 0.22);

	float majorAlpha = majorMask * 0.92;
	float minorAlpha = minorMask * minorVisibility * 0.62;
	float axisXAlpha = gridLineMask(vec2(planePos.x, 0.0), majorCell, 2.0) * 0.55;
	float axisYAlpha = gridLineMask(vec2(0.0, planePos.y), majorCell, 2.0) * 0.55;

	vec3 baseColor = vec3(0.94);
	baseColor = mix(baseColor, vec3(0.35), clamp(minorAlpha, 0.0, 1.0));
	baseColor = mix(baseColor, vec3(0.0), clamp(majorAlpha, 0.0, 1.0));
	baseColor = mix(baseColor, vec3(0.92, 0.48, 0.40), clamp(axisXAlpha, 0.0, 1.0));
	baseColor = mix(baseColor, vec3(0.40, 0.64, 0.92), clamp(axisYAlpha, 0.0, 1.0));

	float alpha = max(majorAlpha, minorAlpha);
	alpha = max(alpha, axisXAlpha);
	alpha = max(alpha, axisYAlpha);
	alpha *= 0.95;

	return vec4(baseColor, alpha);
}

// ---- void main() ------------------------------------------------------------

void main()
{
	vec4 v_color_front;
	vec4 v_color_back;
	vec4 v_color;

	// Discard backfaces if not twoSided
	bool isFrontFacing = gl_FrontFacing;

	if (hasNegativeScale)
	{
		isFrontFacing = !isFrontFacing;  // Invert because negative scale reverses winding
	}

	if (!twoSided && !isFrontFacing && !floorRendering)
	{
		discard;
	}

	// Early discard for reflected pass beyond fade start
	if (isReflectedPass)
	{
		float distance = length(groundPlaneCoords(v_position));
		if (distance > fadeStart)
			discard;
	}

	// SSS capture pass: skip non-SSS surfaces entirely so only subsurface
	// fragments are written into the SSS FBO.
	if (sssCapture && !hasVolumeScattering)
		discard;

	// Choose rendering path - ADS vs PBR
	if (renderingMode == 0)
	{
		if (floorRendering)
		{
			// Floor keeps an analytical up vector, but transform it into view space so
			// ADS lighting stays in one consistent space.
			vec3 floorUpWorld = (worldUpAxis == 1) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
			vec3 floorUpNormal = normalize(mat3(viewMatrix) * floorUpWorld);
			v_color_front = shadeBlinnPhong(lightSource, lightModel, material, v_position, floorUpNormal);
			v_color_back  = v_color_front;
		}
		else
		{
			// Meshes stay in view space in ADS so the legacy CAD-style lighting,
			// bump mapping, hover highlight, and two-sided shading all agree.
			vec3 baseNormal = getUnsignedViewGeometryNormal();
			v_color_front = shadeBlinnPhong(lightSource, lightModel, material, v_position, baseNormal);
			v_color_back  = shadeBlinnPhong(lightSource, lightModel, material, v_position, -baseNormal);
		}
	}
	else
	{
		v_color_front = calculatePBRLighting(renderingMode, 1.0f);
		v_color_back = calculatePBRLighting(renderingMode, -1.0f);
	}

	// Two-sided coloring
	if (isFrontFacing)
	{
		v_color = v_color_front;
	}
	else
	{
		if (sectionActive) // lighten backfaces in section mode
			v_color = v_color_back + 0.15f;
		else
			v_color = v_color_back;
	}

	fragColor = v_color; // Start with default shaded color	

	// Display modes - this needs to be computed before the alpha
	if (displayMode == 1) // wireframe
	{
		fragColor = vec4(v_color.rgb, 0.75f); // semi-transparent shaded
	}
	else if (displayMode == 2) // wireshaded
	{
		fragColor = v_color;
		if (isWireframePass)
		{
			float brightness = dot(fragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
			vec3 overlayColor;
			if (brightness < 0.2)
			{
				overlayColor = fragColor.rgb + vec3(0.6);
			}
			else if (brightness > 0.8)
			{
				overlayColor = fragColor.rgb * 0.3;
			}
			else
			{
				overlayColor = brightness > 0.5 ? fragColor.rgb * 0.5 : fragColor.rgb + vec3(0.4);
			}
			overlayColor = clamp(overlayColor, 0.0, 1.0);
			fragColor = vec4(overlayColor, 1.0);
		}		
	}

	// UNIFIED BLEND MODE AWARE OPACITY CALCULATION
	// Works for both ADS and PBR pipelines with dynamic texture availability
	// Skip for floor rendering - it handles its own alpha
	float finalAlpha = fragColor.a; // Start with whatever alpha the rendering functions set

	if (!floorRendering) // Bypass alpha for floor
	{		
		if (blendMode == 0)
		{
			// OPAQUE: ignore alpha maps, always fully opaque
			finalAlpha = 1.0;
		}
		else if (blendMode == 1)
		{
			// MASK: cutout alpha test. Compute testAlpha from material scalar and textures.
			float testAlpha = opacity; // material scalar

			// Priority: dedicated opacity map > fallbacks
			if (hasOpacityMap)
			{
				float opVal = sampleOpacityMap(getOpacityUV());
				testAlpha *= opVal;
			}
			else
			{
				// fallback to albedo/diffuse alpha or legacy opacity texture
				float fallback = sampleFallbackOpacity(getAlbedoUV());
				testAlpha *= fallback;
			}

			// Alpha test
			if (testAlpha < alphaThreshold) discard;
			finalAlpha = 1.0; // cutout either opaque or discarded
		}
		else
		{ // blendMode == 2 (BLEND) - standard transparency
			 // Compute finalAlpha as material scalar * dedicated opacity map * fallback alpha
			float alphaVal = opacity;

			if (hasOpacityMap)
			{
				alphaVal *= sampleOpacityMap(getOpacityUV());
			}
			else
			{
				alphaVal *= sampleFallbackOpacity(getAlbedoUV());
			}

			// clamp and optionally apply any opacityScale/bias global (if desired)
			finalAlpha = clamp(alphaVal, 0.0, 1.0);

			// Note: do NOT discard here. Transparent fragments are blended.
			// Depth write/disabling and render-order must be handled on the GL side.
		}
	}

	// Apply the final alpha (outside floorRendering block)
	fragColor.a = finalAlpha;

	// Apply vertex color alpha modulation
	if (hasVertexColors)
		fragColor.a *= v_color.a;

	// Premultiply for blending (non-floor; floor path already premultiplies)
	if (!floorRendering)
	{
		fragColor.rgb *= finalAlpha;
	}

	// ---- Debug channel isolation -----------------------------------------------
	// TextureDebugPanel dropdown: replace fragColor with a raw channel value.
	// IDs 1-9  : geometry / vertex channels (no ADS/PBR branching needed).
	// IDs 10-17: core texture channels (branched per rendering path).
	// IDs 18+  : extension texture channels (PBR path only).
	// Skipped for the floor mesh.
	if (debugChannelOutput != 0 && !floorRendering)
	{
		float savedAlpha = fragColor.a;  // preserve before overwriting

		// Default: white-grey checkerboard for channels absent on this mesh.
		// Uses screen-space coordinates so the pattern is a fixed pixel size
		// regardless of UV layout (matches Khronos missing-data style).
		float _checker = mod(floor(gl_FragCoord.x / 16.0) + floor(gl_FragCoord.y / 16.0), 2.0);
		vec3  iso = mix(vec3(1.0), vec3(0.65), _checker); // white / light-grey squares

		// ---- Geometry / vertex channels (IDs 1-9) ----
		if (debugChannelOutput == 1)     // Texture Coordinates 0  (R=U, G=V)
			// Assimp flips V for glTF (top-origin → bottom-origin).  Re-flip here
			// so the display matches Khronos and DCC tool UV conventions (V=0 at top).
			iso = vec3(v_texCoord0.x, 1.0 - v_texCoord0.y, 0.0);

		else if (debugChannelOutput == 2) // Texture Coordinates 1  (R=U, G=V)
			iso = vec3(v_texCoord1.x, 1.0 - v_texCoord1.y, 0.0);

		else if (debugChannelOutput == 3) // Geometry Normal  (world-space, remapped)
			iso = getUnsignedViewGeometryNormal() * 0.5 + 0.5;

		else if (debugChannelOutput == 4) // Geometry Tangent
		{
			bool hasTangentData = length(v_tangent) > 0.01;
			iso = hasTangentData ? normalize(v_tangent) * 0.5 + 0.5 : vec3(0.5);
		}

		else if (debugChannelOutput == 5) // Geometry Bitangent
		{
			bool hasBitangentData = length(v_bitangent) > 0.01;
			iso = hasBitangentData ? normalize(v_bitangent) * 0.5 + 0.5 : vec3(0.5);
		}

		else if (debugChannelOutput == 6) // Geometry Tangent W (handedness: 0=negative, 1=positive)
		{
			bool hasTangentData = length(v_tangent) > 0.01;
			if (hasTangentData)
			{
				vec3 N = getUnsignedViewGeometryNormal();
				vec3 T = normalize(v_tangent   - dot(v_tangent,   N) * N);
				vec3 B = normalize(v_bitangent - dot(v_bitangent, N) * N);
				float w = sign(dot(cross(T, B), N)); // +1 or -1
				iso = vec3((w + 1.0) * 0.5);         // remap to [0,1]
			}
			// else keep mid-grey sentinel (no tangent data)
		}

		else if (debugChannelOutput == 7) // Shading Normal (TBN-transformed, what lighting uses)
		{
			vec3 shadingN;
			if (renderingMode == 0) // ADS
				shadingN = (shadingNormalMode != 1 && hasNormalTexture)
				    ? calcBumpedNormal(texture_normal, getNormalTextureUV())
				    : getSignedViewGeometryNormal();
			else // PBR
				shadingN = (shadingNormalMode != 1 && hasNormalMap)
				    ? calcBumpedNormal(normalMap, getNormalUV())
				    : getSignedViewGeometryNormal();
			iso = shadingN * 0.5 + 0.5;
		}

		else if (debugChannelOutput == 8) // Alpha (final opacity value)
			iso = vec3(savedAlpha);

		else if (debugChannelOutput == 9) // Vertex Color
		{
			if (hasVertexColors)
				iso = v_rawVertexColor.rgb;
			// else keep checkerboard sentinel
		}

		// ---- Texture channels (IDs 10-17, branched per rendering path) ----
		else if (renderingMode == 0) // ADS path
		{
			if      (debugChannelOutput == 10 && hasDiffuseTexture)
				iso = texture(texture_diffuse, getDiffuseTextureUV()).rgb;
			else if (debugChannelOutput == 12 && hasEmissiveTexture)
				iso = texture(texture_emissive, getEmissiveTextureUV()).rgb;
			else if (debugChannelOutput == 13 && hasNormalTexture)
				iso = texture(texture_normal, getNormalTextureUV()).rgb;
			else if (debugChannelOutput == 14 && hasHeightTexture)
				{ float h = texture(texture_height, getHeightUV()).r; iso = vec3(h); }
			else if (debugChannelOutput == 15 && hasOpacityTexture)
				{ float a = texture(texture_opacity, getOpacityTextureUV()).r; iso = vec3(a); }
		}
		else // PBR path
		{
			// Gather the fully-resolved material parameters (texture × scalar factor).
			// Mirrors Khronos viewer behaviour: every channel shows the combined value
			// the lighting model actually uses, so a material with only a metallic scalar
			// (no metallic texture) shows that scalar instead of a checkerboard.
			// Normal/height maps are excluded — they have no meaningful scalar fallback.
			MaterialParams dbg = gatherMaterialParams();

			if      (debugChannelOutput == 10)  // Albedo/Base Color (factor × tex × vtxColor) — sRGB like Khronos
				iso = linearTosRGB(dbg.baseColor);
			else if (debugChannelOutput == 11)  // Metallic (metallicFactor × tex, or just factor)
				iso = vec3(dbg.metallic);
			else if (debugChannelOutput == 12)  // Emissive (emissionFactor × tex × emissiveStrength) — sRGB like Khronos
				iso = linearTosRGB(dbg.emissive);
			else if (debugChannelOutput == 13 && hasNormalMap)
				iso = texture(normalMap, getNormalUV()).rgb;
			else if (debugChannelOutput == 14 && hasHeightMap)
				{ float h = texture(heightMap, getHeightUV()).r; iso = vec3(h); }
			else if (debugChannelOutput == 15 && hasOpacityMap)
				{ float a = texture(opacityMap, getOpacityUV()).r; iso = vec3(a); }
			else if (debugChannelOutput == 16)  // Roughness (roughnessFactor × tex, or just factor)
				iso = vec3(dbg.roughness);
			else if (debugChannelOutput == 17)  // AO (occlusionStrength × tex, or 1.0 if absent)
				iso = vec3(dbg.ambientOcclusion);
			// ---- Extension channels (IDs 18-30, 32, 34-35) ----
			// Each branch is gated by its extXxx flag so that absent extensions
			// fall through to the checkerboard sentinel, matching Khronos behaviour.
			else if (debugChannelOutput == 18 && extClearcoat)
				iso = vec3(dbg.clearcoat);
			else if (debugChannelOutput == 19 && extClearcoat)
				iso = vec3(dbg.clearcoatRoughness);
			else if (debugChannelOutput == 20 && extClearcoat && hasClearcoatNormalMap)
				iso = texture(clearcoatNormalMap, getClearcoatNormalUV()).rgb;
			else if (debugChannelOutput == 21 && extSpecular)
				iso = vec3(dbg.specularFactor);
			else if (debugChannelOutput == 22 && extSpecular)
				iso = dbg.specularColor;
			else if (debugChannelOutput == 23 && extAnisotropy)
				iso = vec3(dbg.anisotropyStrength);
			else if (debugChannelOutput == 32 && extAnisotropy)
			{
				if (hasAnisotropyMap)
					iso = vec3(texture(anisotropyMap, getAnisotropyUV()).xy, 0.0);
				else
					iso = vec3(1.0, 0.5, 0.0); // default +X direction (1,0) remapped to [0,1]
			}
			else if (debugChannelOutput == 24 && extIridescence)
				iso = vec3(dbg.iridescenceFactor);
			else if (debugChannelOutput == 25 && extIridescence)
				iso = vec3(dbg.iridescenceThickness / 1200.0);
			else if (debugChannelOutput == 26 && extSheen)
				iso = dbg.sheenColor;
			else if (debugChannelOutput == 27 && extSheen)
				iso = vec3(dbg.sheenRoughness);
			else if (debugChannelOutput == 28 && extTransmission)
				iso = vec3(dbg.transmission);
			else if (debugChannelOutput == 30 && extVolume)
				iso = vec3(dbg.thickness / max(pbrLighting.thicknessFactor, 0.001));
			else if (debugChannelOutput == 34 && extDiffuseTrans)
				iso = vec3(dbg.diffuseTransmissionFactor);
			else if (debugChannelOutput == 35 && extDiffuseTrans)
				iso = linearTosRGB(dbg.diffuseTransmissionColor);
		}
		fragColor = vec4(iso, 1.0);
	}

	// Selection highlighting — skipped in single-channel isolation mode so raw
	// channel values are not tinted.
	if (debugChannelOutput == 0 && selected && selectionHighlighting) // with glow
	{
		// Compute lighting
		vec3 norm = getSignedViewGeometryNormal();
		vec3 lightDir = normalize(lightSource.position);
		float diff = max(dot(norm, lightDir), 0.0);

		vec3 viewDir = normalize(cameraDir);
		vec3 reflectDir = reflect(-lightDir, norm);
		float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);

		// Base color from fragColor
		vec3 baseColor = fragColor.rgb;

		// Lighten the color with lighting + subtle spec
		vec3 lightened = baseColor + vec3(0.3) * diff + vec3(0.2) * spec + vec3(0.1);
		lightened = clamp(lightened, 0.0, 1.0);

		// Apply a subtle transparency
		float alpha = fragColor.a * 0.99;

		// Add glow effect
		vec3 glowColor = lightened * 1.2; // Make the glow a bit brighter
		glowColor = clamp(glowColor, 0.0, 1.0);

		// Mix base color with the glow
		vec3 finalColor = mix(lightened, glowColor, 0.5); // blend base and glow color

		fragColor = vec4(finalColor, alpha);
	}

	// Hover highlighting (visual preview, non-destructive)
	// Only applied if not already selected (selection takes priority).
	// Also skipped in single-channel isolation mode.
	if (debugChannelOutput == 0 && hovered && hoverHighlighting && !selected)
	{
		// Compute lighting (similar to selection but more subtle)
		vec3 norm = getSignedViewGeometryNormal();
		vec3 lightDir = normalize(lightSource.position);
		float diff = max(dot(norm, lightDir), 0.0);

		vec3 viewDir = normalize(cameraDir);
		vec3 reflectDir = reflect(-lightDir, norm);
		float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);

		// Base color from fragColor
		vec3 baseColor = fragColor.rgb;

		// Subtle brightening (less aggressive than selection - use 50% of selection intensity)
		vec3 brightened = baseColor + vec3(0.15) * diff + vec3(0.1) * spec + vec3(0.05);
		brightened = clamp(brightened, 0.0, 1.0);

		// Apply subtle glow (less pronounced than selection)
		vec3 glowColor = brightened * 1.1;  // Minimal glow boost
		glowColor = clamp(glowColor, 0.0, 1.0);

		// Mix with slight glow (30% blend vs selection's 50%)
		vec3 finalColor = mix(brightened, glowColor, 0.3);

		fragColor = vec4(finalColor, fragColor.a);
	}

	// Finally, handle floor rendering fade-out and background blending
	if (floorRendering)
	{
		if (groundMode == 2)
		{
			fragColor = computeGridOverlayColor();
		}
		else if (!isReflectedPass && floorTextureEnabled)
			fragColor = v_color * texture(texture_diffuse, getDiffuseTextureUV());
		// Compute distance-based blending factor
		float distance = length(groundPlaneCoords(v_position));

		// Set fade parameters first, before any calculations
		// Early discard for pixels beyond fade range
		if (distance > fadeEnd)
			discard;

		// Calculate fade factor
		float fadeFactor = smoothstep(fadeStart, fadeEnd, distance);
		if (fadeFactor >= 1.0)
			discard;
		// Blend floor color with the background gradient
		// View-angle modulation: reduce background mix when looking straight down.
		// Use the analytical world-space floor normal (always pointing down along the up-axis)
		// so the angle is correct regardless of camera rotation — avoids mixed view/world space.
		// NdotV_main ≈ 1 when camera looks straight down; ≈ 0 at grazing.
		vec3 N_main = (worldUpAxis == 1) ? vec3(0.0, -1.0, 0.0) : vec3(0.0, 0.0, -1.0);
		vec3 V_main = normalize(cameraDir);
		float NdotV_main = abs(dot(N_main, V_main));

		// at top-down (NdotV~1) → angleMod = 0.25  (floor more opaque)
		// at grazing  (NdotV~0) → angleMod = 1.0   (floor fades into background)
		float angleMod = mix(1.0, 0.25, NdotV_main);
		float bgMix = fadeFactor * angleMod;

		// Get background color
		vec3 backgroundColor = vec3(1.0);
		if (skyBoxEnabled)
		{
			vec3 N = getUnsignedWorldGeometryNormal();
			vec3 V = normalize(cameraDir);

			// Refract ray into environment
			float ior = 1.5; // IOR of glass
			vec3 R = refract(V, N, 1.0 / ior);

			// Sample environment
			vec3 backgroundColor = texture(envMap, R).rgb;
		}
		else if (!isReflectedPass && floorTextureEnabled)
		{
			// Interpolate background gradient color			
			backgroundColor = texture(texture_diffuse, getDiffuseTextureUV()).rgb;			
		}

		// Blend floor color with background gradient
		fragColor.rgb = mix(fragColor.rgb, backgroundColor, clamp(bgMix, 0.0, 1.0));
		fragColor.a *= (1.0 - fadeFactor) * opacity;
	}
}

// ---- Guard utilities --------------------------------------------------------

// ============================================================================
// GUARD HELPERS: Prevent texture nullification by zero factors
// ============================================================================

// Guard a scalar factor (e.g., roughness, metallic, sheen roughness)
// Returns the factor if non-zero, otherwise returns 1.0 (neutral)
float guardFactorScalar(float factor, float threshold)
{
    return factor < threshold ? 1.0 : factor;
}

// Guard a color/vector factor (e.g., sheenColor, specularColor)
// Returns the factor if non-black, otherwise returns white (neutral)
vec3 guardFactorColor(vec3 factor, float threshold)
{
    return length(factor) < threshold ? vec3(1.0) : factor;
}

// ---- Texture UV Accessors ---------------------------------------------------

// ========== CORE TEXTURE TRANSFORM FUNCTION ==========
vec2 getTransformedUV(int texCoordIndex, TextureTransform transform)
{
	// Step 1: Select the appropriate base UV coordinate set
	vec2 uv;
	switch (texCoordIndex)
	{
	case 1: uv = v_texCoord1; break;
	case 2: uv = v_texCoord2; break;
	case 3: uv = v_texCoord3; break;
	default: uv = v_texCoord0; break; // case 0 and fallback
	}

	// glTF 2.0 spec defines UV coordinates with origin at upper-left corner (0,0).
	// KHR_texture_transform operates in this image-space coordinate system.
	// Since images are not Y-flipped during load (matching KHR Sample Viewer),
	// we flip UV.y here and negate rotation to maintain spec compliance.	
	// Non-traditional approach: No image Y-flip at load (per KHR Sample Viewer).
	// Coordinate system compensation applied in shader:
	//   - UV.y flip: glTF operates in image-space (top-left origin)
	//   - Rotation negation: glTF rotates around origin (0,0), not image center
	// This matches Assimp's documented KHR_texture_transform coordinate conversion.
	// Reference: assimp/code/AssetLib/glTF2/glTF2Importer.cpp
	//   "transform.mRotation = -prop.TextureTransformExt_t.rotation; // must be negated"
	uv = vec2(uv.x, 1.0 - uv.y); 
	float angle = -transform.rotation;

	// Step 2: Apply KHR_texture_transform
	// Order: scale -> rotation -> offset
	
	//Scale
	uv = uv * transform.scale;

	// Apply rotation matrix	
	float cosR = cos(angle);
	float sinR = sin(angle);
	mat2 rotMat = mat2(cosR, sinR, -sinR, cosR);
	uv = rotMat * uv;

	// Apply offset
	uv = uv + transform.offset;

	return uv;
}

// ========== CONVENIENCE FUNCTIONS FOR EACH TEXTURE TYPE ==========
vec2 getAlbedoUV()
{
	return getTransformedUV(albedoTexTransform.texCoordIndex, albedoTexTransform);
}

vec2 getMetallicUV()
{
	return getTransformedUV(metallicTexTransform.texCoordIndex, metallicTexTransform);
}

vec2 getRoughnessUV()
{
	return getTransformedUV(roughnessTexTransform.texCoordIndex, roughnessTexTransform);
}

vec2 getNormalUV()
{
	return getTransformedUV(normalTexTransform.texCoordIndex, normalTexTransform);
}

vec2 getHeightUV()
{
	return getTransformedUV(heightTexTransform.texCoordIndex, heightTexTransform);
}

vec2 getAOUV()
{
	return getTransformedUV(aoTexTransform.texCoordIndex, aoTexTransform);
}

vec2 getOpacityUV()
{
	return getTransformedUV(opacityTexTransform.texCoordIndex, opacityTexTransform);
}

vec2 getEmissiveUV()
{
	return getTransformedUV(emissiveTexTransform.texCoordIndex, emissiveTexTransform);
}

vec2 getTransmissionUV()
{
	return getTransformedUV(transmissionTexTransform.texCoordIndex, transmissionTexTransform);
}

vec2 getIORUV()
{
	return getTransformedUV(iorTexTransform.texCoordIndex, iorTexTransform);
}

vec2 getSheenColorUV()
{
	return getTransformedUV(sheenColorTexTransform.texCoordIndex, sheenColorTexTransform);
}

vec2 getSheenRoughnessUV()
{
	return getTransformedUV(sheenRoughnessTexTransform.texCoordIndex, sheenRoughnessTexTransform);
}

vec2 getClearcoatUV()
{
	return getTransformedUV(clearcoatTexTransform.texCoordIndex, clearcoatTexTransform);
}

vec2 getClearcoatRoughnessUV()
{
	return getTransformedUV(clearcoatRoughnessTexTransform.texCoordIndex, clearcoatRoughnessTexTransform);
}

vec2 getClearcoatNormalUV()
{
	return getTransformedUV(clearcoatNormalTexTransform.texCoordIndex, clearcoatNormalTexTransform);
}

vec2 getSpecularFactorUV()
{
	return getTransformedUV(specularFactorTexTransform.texCoordIndex, specularFactorTexTransform);
}

vec2 getSpecularColorUV()
{
	return getTransformedUV(specularColorTexTransform.texCoordIndex, specularColorTexTransform);
}

vec2 getAnisotropyUV()
{
	return getTransformedUV(anisotropyTexTransform.texCoordIndex, anisotropyTexTransform);
}

vec2 getIridescenceUV()
{
	return getTransformedUV(iridescenceTexTransform.texCoordIndex, iridescenceTexTransform);
}

vec2 getIridescenceThicknessUV()
{
	return getTransformedUV(iridescenceThicknessTexTransform.texCoordIndex, iridescenceThicknessTexTransform);
}

vec2 getThicknessUV()
{
	return getTransformedUV(thicknessTexTransform.texCoordIndex, thicknessTexTransform);
}

vec2 getDiffuseTransmissionUV()
{
	return getTransformedUV(diffuseTransmissionTexTransform.texCoordIndex, diffuseTransmissionTexTransform);
}

vec2 getDiffuseTransmissionColorUV()
{
	return getTransformedUV(diffuseTransmissionColorTexTransform.texCoordIndex, diffuseTransmissionColorTexTransform);
}

vec2 getDiffuseUV()
{
	return getTransformedUV(diffuseTexTransform.texCoordIndex, diffuseTexTransform);
}

vec2 getSpecularGlossinessUV()
{
	return getTransformedUV(specularGlossinessTexTransform.texCoordIndex, specularGlossinessTexTransform);
}



// Legacy ADS texture UV functions
vec2 getDiffuseTextureUV()
{
	return getTransformedUV(diffuseTextureTransform.texCoordIndex, diffuseTextureTransform);
}

vec2 getSpecularTextureUV()
{
	return getTransformedUV(specularTextureTransform.texCoordIndex, specularTextureTransform);
}

vec2 getEmissiveTextureUV()
{
	return getTransformedUV(emissiveTextureTransform.texCoordIndex, emissiveTextureTransform);
}

vec2 getNormalTextureUV()
{
	return getTransformedUV(normalTextureTransform.texCoordIndex, normalTextureTransform);
}

vec2 getHeightTextureUV()
{
	return getTransformedUV(heightTextureTransform.texCoordIndex, heightTextureTransform);
}

vec2 getOpacityTextureUV()
{
	return getTransformedUV(opacityTextureTransform.texCoordIndex, opacityTextureTransform);
}

// ---- Texture & Normal utilities ---------------------------------------------

float samplePackedChannelValue(sampler2D tex, bool hasTexture, vec2 uv,
	int channel, int invert, float scale, float bias,
	float fallback)
{
	if (!hasTexture) return fallback;

	vec4 texel = texture(tex, uv);
	float val;

	if (channel >= 0)
	{
		val = pickChannel(texel, channel, invert, scale, bias);
	}
	else
	{
		val = texel.r * scale + bias;
		if (invert != 0) val = 1.0 - val;
	}

	return clamp(val, 0.0, 1.0);
}

// Parallax mapping function
vec2 applyParallaxMapping(vec2 baseUV, sampler2D heightMap, float heightScale, bool enabled)
{
	if (!enabled) return baseUV;

	// Build TBN matrix
	vec3 n = getUnsignedViewGeometryNormal();
	vec3 t = normalize(v_tangent - dot(v_tangent, n) * n);
	vec3 b = normalize(cross(n, t));
	mat3 TBN = mat3(t, b, n);

	// Transform view direction to tangent space
	vec3 viewDirWorld = normalize(cameraPos - v_position);
	vec3 viewDirTangent = TBN * viewDirWorld;

	// Apply parallax mapping
	vec2 parallaxUV = parallaxOcclusionMapping(baseUV, viewDirTangent, heightMap, heightScale);

	// Clamp to UV bounds
	if (parallaxUV.x < 0.0 || parallaxUV.x > 1.0 ||
		parallaxUV.y < 0.0 || parallaxUV.y > 1.0)
		parallaxUV = baseUV;

	return parallaxUV;
}

// Function for parallax mapping to simulate depth displacement
vec2 parallaxOcclusionMapping(vec2 texCoords, vec3 viewDir, sampler2D heightMap, float heightScale)
{
	// Number of layers varies with angle
	const float minLayers = 8.0;
	const float maxLayers = 32.0;
	float numLayers = mix(maxLayers, minLayers,
		abs(dot(vec3(0.0, 0.0, 1.0), normalize(viewDir))));

	// Calculate layer depth and step size
	float layerDepth = 1.0 / numLayers;
	float currentLayerDepth = 0.0;

	// Shift per step (Z-up -> use xz)
	vec2 P = viewDir.xz * heightScale;
	vec2 deltaTexCoords = P / numLayers;

	// Start at original texcoords
	vec2 currentTexCoords = texCoords;
	float currentDepthMapValue = texture(heightMap, currentTexCoords).r;

	// Iteratively march
	while (currentLayerDepth < currentDepthMapValue)
	{
		currentTexCoords -= deltaTexCoords;
		currentDepthMapValue = texture(heightMap, currentTexCoords).r;
		currentLayerDepth += layerDepth;
	}

	// Interpolate between last two steps for smoothness
	vec2 prevTexCoords = currentTexCoords + deltaTexCoords;

	float afterDepth = currentDepthMapValue - currentLayerDepth;
	float beforeDepth = texture(heightMap, prevTexCoords).r - (currentLayerDepth - layerDepth);

	float weight = afterDepth / (afterDepth - beforeDepth);

	return prevTexCoords * weight + currentTexCoords * (1.0 - weight);
}

// ----------------------------------------------------------------------------
// Easy trick to get tangent-normals to world-space to keep PBR code simplified.
vec3 getNormalFromMap(sampler2D map)
{
	vec3 tangentNormal = texture(map, getNormalUV()).xyz * 2.0 - 1.0;

	vec3 Q1 = dFdx(v_position);
	vec3 Q2 = dFdy(v_position);
	vec2 st1 = dFdx(getNormalUV());
	vec2 st2 = dFdy(getNormalUV());

	vec3 N = getUnsignedViewGeometryNormal();
	vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

mat3 getTBNFromMap(sampler2D map)
{
	vec3 tangentNormal = texture(map, getNormalUV()).xyz * 2.0 - 1.0;

	vec3 Q1 = dFdx(v_position);
	vec3 Q2 = dFdy(v_position);
	vec2 st1 = dFdx(getNormalUV());
	vec2 st2 = dFdy(getNormalUV());

	vec3 N = getUnsignedViewGeometryNormal();
	vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return TBN;
}

// http://ogldev.atspace.co.uk/www/tutorial26/tutorial26.html
// Robust calcBumpedNormal: uses provided mesh tangent & bitangent, preserves handedness
vec3 calcBumpedNormal(sampler2D map, vec2 texCoord)
{
    // base geometric normal (world space)
    vec3 N = getUnsignedViewGeometryNormal();
    
    // Check if we have valid tangent data
    bool hasTangents = (length(v_tangent) > 0.01);
    
    vec3 T, B;
    
    if (hasTangents)
    {
        // Use mesh-provided tangent and bitangent
        // Make tangent orthogonal to normal
        T = normalize(v_tangent - dot(v_tangent, N) * N);
        
        // Prefer using the provided bitangent (v_bitangent) instead of computing cross(N, T)
        // but orthogonalize it too
        B = normalize(v_bitangent - dot(v_bitangent, N) * N);
        
        // Ensure T, B, N form a right-handed basis; if not, flip B
        float handedness = sign(dot(cross(T, B), N));
        if (handedness < 0.0)
        {
            B = -B;
        }
    }
    else
    {
        // FALLBACK: Compute tangent space from screen-space derivatives
        // This is the same technique as getNormalFromMap
        vec3 Q1 = dFdx(v_position);
        vec3 Q2 = dFdy(v_position);
        vec2 st1 = dFdx(texCoord);
        vec2 st2 = dFdy(texCoord);
        
        T = normalize(Q1 * st2.t - Q2 * st1.t);
        B = -normalize(cross(N, T));
    }
    
    mat3 TBN = mat3(T, B, N);
    
    vec3 bumpMapNormal = texture(map, texCoord).rgb;
    bumpMapNormal = bumpMapNormal * 2.0 - 1.0;
    
    vec3 Nw = normalize(TBN * bumpMapNormal);
    return Nw;
}

// ---- Core BRDF primitives (NDF * G * F) -------------------------------------

// ----------------------------------------------------------------------------
// Advanced PBR Functions

// IOR-based Fresnel calculation
// Energy-conserving mix for iridescent dielectric surfaces.
// Matches Khronos glTF-Sample-Viewer functions.glsl rgb_mix().
// Iridescent thin-film Fresnel varies per channel (e.g. R=0.9, G=0.1, B=0.8).
// Plain mix() lets low-Fresnel channels retain most of the base (transmitted
// light), inflating overall brightness. rgb_mix reduces the base by the MAX
// channel Fresnel uniformly, so no channel retains more base than the most-
// reflective channel allows, while per-channel specular coloring is preserved.
vec3 rgb_mix(vec3 base, vec3 layer, vec3 rgb_alpha)
{
    float rgb_alpha_max = max(rgb_alpha.r, max(rgb_alpha.g, rgb_alpha.b));
    return (1.0 - rgb_alpha_max) * base + rgb_alpha * layer;
}

vec3 fresnelSchlickIOR(float cosTheta, float ior)
{
	float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
	return vec3(f0) + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

// ----------------------------------------------------------------------------
float distributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float nom = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return nom / max(denom, 0.001); // prevent divide by zero for roughness=0.0 and NdotH=1.0
}
// ----------------------------------------------------------------------------
float geometrySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}
// ----------------------------------------------------------------------------
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = geometrySchlickGGX(NdotV, roughness);
	float ggx1 = geometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return fresnelSchlick(cosTheta, F0, vec3(1.0));
}

vec3 fresnelSchlick(float cosTheta, vec3 F0, vec3 F90)
{
	return F0 + (F90 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---- KHR Sheen BRDF ---------------------------------------------------------

// Charlie distribution for sheen (fabric-like materials)
float distributionCharlie(vec3 N, vec3 H, float roughness)
{
	float alpha = roughness * roughness;
	float invAlpha = 1.0 / alpha;
	float cos2h = dot(N, H) * dot(N, H);
	float sin2h = max(1.0 - cos2h, 0.0078125); // 2^(-7), so sin2h is always > 0
	return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

// Charlie geometry function for sheen
float geometryCharlie(float NdotV, float roughness)
{
	float alpha = roughness * roughness;
	float sinTheta = sqrt(1.0 - NdotV * NdotV);
	return NdotV / (NdotV + alpha * sinTheta);
}

// Charlie NDF for sheen direct lighting.
// Matches Khronos brdf.glsl: alphaG = roughness * roughness (squared).
float D_Charlie(float roughness, float NoH)
{
	// Khronos-compliant: square roughness to get alphaG, matching brdf.glsl and sheenELUT.r baking.
	// float alphaG = roughness * roughness; invR = 1/alphaG (same as Khronos runtime D_Charlie).
	float alphaG = max(roughness * roughness, 0.000001);
	float invAlpha = 1.0 / alphaG;
	float sin2h = max(1.0 - NoH * NoH, 0.0078125);
	return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

// ============================================================================
// KHR_materials_sheen: Visibility Function with lambdaSheen Approximation
// ============================================================================

float lambdaSheenNumericHelper(float x, float alphaG)
{
	float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
	float a = mix(21.5473, 25.3245, oneMinusAlphaSq);
	float b = mix(3.82987, 3.32435, oneMinusAlphaSq);
	float c = mix(0.19823, 0.16801, oneMinusAlphaSq);
	float d = mix(-1.97760, -1.27393, oneMinusAlphaSq);
	float e = mix(-4.32054, -4.85967, oneMinusAlphaSq);
	return a / (1.0 + b * pow(x, c)) + d * x + e;
}


float lambdaSheen(float cosTheta, float alphaG)
{
	if (abs(cosTheta) < 0.5)
	{
		return exp(lambdaSheenNumericHelper(cosTheta, alphaG));
	}
	else
	{
		return exp(2.0 * lambdaSheenNumericHelper(0.5, alphaG) -
			lambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
	}
}

float V_Sheen(float NdotL, float NdotV, float sheenRoughness)
{
	sheenRoughness = max(sheenRoughness, 0.000001);
	// Khronos-compliant: square roughness to get alphaG, matching brdf.glsl V_Sheen.
	// This ensures direct sheen energy matches what sheenELUT.r represents.
	float alphaG = sheenRoughness * sheenRoughness;

	return clamp(1.0 / ((1.0 + lambdaSheen(NdotV, alphaG) +
		lambdaSheen(NdotL, alphaG)) *
		(4.0 * NdotV * NdotL)), 0.0, 1.0);
}

// ============================================================================
// Utility Functions
// ============================================================================

float max3(vec3 v)
{
	return max(max(v.x, v.y), v.z);
}

// ============================================================================
// KHR-compliant direct sheen calculation
// Formula: f_sheen = D_charlie(alpha, h) * V_sheen(NdotL, NdotV, alpha) * sheenColor
// sheenColor is already the complete color contribution (no metallic interaction)
// ============================================================================
vec3 calculateSheen(vec3 N, vec3 V, vec3 L, vec3 sheenColor, float sheenRoughness)
{
	vec3 H = normalize(V + L);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float NdotV = clamp(dot(N, V), 0.0, 1.0);
	float NdotH = clamp(dot(N, H), 0.0, 1.0);

	// Early exit for backfacing
	if (NdotL <= 0.0 || NdotV <= 0.0)
		return vec3(0.0);

	// Clamp roughness to safe range
	float sheenRoughFinal = clamp(sheenRoughness, 0.000001, 1.0);

	// D_Charlie distribution
	float D = D_Charlie(sheenRoughFinal, NdotH);

	// V_Sheen visibility (KHR-compliant, handles grazing angles correctly)
	float V_sheen = V_Sheen(NdotL, NdotV, sheenRoughFinal);

	// Sheen BRDF = D * V * sheenColor
	// (No Fresnel term - sheen color is already the full diffuse contribution)
	vec3 sheenBRDF = sheenColor * D * V_sheen;

	// Return: includes NdotL weighting for proper irradiance
	return sheenBRDF * NdotL;
}

// ---- KHR Anisotropy ---------------------------------------------------------

// Anisotropic visibility/masking function (Khronos spec line 174-181)
float V_GGX_anisotropic(float NdotL, float NdotV, float BdotV, float TdotV,
	float TdotL, float BdotL, float at, float ab)
{
	float GGXV = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
	float GGXL = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
	float v = 0.5 / (GGXV + GGXL);
	return clamp(v, 0.0, 1.0);
}

// Anisotropic GGX normal distribution (Khronos spec line 166-172)
float D_GGX_anisotropic(float NdotH, float TdotH, float BdotH, float at, float ab)
{
	const float PI = 3.141592653589793;
	float a2 = at * ab;
	vec3 f = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
	float w2 = a2 / dot(f, f);
	return a2 * w2 * w2 / PI;
}

AnisotropyData decodeAnisotropyTexture(
	vec3 texelRGB,                           // Raw texture data
	float uniformStrength,                   // Uniform strength parameter
	float uniformRotation,                   // Uniform rotation in radians
	bool hasTexture
)
{
	AnisotropyData result;

	if (!hasTexture)
	{
		// Default: direction (1,0) = +X axis, full strength
		result.direction = vec2(1.0, 0.0);
		result.strength = uniformStrength;
		result.rotation = uniformRotation;
		return result;
	}

	// ====== SPEC-COMPLIANT TEXTURE DECODING (Line 82) ======
	// Red [0,1] -> X [-1,1], Green [0,1] -> Y [-1,1], Blue = strength [0,1]

	result.direction = texelRGB.rg * 2.0 - 1.0;  // [0,1] -> [-1,1]
	float directionLength = length(result.direction);
	if (directionLength < 0.0001)
	{
		// Neutral texels should not generate unstable directions/angles.
		// Fall back to the canonical +X direction and let the uniform rotation drive it.
		result.direction = vec2(1.0, 0.0);
	}
	else
	{
		result.direction /= directionLength; // Unit vector in tangent space
	}

	// Blue channel is strength, multiply by uniform
	result.strength = texelRGB.b * uniformStrength;
	result.strength = clamp(result.strength, 0.0, 1.0);

	// Direction rotation: apply uniform rotation to texture direction
	// (Spec line 131-132: rotate the direction vector by the rotation matrix)
	float c = cos(uniformRotation);
	float s = sin(uniformRotation);
	vec2 rotated = vec2(
		c * result.direction.x - s * result.direction.y,
		s * result.direction.x + c * result.direction.y
	);

	// Convert 2D rotated direction to rotation angle for T/B rotation
	result.rotation = atan(rotated.y, rotated.x);

	return result;
}

// ---- KHR Iridescence --------------------------------------------------------

const float M_PI = 3.14159265359;
const mat3 XYZ_TO_REC709 = mat3(
	3.2404542, -0.9692660, 0.0556434,
	-1.5371385, 1.8760108, -0.2040259,
	-0.4985314, 0.0415560, 1.0572252
);

// Helper: square a value
float sq(float a) { return a * a; }
vec3 sq(vec3 a) { return a * a; }

// Fresnel0 to IOR conversion
vec3 Fresnel0ToIor(vec3 fresnel0)
{
	vec3 sqrtF0 = sqrt(fresnel0);
	return (vec3(1.0) + sqrtF0) / (vec3(1.0) - sqrtF0);
}

// IOR to Fresnel0 conversion (vec3 version)
vec3 IorToFresnel0(vec3 transmittedIor, float incidentIor)
{
	return sq((transmittedIor - vec3(incidentIor)) / (transmittedIor + vec3(incidentIor)));
}

// IOR to Fresnel0 conversion (float version)
float IorToFresnel0(float transmittedIor, float incidentIor)
{
	return sq((transmittedIor - incidentIor) / (transmittedIor + incidentIor));
}

// Scalar version with F90
float F_Schlick_Iridescence(float f0, float cosTheta, float f90)
{
	return f0 + (f90 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Vector version with F90
vec3 F_Schlick_Iridescence(vec3 f0, float cosTheta, vec3 f90)
{
	return f0 + (f90 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Keep old versions for backward compatibility (optional)
float F_Schlick_Iridescence(float f0, float cosTheta)
{
	return F_Schlick_Iridescence(f0, cosTheta, 1.0);
}

vec3 F_Schlick_Iridescence(vec3 f0, float cosTheta)
{
	return F_Schlick_Iridescence(f0, cosTheta, vec3(1.0));
}

// CRITICAL: XYZ sensitivity curves -> RGB (makes colors vibrant!)
vec3 evalSensitivity(float OPD, vec3 shift)
{
	float phase = 2.0 * M_PI * OPD * 1.0e-9;
	vec3 val = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
	vec3 pos = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
	vec3 var = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);

	vec3 xyz = val * sqrt(2.0 * M_PI * var) * cos(pos * phase + shift) * exp(-sq(phase) * var);
	xyz.x += 9.7470e-14 * sqrt(2.0 * M_PI * 4.5282e+09) * cos(2.2399e+06 * phase + shift[0]) * exp(-4.5282e+09 * sq(phase));
	xyz /= 1.0685e-7;

	vec3 srgb = XYZ_TO_REC709 * xyz;
	return srgb;
}

vec3 evalIridescence(float outsideIOR, float eta2, float cosTheta1,
	float thinFilmThickness, vec3 baseF0)
{
	return evalIridescence(outsideIOR, eta2, cosTheta1, thinFilmThickness, baseF0, vec3(1.0));
}

vec3 evalIridescence(float outsideIOR, float eta2, float cosTheta1,
	float thinFilmThickness, vec3 baseF0, vec3 baseF90)
{
	vec3 I;
	float iridescenceIor = mix(outsideIOR, eta2, smoothstep(0.0, 0.03, thinFilmThickness));
	float sinTheta2Sq = sq(outsideIOR / iridescenceIor) * (1.0 - sq(cosTheta1));
	float cosTheta2Sq = 1.0 - sinTheta2Sq;
	if (cosTheta2Sq < 0.0)
	{
		return vec3(1.0);
	}
	float cosTheta2 = sqrt(cosTheta2Sq);

	// First interface (air to iridescent film)
	// F90 at air-film interface is always 1.0
	float R0 = IorToFresnel0(iridescenceIor, outsideIOR);
	float R12 = F_Schlick_Iridescence(R0, cosTheta1, 1.0);  // F90 = 1.0 for air interface
	float R21 = R12;
	float T121 = 1.0 - R12;
	float phi12 = 0.0;
	if (iridescenceIor < outsideIOR) phi12 = M_PI;
	float phi21 = M_PI - phi12;

	// Second interface (iridescent film to base material)
	// F90 at film-base interface uses baseF90 from the base material
	vec3 baseIOR = Fresnel0ToIor(clamp(baseF0, 0.0, 0.9999));
	vec3 R1 = IorToFresnel0(baseIOR, iridescenceIor);
	vec3 R23 = F_Schlick_Iridescence(R1, cosTheta2, baseF90);  // Use baseF90 for base material
	vec3 phi23 = vec3(0.0);
	if (baseIOR[0] < iridescenceIor) phi23[0] = M_PI;
	if (baseIOR[1] < iridescenceIor) phi23[1] = M_PI;
	if (baseIOR[2] < iridescenceIor) phi23[2] = M_PI;

	// Optical path difference
	float OPD = 2.0 * iridescenceIor * thinFilmThickness * cosTheta2;
	vec3 phi = vec3(phi21) + phi23;

	// Compound terms
	vec3 R123 = clamp(R12 * R23, 1e-5, 0.9999);
	vec3 r123 = sqrt(R123);
	vec3 Rs = sq(T121) * R23 / (vec3(1.0) - R123);

	// DC term
	vec3 C0 = R12 + Rs;
	I = C0;

	// Higher order interference
	vec3 Cm = Rs - T121;
	for (int m = 1; m <= 2; ++m)
	{
		Cm *= r123;
		vec3 Sm = 2.0 * evalSensitivity(float(m) * OPD, float(m) * phi);
		I += Cm * Sm;
	}
	return max(I, vec3(0.0));
}

vec3 computeIridescentFresnel(float cosTheta,
	vec3 baseF0,
	vec3 baseF90,
	float iridescenceFactor,
	float iridescenceIor,
	float iridescenceThickness)
{
	float clampedCosTheta = clamp(cosTheta, 0.0, 1.0);
	vec3 baseFresnel = fresnelSchlick(clampedCosTheta, baseF0, baseF90);
	if (iridescenceFactor <= 0.001)
	{
		return baseFresnel;
	}

	vec3 iridescentFresnel = evalIridescence(
		1.0,
		iridescenceIor,
		clampedCosTheta,
		iridescenceThickness,
		clamp(baseF0, vec3(0.0), vec3(0.9999)),
		baseF90
	);
	return mix(baseFresnel, iridescentFresnel, iridescenceFactor);
}

// ---- KHR Transmission & Volume ----------------------------------------------

vec3 calculateTransmissionKHR(vec3 normal, vec3 view, vec3 pointToLight, float alphaRoughness, vec3 baseColor, float ior)
{
	float transmissionRoughness = applyIorToRoughness(alphaRoughness, ior);

	vec3 n = normalize(normal);
	vec3 v = normalize(view);
	vec3 l = normalize(pointToLight);
	vec3 l_mirror = normalize(l + 2.0 * n * dot(-l, n));
	vec3 h = normalize(l_mirror + v);

	float D = distributionGGX(n, h, transmissionRoughness);
	float G = geometrySmith(n, v, l_mirror, transmissionRoughness);
	float NdotL = clamp(dot(n, l_mirror), 0.0, 1.0);
	float NdotV = clamp(dot(n, v), 0.0, 1.0);
	float Vis = (4.0 * NdotL * NdotV > 0.0) ? (G / (4.0 * NdotL * NdotV)) : 0.0;

	return baseColor * D * Vis;
}

// KHR_materials_volume
vec3 calculateVolumeAttenuation(vec3 transmittedLight, float distance, float thickness, vec3 attenuationColor, float attenuationDistance)
{
	if (attenuationDistance == 0.0)
	{
		return transmittedLight;
	}

	float transmissionDistance = max(distance, max(thickness, 0.0));
	vec3 transmittance = pow(attenuationColor, vec3(transmissionDistance / attenuationDistance));

	return transmittedLight * transmittance;
}


// ============================================================================
// Helper: Apply IOR to Roughness (for LOD calculation)
// ============================================================================
float applyIorToRoughness(float roughness, float ior)
{
	// Scale roughness with IOR so that an IOR of 1.0 results in no microfacet refraction and
	// an IOR of 1.5 results in the default amount of microfacet refraction.
	return roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
}

// ============================================================================
// Calculate 3D Transmission Ray Exit Point
// ============================================================================
vec3 getVolumeTransmissionRay(vec3 n, vec3 v, float thickness, float ior, mat4 modelMatrix)
{
	// Direction of refracted light (Snell's law)
	vec3 refractionVector = refract(-v, normalize(n), 1.0 / ior);

	// The thickness is specified in local space
	// Returns the world-space displacement vector
	return normalize(refractionVector) * thickness * computeVolumeScaleFactor();
}

// ============================================================================
// Sample Transmission with Roughness LOD
// ============================================================================
vec3 getTransmissionSample(vec2 fragCoord, float roughness, float ior)
{
	// Calculate LOD based on roughness and IOR
	// Higher roughness and/or higher IOR = access lower (blurred) mip levels
	float framebufferLod = log2(transmissionFramebufferSize.x) * applyIorToRoughness(roughness, ior);

	// Sample with automatic mipmap interpolation
	vec3 transmittedLight = textureLod(transmissionSceneTexture, fragCoord.xy, framebufferLod).rgb;

	return transmittedLight;
}

// ============================================================================
// Main Transmission Ray Tracing Function
// ============================================================================
vec3 getIBLVolumeRefraction(vec3 n, vec3 v, float perceptualRoughness, vec3 baseColor,
	vec3 position, mat4 modelMatrix, float ior, float thickness,
	vec3 attenuationColor, float attenuationDistance)
{
	// Calculate 3D transmission ray (refracted path through volume)
	vec3 transmissionRay = getVolumeTransmissionRay(n, v, thickness, ior, modelMatrix);

	// Calculate exit point in world space
	vec3 refractedRayExit = position + transmissionRay;

	// Project exit point to screen space
	vec4 ndcPos = projectionMatrix * viewMatrix * vec4(refractedRayExit, 1.0);
	vec2 refractionCoords = ndcPos.xy / ndcPos.w;
	refractionCoords += 1.0;
	refractionCoords /= 2.0;

	// Get transmission ray length for attenuation
	float transmissionRayLength = length(transmissionRay);

	// Sample framebuffer at projected coordinates with roughness LOD
	vec3 transmittedLight = getTransmissionSample(refractionCoords, perceptualRoughness, ior);

	// Apply volume attenuation (Beer's law)
	if (transmissionRayLength > 0.0 && attenuationDistance > 0.0)
	{
		vec3 transmittance = pow(attenuationColor, vec3(transmissionRayLength / attenuationDistance));
		transmittedLight *= transmittance;
	}

	// Apply base color tinting
	transmittedLight *= baseColor;

	return transmittedLight;
}

// ============================================================================
// Per-Channel Transmission for Dispersion
// ============================================================================
vec3 getIBLVolumeRefractionPerChannel(vec3 n, vec3 v, float perceptualRoughness, vec3 baseColor,
	vec3 position, mat4 modelMatrix, vec3 iors, float thickness,
	vec3 attenuationColor, float attenuationDistance)
{
	vec3 transmittedLight = vec3(0.0);

	// Process each channel (R, G, B) with different IOR
	for (int i = 0; i < 3; i++)
	{
		float ior = iors[i];

		// Get transmission ray for this channel
		vec3 transmissionRay = getVolumeTransmissionRay(n, v, thickness, ior, modelMatrix);
		vec3 refractedRayExit = position + transmissionRay;
		float transmissionRayLength = length(transmissionRay);

		// Project to screen space
		vec4 ndcPos = projectionMatrix * viewMatrix * vec4(refractedRayExit, 1.0);
		vec2 refractionCoords = ndcPos.xy / ndcPos.w;
		refractionCoords += 1.0;
		refractionCoords /= 2.0;

		// Sample with LOD for this channel's IOR
		vec3 sampledLight = getTransmissionSample(refractionCoords, perceptualRoughness, ior);

		// Apply attenuation
		if (transmissionRayLength > 0.0 && attenuationDistance > 0.0)
		{
			vec3 transmittance = pow(attenuationColor, vec3(transmissionRayLength / attenuationDistance));
			sampledLight *= transmittance;
		}

		// Extract channel component for chromatic aberration effect
		transmittedLight[i] = sampledLight[i] * baseColor[i];
	}

	return transmittedLight;
}

// ---- KHR Volume Scatter -----------------------------------------------------

// KHR_materials_volume_scatter: Convert multi-scatter color to single scatter ratio
// Based on glTF 2.0 specification
vec3 multiToSingleScatter()
{
	vec3 s = 4.09712 + 4.20863 * multiScatterColor - sqrt(9.59217 + 41.6808 * multiScatterColor + 17.7126 * multiScatterColor * multiScatterColor);
	return 1.0 - s * s;
}

vec3 burleySetup(vec3 radius, vec3 albedo)
{
	const float invPi = 0.31830988618;
	vec3 s = 1.9 - albedo + 3.5 * ((albedo - 0.8) * (albedo - 0.8));
	vec3 l = 0.25 * invPi * radius;
	return l / s;
}

vec3 burleyEval(vec3 d, float r)
{
	vec3 exp_r_3_d = exp(-r / (3.0 * d));
	vec3 exp_r_d = exp_r_3_d * exp_r_3_d * exp_r_3_d;
	vec3 value = (exp_r_d + exp_r_3_d) / (8.0 * PI * d);
	bvec3 valid = lessThan(vec3(r), 16.0 * d);
	return vec3(
		valid.x ? value.x : 0.0,
		valid.y ? value.y : 0.0,
		valid.z ? value.z : 0.0
	);
}

vec3 sampleCapturedSSSDiffuse(float attenuationDistance, vec3 diffuseColor)
{
	if (sssFramebufferSize.x <= 0.0 || sssFramebufferSize.y <= 0.0)
		return vec3(0.0);

	const float scatterMinRadius = 0.001394607;
	// 16 samples subsampled from the original 55-tap Burley disk — indices chosen
	// to maintain even inner/middle/outer radius coverage while preserving the
	// critical high-weight outer samples that define the long-range scatter glow.
	// The weighted-average normalisation (totalDiffuse/totalWeight) corrects for
	// the reduced sampling density, so the profile shape is preserved.
	const int sampleCount = 16;
	const vec3 scatterSamples[sampleCount] = vec3[](
		vec3(3.141593,   0.001395, 0.969751),   // i=0  inner
		vec3(10.341482,  0.010135, 1.045482),   // i=3
		vec3(19.941335,  0.022900, 1.163813),   // i=7
		vec3(29.541188,  0.037174, 1.307319),   // i=11
		vec3(39.141041,  0.053291, 1.484019),   // i=15 mid-inner
		vec3(48.740894,  0.071700, 1.705477),   // i=19
		vec3(60.740710,  0.098885, 2.072299),   // i=24 mid
		vec3(72.740526,  0.132402, 2.593131),   // i=29
		vec3(84.740342,  0.175170, 3.376072),   // i=34 mid-outer
		vec3(96.740159,  0.232459, 4.660052),   // i=39
		vec3(108.739975, 0.315440, 7.116369),   // i=44 outer
		vec3(118.339828, 0.420016, 11.641928),  // i=48
		vec3(123.139754, 0.500418, 16.736521),  // i=50
		vec3(127.939681, 0.627013, 29.298947),  // i=52 long-range
		vec3(130.339644, 0.733019, 46.613892),  // i=53
		vec3(132.739607, 0.936606, 113.316758)  // i=54 far glow
	);

	vec2 texelSize = 1.0 / sssFramebufferSize;
	vec2 uv = gl_FragCoord.xy * texelSize;
	vec4 centerSample = textureLod(sssDiffuseTexture, uv, 0.0);
	float centerId = centerSample.a;
	if (centerId <= 0.0)
		return centerSample.rgb;

	float centerDepth = textureLod(sssDepthTexture, uv, 0.0).r * 2.0 - 1.0;
	vec2 clipUV = uv * 2.0 - 1.0;
	vec4 clipSpacePosition = vec4(clipUV.x, clipUV.y, centerDepth, 1.0);
	vec4 upos = inverseProjectionMatrix * clipSpacePosition;
	vec3 fragViewPosition = upos.xyz / max(upos.w, 1e-6);
	upos = inverseProjectionMatrix * vec4(clipUV.x + texelSize.x, clipUV.y, centerDepth, 1.0);
	vec3 offsetViewPosition = upos.xyz / max(upos.w, 1e-6);
	float metersPerPixel = distance(fragViewPosition, offsetViewPosition);
	if (metersPerPixel <= 1e-6)
		return centerSample.rgb;

	vec3 scatterDistance = attenuationDistance * multiScatterColor;
	float maxColor = max(max3(scatterDistance), 1e-5);
	float maxRadiusPixels = maxColor / metersPerPixel;
	if (maxRadiusPixels <= 1.0)
		return centerSample.rgb;

	vec3 clampedScatterDistance = max(vec3(scatterMinRadius), scatterDistance / maxColor) * maxColor;
	vec3 d = burleySetup(clampedScatterDistance, vec3(1.0));
	vec3 totalWeight = vec3(0.0);
	vec3 totalDiffuse = vec3(0.0);

	for (int i = 0; i < sampleCount; ++i)
	{
		vec3 scatterSample = scatterSamples[i];
		float angle = scatterSample.x;
		float r = scatterSample.y * maxRadiusPixels * texelSize.x;
		vec2 sampleUV = uv + vec2(cos(angle) * r, sin(angle) * r);
		vec4 sampleColor = textureLod(sssDiffuseTexture, sampleUV, 0.0);
		if (sampleColor.a != centerId)
			continue;

		float sampleDepth = textureLod(sssDepthTexture, sampleUV, 0.0).r * 2.0 - 1.0;
		vec2 sampleClipUV = sampleUV * 2.0 - 1.0;
		vec4 sampleUpos = inverseProjectionMatrix * vec4(sampleClipUV.x, sampleClipUV.y, sampleDepth, 1.0);
		vec3 sampleViewPosition = sampleUpos.xyz / max(sampleUpos.w, 1e-6);
		float sampleDistance = distance(sampleViewPosition, fragViewPosition);
		vec3 weight = burleyEval(d, sampleDistance) * scatterSample.z;
		totalWeight += weight;
		totalDiffuse += weight * sampleColor.rgb;
	}

	totalWeight = max(totalWeight, vec3(1e-4));
	return (totalDiffuse / totalWeight) * diffuseColor;
}

// ---- Shadows ----------------------------------------------------------------

// ----------------------------------------------------------------------------
float calculateShadowVariableKernel(vec4 fragPosLightSpace, vec3 fragPos, vec3 lightPos)
{
	vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = projCoords * 0.5 + 0.5;
	if (projCoords.z > 1.0)
		return 0.0;

	float distanceToLight = length(fragPos - lightPos);

	// Size-aware kernel scaling
	int kernelSize;
	float distanceThreshold1 = 5.0 / max(shadowSizeScale, 0.1);
	float distanceThreshold2 = 15.0 / max(shadowSizeScale, 0.1);

	if (distanceToLight < distanceThreshold1)
		kernelSize = max(2, shadowMaxKernelSize - 2);
	else if (distanceToLight < distanceThreshold2)
		kernelSize = max(3, shadowMaxKernelSize - 1);
	else
		kernelSize = shadowMaxKernelSize;

	// Size-aware adaptive softness
	float sizeAdjustedSoftness = shadowSoftness * shadowSizeScale;
	float adaptiveSoftness = sizeAdjustedSoftness * clamp(
		distanceToLight * shadowSoftnessScale,
		1.0,
		shadowMaxSoftnessClamp
	);

	// Rest of the function remains the same...
	float currentDepth = projCoords.z;
	float shadow = 0.0;
	float totalWeight = 0.0;

	for (int x = -kernelSize; x <= kernelSize; ++x)
	{
		for (int y = -kernelSize; y <= kernelSize; ++y)
		{
			float distSq = float(x * x + y * y);
			if (distSq > float(kernelSize * kernelSize)) continue;
			float weight = exp(-distSq / float(kernelSize * kernelSize));
			totalWeight += weight;

			vec2 offset = vec2(x, y) * adaptiveSoftness * 1.5 / lightFarPlane;
			float sampleDepth = texture(shadowMap, projCoords.xy + offset).r;

			float bias = mix(shadowBiasMin, shadowBiasMax,
				clamp(distanceToLight * 0.05, 0.0, 1.0));
			float depthDiff = currentDepth - sampleDepth - bias;

			float shadowContrib = smoothstep(-shadowTransitionRange, shadowTransitionRange, depthDiff);
			shadow += shadowContrib * weight;
		}
	}

	shadow /= totalWeight;
	shadow = pow(shadow, shadowGammaCorrection);

	return shadow;
}

// ---- Background -------------------------------------------------------------

vec2 calculateBackgroundUV()
{
	vec2 ndc = (gl_FragCoord.xy / screenSize) * 2.0 - 1.0;
	return ndc * 0.5 + 0.5;
}

vec3 calculateBackgroundColor()
{
	vec2 v_uv = calculateBackgroundUV();

	vec4 frag_color;
	if (gradientStyle == 0)
	{
		frag_color = mix(botColor, topColor, v_uv.y);
	}
	else if (gradientStyle == 1)
	{
		frag_color = mix(topColor, botColor, v_uv.x);
	}
	else if (gradientStyle == 2)
	{
		float diagonal_factor = (v_uv.x + (1.0 - v_uv.y)) * 0.5;
		frag_color = mix(topColor, botColor, diagonal_factor);
	}
	else if (gradientStyle == 3)
	{
		float diagonal_factor = ((1.0 - v_uv.x) + (1.0 - v_uv.y)) * 0.5;
		frag_color = mix(topColor, botColor, diagonal_factor);
	}
	else
	{
		frag_color = mix(botColor, topColor, v_uv.y);
	}

	return frag_color.rgb;
}

// ---- Tone mapping & colour space --------------------------------------------

// sRGB <-> Linear helpers (IEC 61966-2-1 piecewise)
vec3 sRGBToLinear(vec3 c)
{
	return mix(c / 12.92,
	           pow((c + 0.055) / 1.055, vec3(2.4)),
	           step(vec3(0.04045), c));
}
vec3 linearTosRGB(vec3 c)
{
	return mix(c * 12.92,
	           1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
	           step(vec3(0.0031308), c));
}

float sRGBSaturation(vec3 c)
{
	float mx = max(max(c.r, c.g), c.b);
	float mn = min(min(c.r, c.g), c.b);
	return mx - mn; // cheap proxy; OK for gray detection
}

// ============================================================================
// Tone Mapping
// ============================================================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
	0.59719, 0.07600, 0.02840,
	0.35458, 0.90834, 0.13383,
	0.04823, 0.01566, 0.83777
);


// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
	1.60475, -0.10208, -0.00327,
	-0.53108, 1.10813, -0.07276,
	-0.07367, -0.00605, 1.07602
);

// ACES tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMapACES_Narkowicz(vec3 color)
{
	const float A = 2.51;
	const float B = 0.03;
	const float C = 2.43;
	const float D = 0.59;
	const float E = 0.14;
	return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

// ACES filmic tone map approximation
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
	vec3 a = color * (color + 0.0245786) - 0.000090537;
	vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	return a / b;
}

vec3 toneMapACES_Hill(vec3 color)
{
	color = ACESInputMat * color;

	// Apply RRT and ODT
	color = RRTAndODTFit(color);

	color = ACESOutputMat * color;

	// Clamp to [0, 1]
	color = clamp(color, 0.0, 1.0);

	return color;
}

// Khronos PBR neutral tone mapping
vec3 toneMap_KhronosPbrNeutral(vec3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) return color;

	const float d = 1. - startCompression;
	float newPeak = 1. - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
	return mix(color, newPeak * vec3(1, 1, 1), g);
}

vec3 uncharted2ToneMapping(vec3 color)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;
	const float W = 11.2;

	color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
	float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
	return color / white;
}

vec3 applyToneMapping(vec3 color)
{
	if (!hdrToneMapping) return color;

	color *= iblExposure;

	if (toneMapMode == 0)
	{
		color = toneMap_KhronosPbrNeutral(color);
	}
	else if (toneMapMode == 1)
	{
		color = toneMapACES_Narkowicz(color);
	}
	else if (toneMapMode == 2)
	{
		color = toneMapACES_Hill(color);
	}
	else if (toneMapMode == 3)
	{
		// boost exposure as discussed in https://github.com/mrdoob/three.js/pull/19621
		// this factor is based on the exposure correction of Krzysztof Narkowicz in his
		// implementation of ACES tone mapping
		color /= 0.6;
		color = toneMapACES_Hill(color);
	}
	else if (toneMapMode == 4)
	{
		color = uncharted2ToneMapping(color);
	}
	else
	{
		// Reinhard
		color = color / (color + vec3(1.0));
	}

	return color;
}

// ---- Miscellaneous utilities ------------------------------------------------

float readMaskChannel(vec4 texel, int channel)
{
	if (channel == 0) return texel.r;
	if (channel == 1) return texel.g;
	if (channel == 2) return texel.b;
	return texel.a;
}

// pickChannel helper: choose channel (0=r,1=g,2=b,3=a), optionally invert and remap
float pickChannel(vec4 v, int ch, int invertFlag, float scale, float bias)
{
	float c = 0.0;
	if (ch == 0) c = v.r;
	else if (ch == 1) c = v.g;
	else if (ch == 2) c = v.b;
	else if (ch == 3) c = v.a;
	else c = 0.0; // sentinel value (no channel)

	if (invertFlag != 0) c = 1.0 - c;
	c = c * scale + bias;
	return clamp(c, 0.0, 1.0);
}

// compute sampled opacity from a dedicated opacity map (with packing support)
float sampleOpacityMap(vec2 uv)
{
	if (!hasOpacityMap) return 1.0; // neutral when no map
	vec4 opTex = texture(opacityMap, uv);

	// channel-aware extraction: if channel < 0 fallback to .r (legacy)
	float val;
	if (opacityChannel >= 0)
	{
		val = pickChannel(opTex, opacityChannel, opacityInvert, opacityScale, opacityBias);
	}
	else
	{
		// legacy fallback: red channel + optional invert/scale/bias
		val = opTex.r * opacityScale + opacityBias;
		if (opacityInvert != 0) val = 1.0 - val;
		val = clamp(val, 0.0, 1.0);
	}
	return val;
}

// compute fallback opacity from albedo/diffuse or legacy opacity texture
float sampleFallbackOpacity(vec2 uv)
{
	float val = 1.0;

	if (renderingMode == 0)
	{ // ADS -> use diffuse alpha (if present) and/or texture_opacity
		if (hasDiffuseTexture)
		{
			vec4 diff = texture(texture_diffuse, uv);
			val *= diff.a; // alpha from diffuse texture
		}
		if (hasOpacityTexture)
		{
			// legacy single-channel opacity texture (no channel packing currently)
			float optex = texture(texture_opacity, uv).r;
			if (opacityTextureInverted) optex = 1.0 - optex;
			val *= optex;
		}
	}
	else
	{ // PBR mode -> albedo alpha may indicate opacity
		if (hasAlbedoMap)
		{
			vec4 alb = texture(albedoMap, uv);
			// Use albedo alpha as fallback but be conservative (do not force)
			val *= alb.a;
		}
	}

	return clamp(val, 0.0, 1.0);
}

// ---- KHR PBR path - setup & materials ---------------------------------------

SurfaceFrame buildSurfaceFrame(float side, vec2 normalUV, vec2 clearcoatNormalUV, vec3 lightDirection)
{
	SurfaceFrame frame;

	frame.V = normalize(cameraPos - getFragPosition());
	frame.I = -frame.V;
	frame.L = normalize(lightDirection);
	float frameSide = side < 0.0 ? -1.0 : 1.0;
	if (length(v_reflectionNormal) < 0.01)
	{
		// No vertex normals: derive face normal from screen-space position derivatives
		vec3 dx = dFdx(v_position);
		vec3 dy = dFdy(v_position);
		vec3 faceN = cross(dx, dy);
		if (length(faceN) > 0.0001)
			frame.Ng = normalize(faceN);
		else
			frame.Ng = normalize(cameraPos - v_position); // camera-facing fallback for points/lines
	}
	else
	{
		frame.Ng = getUnsignedWorldGeometryNormal();
	}

	vec3 tangent;
	vec3 bitangent;
	if (length(v_worldTangent) > 0.01)
	{
		tangent = normalize(v_worldTangent - dot(v_worldTangent, frame.Ng) * frame.Ng);
		float handedness = 1.0;
		if (length(v_worldBitangent) > 0.01)
		{
			vec3 importedBitangent = normalize(v_worldBitangent - dot(v_worldBitangent, frame.Ng) * frame.Ng);
			handedness = sign(dot(cross(frame.Ng, tangent), importedBitangent));
			if (handedness == 0.0)
			{
				handedness = 1.0;
			}
		}
		bitangent = normalize(cross(frame.Ng, tangent)) * handedness;
	}
	else
	{
		vec3 Q1 = dFdx(v_position);
		vec3 Q2 = dFdy(v_position);
		vec2 st1 = dFdx(normalUV);
		vec2 st2 = dFdy(normalUV);

		tangent = Q1 * st2.t - Q2 * st1.t;
		if (length(tangent) < 0.0001)
		{
			vec3 fallback = abs(frame.Ng.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
			tangent = cross(fallback, frame.Ng);
		}
		tangent = normalize(tangent - dot(tangent, frame.Ng) * frame.Ng);
		bitangent = normalize(cross(frame.Ng, tangent));
	}

	// The caller already selects the front/back shading frame through `side`.
	// Flipping again from the raster-facing flag makes the back-face path reuse the
	// front-side normal, which washes out two-sided PBR materials.
	if (frameSide < 0.0)
	{
		frame.Ng *= -1.0;
		tangent *= -1.0;
		bitangent *= -1.0;
	}

	frame.T = tangent;
	frame.B = bitangent;

	frame.N = frame.Ng;
	if (hasNormalMap)
	{
		frame.N = sampleMappedNormal(
			normalMap,
			normalUV,
			pbrLighting.normalScale,
			frame.Ng,
			frame.T,
			frame.B
		);
	}

	frame.Ncoat = frame.Ng;
	if (hasClearcoatNormalMap)
	{
		frame.Ncoat = sampleMappedNormal(
			clearcoatNormalMap,
			clearcoatNormalUV,
			clearcoatNormalScale,
			frame.Ng,
			frame.T,
			frame.B
		);
	}

	return frame;
}

vec3 computeDielectricF0(float ior, float specularFactor, vec3 specularColor, bool useSpecGloss, vec3 specGlossSpecular)
{
	if (useSpecGloss)
	{
		return clamp(specGlossSpecular, vec3(0.0), vec3(1.0));
	}

	float f0_from_ior = pow((ior - 1.0) / (ior + 1.0), 2.0);
	vec3 dielectricF0 = vec3(f0_from_ior);
	if (specularFactor > 0.0)
	{
		dielectricF0 *= specularColor;
	}
	return clamp(dielectricF0, vec3(0.0), vec3(1.0));
}

vec3 computeF90(float metallic, float specularFactor)
{
	vec3 F90_dielectric = vec3(specularFactor);
	return mix(F90_dielectric, vec3(1.0), metallic);
}

float computeVolumeScaleFactor()
{
	return (length(vec3(modelMatrix[0].xyz)) +
		length(vec3(modelMatrix[1].xyz)) +
		length(vec3(modelMatrix[2].xyz))) / 3.0;
}

float computeVolumeThickness(float thickness)
{
	return thickness * computeVolumeScaleFactor();
}

vec3 computeBaseColor(vec2 uv,
	vec3 matBaseColor_linear,
	sampler2D albedoTex,
	bool hasAlbedoTex,
	vec3 vertexColor_linear,
	bool useVertexColor)
{
	vec3 tex_sRGB = hasAlbedoTex ? texture(albedoTex, uv).rgb : vec3(1.0);
	vec3 tex_L = sRGBToLinear(tex_sRGB);
	vec3 vtx_L = useVertexColor ? vertexColor_linear : vec3(1.0);

	vec3 out_L;

	if (!hasAlbedoTex)
	{
		out_L = matBaseColor_linear; // color only
	}
	else
	{
		out_L = tex_L * matBaseColor_linear;
	}

	// Apply vertex color last (in linear)
	out_L *= vtx_L;

	return out_L; // keep in linear for the rest of PBR
}

MaterialParams gatherMaterialParams()
{
	MaterialParams params;

	params.baseColor = pbrLighting.albedo;
	params.emissive = material.emission * pbrLighting.emissiveStrength;
	params.metallic = clamp(pbrLighting.metallic, 0.0, 1.0);
	params.roughness = clamp(pbrLighting.roughness, 0.0001, 1.0);
	params.ambientOcclusion = clamp(guardFactorScalar(pbrLighting.ambientOcclusion, 0.01), 0.0001, 1.0);
	params.transmission = pbrLighting.transmission;
	params.ior = pbrLighting.ior;
	params.clearcoat = clamp(pbrLighting.clearcoat, 0.0, 1.0);
	params.clearcoatRoughness = clamp(pbrLighting.clearcoatRoughness, 0.0001, 1.0);
	params.sheenColor = clamp(pbrLighting.sheenColor, vec3(0.0), vec3(1.0));
	params.sheenRoughness = clamp(pbrLighting.sheenRoughness, 0.0001, 1.0);
	params.anisotropyStrength = pbrLighting.anisotropyStrength;
	params.anisotropyRotation = pbrLighting.anisotropyRotation;
	params.iridescenceFactor = pbrLighting.iridescenceFactor;
	params.iridescenceIor = pbrLighting.iridescenceIor;
	params.iridescenceThickness = pbrLighting.iridescenceThicknessMax;
	params.thickness = pbrLighting.thicknessFactor;
	params.diffuseTransmissionFactor = pbrLighting.diffuseTransmissionFactor;
	params.diffuseTransmissionColor = pbrLighting.diffuseTransmissionColorFactor;
	params.attenuationColor = pbrLighting.attenuationColor;
	params.attenuationDistance = pbrLighting.attenuationDistance;
	params.dispersion = pbrLighting.dispersion;
	params.specularFactor = pbrLighting.specularFactor;
	params.specularColor = pbrLighting.specularColorFactor;
	params.useSpecGloss = useSpecularGlossiness;
	params.unlit = pbrLighting.unlit;

	if (params.useSpecGloss)
	{
		params.baseColor = computeBaseColor(
			getDiffuseUV(),
			diffuseFactor,
			diffuseMap,
			hasDiffuseMap,
			v_color.rgb,
			hasVertexColors
		);

		params.specularColor = specularColor;
		if (hasSpecularGlossinessMap)
		{
			vec4 specGlossSample = texture(specularGlossinessMap, getSpecularGlossinessUV());
			params.specularColor *= specGlossSample.rgb;
			params.roughness = clamp(1.0 - (glossinessFactor * specGlossSample.a), 0.0001, 1.0);
		}
		else
		{
			params.roughness = clamp(1.0 - glossinessFactor, 0.0001, 1.0);
		}

		params.metallic = 0.0;
		params.specularFactor = 1.0;
	}
	else
	{
		params.baseColor = computeBaseColor(
			getAlbedoUV(),
			pbrLighting.albedo,
			albedoMap,
			hasAlbedoMap,
			v_color.rgb,
			hasVertexColors
		);
	}
	if (hasEmissiveMap)
	{
		vec3 emissionFactor = guardFactorColor(material.emission, 0.01);
		params.emissive = sRGBToLinear(texture(emissiveMap, getEmissiveUV()).rgb) * emissionFactor * pbrLighting.emissiveStrength;
	}
	if (hasAOMap)
	{
		float texAO = samplePackedChannelValue(aoMap, hasAOMap, getAOUV(),
			aoChannel, aoInvert, aoScale, aoBias, pbrLighting.ambientOcclusion);
		params.ambientOcclusion = clamp(mix(1.0, texAO, pbrLighting.occlusionStrength), 0.0001, 1.0);
	}
	if (!params.useSpecGloss)
	{
		float texMetallic = samplePackedChannelValue(metallicMap, hasMetallicMap, getMetallicUV(),
			metallicChannel, metallicInvert, metallicScale, metallicBias, 1.0);
		params.metallic = clamp(params.metallic * texMetallic, 0.0, 1.0);

		float texRoughness = samplePackedChannelValue(roughnessMap, hasRoughnessMap, getRoughnessUV(),
			roughnessChannel, roughnessInvert, roughnessScale, roughnessBias, 1.0);
		params.roughness = clamp(params.roughness * texRoughness, 0.0001, 1.0);
	}
	if (hasTransmissionMap)
	{
		params.transmission *= texture(transmissionMap, getTransmissionUV()).r;
	}
	if (hasIORMap)
	{
		params.ior = texture(iorMap, getIORUV()).r;
	}
	if (hasSheenColorMap)
	{
		params.sheenColor *= sRGBToLinear(texture(sheenColorMap, getSheenColorUV()).rgb);
	}
	if (hasSheenRoughnessMap)
	{
		params.sheenRoughness *= texture(sheenRoughnessMap, getSheenRoughnessUV()).a;
	}
	if (hasAnisotropyMap || params.anisotropyStrength > 0.0)
	{
		AnisotropyData anisoData;
		if (hasAnisotropyMap)
		{
			vec3 anisoTexel = texture(anisotropyMap, getAnisotropyUV()).rgb;
			anisoData = decodeAnisotropyTexture(
				anisoTexel,
				pbrLighting.anisotropyStrength,
				pbrLighting.anisotropyRotation,
				true
			);
		}
		else
		{
			anisoData = decodeAnisotropyTexture(
				vec3(1.0, 0.5, 1.0),
				pbrLighting.anisotropyStrength,
				pbrLighting.anisotropyRotation,
				false
			);
		}
		params.anisotropyStrength = anisoData.strength;
		params.anisotropyRotation = anisoData.rotation;
	}
	if (hasClearcoatMap)
	{
		params.clearcoat *= texture(clearcoatColorMap, getClearcoatUV()).r;
	}
	if (hasClearcoatRoughnessMap)
	{
		params.clearcoatRoughness *= texture(clearcoatRoughnessMap, getClearcoatRoughnessUV()).g;
	}
	if (hasSpecularFactorMap)
	{
		params.specularFactor *= texture(specularFactorMap, getSpecularFactorUV()).a;
	}
	if (hasSpecularColorMap)
	{
		params.specularColor *= sRGBToLinear(texture(specularColorMap, getSpecularColorUV()).rgb);
	}
	if (hasIridescenceMap)
	{
		params.iridescenceFactor *= texture(iridescenceMap, getIridescenceUV()).r;
	}
	if (hasIridescenceThicknessMap)
	{
		float thicknessNorm = texture(iridescenceThicknessMap, getIridescenceThicknessUV()).g;
		params.iridescenceThickness = mix(pbrLighting.iridescenceThicknessMin, pbrLighting.iridescenceThicknessMax, thicknessNorm);
	}
	if (hasThicknessMap)
	{
		vec4 thicknessTexel = texture(thicknessMap, getThicknessUV());
		float thicknessSample = hasThicknessAlpha ? thicknessTexel.a : thicknessTexel.g;
		params.thickness *= thicknessSample;
	}
	if (hasDiffuseTransmissionMap)
	{
		params.diffuseTransmissionFactor *= texture(diffuseTransmissionMap, getDiffuseTransmissionUV()).a;
	}
	if (hasDiffuseTransmissionColorMap)
	{
		params.diffuseTransmissionColor *= sRGBToLinear(texture(diffuseTransmissionColorMap, getDiffuseTransmissionColorUV()).rgb);
	}

	params.baseColor = clamp(params.baseColor, vec3(0.0), vec3(1.0));
	params.sheenColor = clamp(params.sheenColor, vec3(0.0), vec3(1.0));
	params.specularColor = max(params.specularColor, vec3(0.0));
	params.clearcoat = clamp(params.clearcoat, 0.0, 1.0);
	params.clearcoatRoughness = clamp(params.clearcoatRoughness, 0.0001, 1.0);
	params.sheenRoughness = clamp(params.sheenRoughness, 0.0001, 1.0);
	params.roughness = clamp(params.roughness, 0.0001, 1.0);
	params.metallic = clamp(params.metallic, 0.0, 1.0);

	vec3 dielectricF0 = computeDielectricF0(
		params.ior,
		params.specularFactor,
		params.specularColor,
		params.useSpecGloss,
		params.specularColor
	);
	params.dielectricF0 = dielectricF0;
	params.F0 = mix(dielectricF0, params.baseColor, params.metallic);
	params.F90 = params.useSpecGloss ? vec3(1.0) : computeF90(params.metallic, params.specularFactor);

	return params;
}

// ---- KHR PBR path - IBL helpers ---------------------------------------------

void buildAnisotropyBasis(in SurfaceFrame frame, in MaterialParams params, out vec3 anisotropicT, out vec3 anisotropicB)
{
	vec2 direction = vec2(cos(params.anisotropyRotation), sin(params.anisotropyRotation));

	vec3 tangentBasisT = frame.T;
	vec3 tangentBasisB = frame.B;

	anisotropicT = normalize(direction.x * tangentBasisT + direction.y * tangentBasisB);
	anisotropicB = normalize(cross(frame.Ng, anisotropicT));
	if (length(anisotropicB) < 0.0001)
	{
		anisotropicB = normalize(cross(frame.Ng, tangentBasisT));
	}
	if (dot(cross(anisotropicT, anisotropicB), frame.Ng) < 0.0)
	{
		anisotropicB = -anisotropicB;
	}
}

vec3 sampleAnisotropicSpecularIBL(in SurfaceFrame frame, in MaterialParams params)
{
	vec3 anisotropicT;
	vec3 anisotropicB;
	buildAnisotropyBasis(frame, params, anisotropicT, anisotropicB);

	float tangentRoughness = mix(params.roughness, 1.0, params.anisotropyStrength * params.anisotropyStrength);
	vec3 anisotropicTangent = cross(anisotropicB, frame.V);
	if (length(anisotropicTangent) < 0.0001)
	{
		anisotropicTangent = cross(anisotropicB, frame.N);
	}
	anisotropicTangent = normalize(anisotropicTangent);
	vec3 anisotropicNormal = normalize(cross(anisotropicTangent, anisotropicB));
	float bendFactor = 1.0 - params.anisotropyStrength * (1.0 - params.roughness);
	float bendFactorPow4 = bendFactor * bendFactor * bendFactor * bendFactor;
	vec3 bentNormal = normalize(mix(anisotropicNormal, frame.N, bendFactorPow4));

	vec3 reflection = normalize(reflect(frame.I, bentNormal));
	reflection = normalize(envMapRotationMatrix * reflection);
	vec3 reflectionPrefilter = toPrefilterDirection(reflection);

	float maxReflectionLod = float(max(prefilterMipLevels - 1, 0));
	float lod = clamp(params.roughness * maxReflectionLod, 0.0, maxReflectionLod);
	vec3 prefilteredColor = textureLod(prefilterMap, reflectionPrefilter, lod).rgb;
	prefilteredColor = max(prefilteredColor, vec3(0.0));
	return prefilteredColor * envMapExposure;
}

vec3 transformNormalForIBL(vec3 normal)
{
	return normalize(envMapRotationMatrix * normal);
}

vec3 toPrefilterDirection(vec3 v)
{
	return vec3(v.x, -v.z, v.y);
}

float computeSheenScaling(float Ndot, float sheenRoughness)
{
	// Khronos-compliant: use sheenELUT.r (pre-integrated directional albedo E(NdotV, roughness))
	// for energy conservation. Matches albedoSheenScalingLUT() in Khronos material_info.glsl:
	//   return texture(u_SheenELUT, vec2(NdotV, sheenRoughnessFactor)).r;
	// This represents how much energy sheen removes from the base layer.
	float sheenRoughFinal = clamp(sheenRoughness, 0.000001, 1.0);
	vec2 brdfCoord = clamp(vec2(Ndot, sheenRoughFinal), vec2(0.0), vec2(1.0));
	return texture(sheenELUT, brdfCoord).r;
}

float computeCharlieBRDF(float Ndot, float sheenRoughness)
{
	// Khronos-compliant: use charlieLUT.b for the pre-integrated sheen BRDF contribution.
	// Matches getIBLRadianceCharlie() in Khronos ibl.glsl:
	//   float brdf = texture(u_CharlieLUT, brdfSamplePoint).b;
	float sheenRoughFinal = clamp(sheenRoughness, 0.000001, 1.0);
	vec2 brdfCoord = clamp(vec2(Ndot, sheenRoughFinal), vec2(0.0), vec2(1.0));
	return texture(charlieLUT, brdfCoord).b;
}

vec3 computeIBLGGXFresnelFromSample(float NdotV, float roughness, vec2 f_ab, vec3 F0, float specularWeight)
{
	vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
	vec3 kS = F0 + Fr * pow(1.0 - NdotV, 5.0);
	vec3 FssEss = specularWeight * (kS * f_ab.x + f_ab.y);

	float Ems = 1.0 - (f_ab.x + f_ab.y);
	vec3 Favg = specularWeight * (F0 + (vec3(1.0) - F0) / 21.0);
	vec3 FmsEms = Ems * FssEss * Favg / max(vec3(1.0) - Favg * Ems, vec3(0.0001));

	return FssEss + FmsEms;
}

vec3 computeAnisotropicSpecularLobe(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir)
{
	vec3 anisotropicT;
	vec3 anisotropicB;
	buildAnisotropyBasis(frame, params, anisotropicT, anisotropicB);

	vec3 H = normalize(frame.V + lightDir);
	float alphaRoughness = max(params.roughness * params.roughness, 0.001);
	float at = mix(alphaRoughness, 1.0, params.anisotropyStrength * params.anisotropyStrength);
	float ab = clamp(alphaRoughness, 0.001, 1.0);
	float NdotL = clamp(dot(frame.N, lightDir), 0.0, 1.0);
	float NdotH = clamp(dot(frame.N, H), 0.001, 1.0);
	float NdotV = clamp(dot(frame.N, frame.V), 0.0, 1.0);

	float V_aniso = V_GGX_anisotropic(
		NdotL,
		NdotV,
		dot(anisotropicB, frame.V),
		dot(anisotropicT, frame.V),
		dot(anisotropicT, lightDir),
		dot(anisotropicB, lightDir),
		at,
		ab
	);
	float D_aniso = D_GGX_anisotropic(
		NdotH,
		dot(anisotropicT, H),
		dot(anisotropicB, H),
		at,
		ab
	);

	return vec3(V_aniso * D_aniso);
}

// ---- KHR PBR path - layer composition ---------------------------------------

LayerContributions makeEmptyLayerContributions()
{
	LayerContributions layers;
	layers.baseDirectDiffuse = vec3(0.0);
	layers.baseDirectSpecular = vec3(0.0);
	layers.baseDiffuseIBL = vec3(0.0);
	layers.baseSpecularIBL = vec3(0.0);
	layers.sheenDirect = vec3(0.0);
	layers.sheenIBL = vec3(0.0);
	layers.clearcoatDirect = vec3(0.0);
	layers.clearcoatIBL = vec3(0.0);
	layers.transmission = vec3(0.0);
	layers.emissive = vec3(0.0);
	return layers;
}

vec3 sampleMappedNormal(sampler2D map, vec2 texCoord, float normalScale, vec3 Ng, vec3 T, vec3 B)
{
	vec3 tangentNormal = texture(map, texCoord).rgb * 2.0 - 1.0;
	tangentNormal.xy *= normalScale;
	tangentNormal = normalize(tangentNormal);
	return normalize(mat3(T, B, Ng) * tangentNormal);
}

vec3 composeBaseLayer(in MaterialParams params, in LayerContributions layers)
{
	return layers.emissive +
		layers.baseDirectDiffuse +
		layers.baseDirectSpecular +
		layers.baseDiffuseIBL +
		layers.baseSpecularIBL +
		layers.sheenDirect +
		layers.sheenIBL;
}

vec3 composeLayeredPBR(in SurfaceFrame frame, in MaterialParams params, in LayerContributions layers)
{
	vec3 baseColor = layers.baseDirectDiffuse + layers.baseDirectSpecular +
		layers.baseDiffuseIBL + layers.baseSpecularIBL +
		layers.sheenDirect + layers.sheenIBL;

	if (params.clearcoat <= 0.0)
	{
		return layers.emissive + baseColor;
	}

	vec3 clearcoatLayer = layers.clearcoatDirect + layers.clearcoatIBL;
	float clearcoatIor = max(params.ior, 1.0);
	float clearcoatF0Scalar = pow((clearcoatIor - 1.0) / (clearcoatIor + 1.0), 2.0);
	vec3 clearcoatFresnel = fresnelSchlick(clamp(dot(frame.Ncoat, frame.V), 0.0, 1.0), vec3(clearcoatF0Scalar), vec3(1.0));
	vec3 color = mix(baseColor, clearcoatLayer, params.clearcoat * clearcoatFresnel);
	return layers.emissive * (1.0 - params.clearcoat * clearcoatFresnel) + color;
}

// ---- KHR PBR path - punctual & direct lighting ------------------------------

vec3 evaluateSheenIBL(in SurfaceFrame frame, in MaterialParams params)
{
	// envMapEnabled deliberately NOT checked here - that flag is the legacy
	// floor/ground ADS reflection toggle (shadeBlinnPhong()'s own separate
	// "ADS reflection with exposure" block), unrelated to this KHR PBR path's
	// IBL lighting - useIBL alone is the real IBL master switch.
	if (!useIBL || length(params.sheenColor) <= 0.0)
	{
		return vec3(0.0);
	}

	float sheenRoughFinal = clamp(params.sheenRoughness, 0.000001, 1.0);
	vec3 R = reflect(frame.I, frame.N);
	R = normalize(envMapRotationMatrix * R);
	vec3 Rprefilter = toPrefilterDirection(R);

	// Use the effective mip count (not textureQueryLevels) so the LOD formula matches
	// the roughness-per-mip mapping baked into the prefilter (Khronos: 5 levels, lod=rough*4).
	// textureQueryLevels() returns 9 for a 256px cubemap → lod=0.8 at rough=0.1 (too dark).
	// With sheenPrefilterMipLevels=5 → lod = roughness*4, e.g. 0.4 at roughness=0.1.
	float maxLod = float(max(sheenPrefilterMipLevels - 1, 0));
	// Khronos-compliant LOD: lod = sheenRoughness * (mipCount - 1), no minimum clamp.
	// Matches getIBLRadianceCharlie() in Khronos ibl.glsl:
	//   float lod = sheenRoughness * float(u_MipCount - 1);
	float lod = clamp(sheenRoughFinal * maxLod, 0.0, maxLod);
	vec3 prefilteredLight = textureLod(sheenPrefilterMap, Rprefilter, lod).rgb;
	prefilteredLight = max(prefilteredLight, vec3(0.0));
	prefilteredLight *= envMapExposure;

	float NdotV = clamp(dot(frame.N, frame.V), 0.0, 1.0);
	// Use min(charlieLUT.b, sheenELUT.r) for the sheen IBL gain — symmetric with
	// iblSheenScaling — so both the energy removed from the base and the energy
	// added by the sheen IBL use the same factor, preserving energy balance.
	//
	// Why this is necessary:
	//   charlieLUT.b — old convention (alphaG = roughness, NOT squared)
	//   sheenELUT.r  — new convention (alphaG = roughness², squared)
	//
	//   Low roughness (<~0.2): charlieLUT.b > sheenELUT.r.
	//     Using charlieLUT.b for the IBL gain at roughness=0.05 (LOD=0.2, near-mirror
	//     prefilter × bright env) produces an overwhelmingly bright additive sheen
	//     contribution — the "bright glassy electric blue" on SheenDamask/ChairDamask.
	//     min = sheenELUT.r ≈ near-zero at these roughnesses → sheen IBL is
	//     proportionally tiny; no bright overlay.  Direct-light sheen is unaffected.
	//
	//   High roughness (>~0.3): charlieLUT.b < sheenELUT.r.
	//     min = charlieLUT.b → identical to the current formula; SheenHighHeel
	//     stays correct.
	//
	// The prefilter_charlie was baked with the old convention, so charlieLUT.b is
	// the "exact" split-sum factor.  Using min() intentionally underestimates at
	// low roughness to avoid the split-sum overestimate (near-mirror prefilter ×
	// non-trivially large old-convention LUT), which is the dominant source of
	// error in that regime.
	float E_charlie = computeCharlieBRDF(NdotV, sheenRoughFinal);
	float E_elut    = computeSheenScaling(NdotV, sheenRoughFinal);
	float E_sheen   = min(E_charlie, E_elut);
	// Apply AO to sheen IBL: Khronos reference applies ao to the full composed
	// color (f_sheen + base * albedoSheenScaling) * ao, so sheen receives the
	// same occlusion as the base IBL. We apply it here to match that behaviour.
	return prefilteredLight * params.sheenColor * E_sheen * kSheenStrength * params.ambientOcclusion;
}

void evaluatePunctualLight(in PunctualLight light, out vec3 lightDir, out vec3 lightIntensity)
{
	lightDir = vec3(0.0);
	lightIntensity = vec3(0.0);

	if (light.type == LightType_Directional)
	{
		lightDir = -normalize(light.direction);
		lightIntensity = light.intensity * light.color;
		return;
	}

	vec3 pointToLight = light.position - v_position;
	float distance = length(pointToLight);
	if (distance <= 1e-6)
	{
		return;
	}

	lightDir = pointToLight / distance;

	float rangeAttenuation = 1.0 / (distance * distance);
	if (light.range > 0.0)
	{
		float distAttenuation = 1.0 - pow(distance / light.range, 4.0);
		rangeAttenuation = max(min(distAttenuation, 1.0), 0.0) / (distance * distance);
	}

	float spotAttenuation = 1.0;
	if (light.type == LightType_Spot)
	{
		vec3 lightDirWorld = normalize(light.direction);
		float actualCos = dot(lightDirWorld, normalize(-pointToLight));
		if (actualCos > light.outerConeCos)
		{
			if (actualCos < light.innerConeCos)
			{
				float angularAtten = (actualCos - light.outerConeCos) / (light.innerConeCos - light.outerConeCos);
				spotAttenuation = angularAtten * angularAtten;
			}
		}
		else
		{
			spotAttenuation = 0.0;
		}
	}

	lightIntensity = rangeAttenuation * spotAttenuation * light.intensity * light.color;
}

void evaluateBaseDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor, out vec3 diffuseOut, out vec3 specularOut)
{
	float NdotL = max(dot(frame.N, lightDir), 0.0);
	float NdotLBack = max(dot(-frame.N, lightDir), 0.0);
	float NdotV = max(dot(frame.N, frame.V), 0.0);
	if (NdotL <= 0.0 && NdotLBack <= 0.0)
	{
		diffuseOut = vec3(0.0);
		specularOut = vec3(0.0);
		return;
	}

	vec3 H = normalize(frame.V + lightDir);
	float VdotH = clamp(dot(H, frame.V), 0.0, 1.0);
	vec3 dielectricDirectF0 = params.dielectricF0 * vec3(params.specularFactor);
	vec3 directF0 = mix(dielectricDirectF0, params.baseColor, params.metallic);
	float NDF = distributionGGX(frame.N, H, params.roughness);
	float G = geometrySmith(frame.N, frame.V, lightDir, params.roughness);
	vec3 specBRDFNoF = vec3((NDF * G) / max(4.0 * NdotV * max(NdotL, 0.0001), 0.001));
	vec3 F = fresnelSchlick(clamp(dot(H, frame.V), 0.0, 1.0), directF0, params.F90);
	vec3 specBRDF = specBRDFNoF * F;
	if (params.anisotropyStrength > 0.0)
	{
		specBRDF = computeAnisotropicSpecularLobe(frame, params, lightDir) * F;
		specBRDFNoF = computeAnisotropicSpecularLobe(frame, params, lightDir);
	}

	vec3 kS = F;
	vec3 kD = (vec3(1.0) - kS) * (1.0 - params.metallic);

	if (params.useSpecGloss)
	{
		vec3 l_diffuse = params.baseColor / PI * lightIntensity * NdotL * lightFactor;
		vec3 l_specular_dielectric = specBRDFNoF * lightIntensity * NdotL * lightFactor;
		vec3 dielectric_fresnel = fresnelSchlick(VdotH, params.dielectricF0, vec3(1.0));
		diffuseOut = vec3(0.0);
		specularOut = mix(l_diffuse, l_specular_dielectric, dielectric_fresnel);
		return;
	}

	diffuseOut = kD * params.baseColor / PI * lightIntensity * NdotL * lightFactor;
	specularOut = (NdotL > 0.0) ? (specBRDF * lightIntensity * NdotL * lightFactor) : vec3(0.0);

	float diffuseTransmissionThickness = computeVolumeThickness(params.thickness);
	if (!sssCapture && !interactionFastPath && params.diffuseTransmissionFactor > 0.0)
	{
		diffuseOut *= (1.0 - params.diffuseTransmissionFactor);
		if (NdotLBack > 0.0)
		{
			vec3 diffuseBTDF = lightIntensity * NdotLBack * (params.diffuseTransmissionColor / PI) * lightFactor;
			if (diffuseTransmissionThickness > 0.0)
			{
				diffuseBTDF = calculateVolumeAttenuation(
					diffuseBTDF,
					diffuseTransmissionThickness,
					diffuseTransmissionThickness,
					params.attenuationColor,
					params.attenuationDistance
				);
			}
			if (hasVolumeScattering && diffuseTransmissionThickness > 0.0)
			{
				diffuseBTDF *= (vec3(1.0) - multiToSingleScatter());
			}
			diffuseOut += diffuseBTDF * params.diffuseTransmissionFactor;
		}
	}

	if (!sssCapture && !interactionFastPath && params.transmission > 0.0)
	{
		vec3 transmittedLight = lightIntensity *
			calculateTransmissionKHR(
				frame.N,
				frame.V,
				lightDir,
				params.roughness * params.roughness,
				params.baseColor,
				params.ior) * lightFactor;
		if (diffuseTransmissionThickness > 0.0)
		{
			transmittedLight = calculateVolumeAttenuation(
				transmittedLight,
				diffuseTransmissionThickness,
				diffuseTransmissionThickness,
				params.attenuationColor,
				params.attenuationDistance
			);
		}
		diffuseOut = mix(diffuseOut, transmittedLight, params.transmission);
	}

	if (params.iridescenceFactor > 0.001 && params.iridescenceThickness > 0.0)
	{
		vec3 l_diffuse = diffuseOut;
		vec3 l_specular = (NdotL > 0.0)
			? vec3((NDF * G) / max(4.0 * NdotV * NdotL, 0.001)) * lightIntensity * NdotL * lightFactor
			: vec3(0.0);
		if (params.anisotropyStrength > 0.0 && NdotL > 0.0)
		{
			l_specular = computeAnisotropicSpecularLobe(frame, params, lightDir) * lightIntensity * NdotL * lightFactor;
		}
		vec3 dielectric_fresnel = fresnelSchlick(VdotH, dielectricDirectF0, vec3(params.specularFactor));
		vec3 metal_fresnel = fresnelSchlick(VdotH, params.baseColor, vec3(1.0));
		vec3 l_dielectric_brdf = mix(l_diffuse, l_specular, dielectric_fresnel);
		vec3 l_metal_brdf = metal_fresnel * l_specular;
		vec3 iridescenceFresnel_dielectric = evalIridescence(1.0, params.iridescenceIor, NdotV, params.iridescenceThickness, params.dielectricF0);
		vec3 iridescenceFresnel_metallic = evalIridescence(1.0, params.iridescenceIor, NdotV, params.iridescenceThickness, params.baseColor);
		l_metal_brdf = mix(l_metal_brdf, l_specular * iridescenceFresnel_metallic, params.iridescenceFactor);
		l_dielectric_brdf = mix(l_dielectric_brdf, rgb_mix(l_diffuse, l_specular, iridescenceFresnel_dielectric), params.iridescenceFactor);
		diffuseOut = vec3(0.0);
		specularOut = mix(l_dielectric_brdf, l_metal_brdf, params.metallic);
	}
}

vec3 evaluateSheenDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor)
{
	if (length(params.sheenColor) <= 0.0)
	{
		return vec3(0.0);
	}

	vec3 punctualSheen = calculateSheen(frame.N, frame.V, lightDir, params.sheenColor, params.sheenRoughness);
	return punctualSheen * lightIntensity * lightFactor * kSheenStrength;
}

void evaluateBaseIBL(in SurfaceFrame frame, in MaterialParams params, out vec3 diffuseIBLOut, out vec3 specularIBLOut)
{
	diffuseIBLOut = vec3(0.0);
	specularIBLOut = vec3(0.0);

	if (!useIBL)
	{
		return;
	}

	vec3 Nibl = transformNormalForIBL(normalize(frame.N));
	vec3 irradiance = texture(irradianceMap, Nibl).rgb * envMapExposure;
	vec3 f_diffuse = irradiance * params.baseColor;
	float diffuseTransmissionThickness = computeVolumeThickness(params.thickness);

	if (!sssCapture && !interactionFastPath && params.diffuseTransmissionFactor > 0.0)
	{
		vec3 backNormalIBL = transformNormalForIBL(normalize(-frame.N));
		vec3 diffuseTransmissionIBL = texture(irradianceMap, backNormalIBL).rgb * envMapExposure * params.diffuseTransmissionColor;
		if (diffuseTransmissionThickness > 0.0)
		{
			diffuseTransmissionIBL = calculateVolumeAttenuation(
				diffuseTransmissionIBL,
				diffuseTransmissionThickness,
				diffuseTransmissionThickness,
				params.attenuationColor,
				params.attenuationDistance
			);
		}
		if (hasVolumeScattering && diffuseTransmissionThickness > 0.0)
		{
			diffuseTransmissionIBL *= (vec3(1.0) - multiToSingleScatter());
		}
		f_diffuse = mix(f_diffuse, diffuseTransmissionIBL, params.diffuseTransmissionFactor);
	}

	// envMapEnabled deliberately NOT checked here - see evaluateSheenIBL()'s
	// identical doc comment. Always takes the full specular-reflection path
	// below (folding f_diffuse into the combined diffuseIBLOut=0/
	// specularIBLOut=mix(...) split-sum result near the end of this
	// function) rather than the cheaper diffuse-only fallback this branch
	// used to take whenever the unrelated floor/ground toggle was off.
	vec3 R = reflect(frame.I, frame.N);
	R = normalize(envMapRotationMatrix * R);
	vec3 Rprefilter = toPrefilterDirection(R);
	float maxReflectionLod = float(max(prefilterMipLevels - 1, 0));
	float lod = clamp(params.roughness * maxReflectionLod, 0.0, maxReflectionLod);
	vec3 prefilteredColor = textureLod(prefilterMap, Rprefilter, lod).rgb;
	prefilteredColor = max(prefilteredColor, vec3(0.0));
	prefilteredColor *= envMapExposure;

	if (!sssCapture && !interactionFastPath && params.transmission > 0.0)
	{
		vec3 transmittedLight = vec3(0.0);
		if (params.dispersion > 0.0)
		{
			float halfSpread = (params.ior - 1.0) * 0.025 * params.dispersion;
			vec3 iors = vec3(params.ior - halfSpread, params.ior, params.ior + halfSpread);
			transmittedLight = getIBLVolumeRefractionPerChannel(
				frame.N,
				frame.V,
				params.roughness,
				params.baseColor,
				v_position,
				modelMatrix,
				iors,
				params.thickness,
				params.attenuationColor,
				params.attenuationDistance
			);
		}
		else
		{
			transmittedLight = getIBLVolumeRefraction(
				frame.N,
				frame.V,
				params.roughness,
				params.baseColor,
				v_position,
				modelMatrix,
				params.ior,
				params.thickness,
				params.attenuationColor,
				params.attenuationDistance
			);
		}
		f_diffuse = mix(f_diffuse, transmittedLight, params.transmission);
	}

	vec3 f_specular_metal = prefilteredColor;
	vec3 f_specular_dielectric = prefilteredColor;
	if (params.anisotropyStrength > 0.0)
	{
		f_specular_metal = sampleAnisotropicSpecularIBL(frame, params);
		f_specular_dielectric = f_specular_metal;
	}
	if (params.useSpecGloss)
	{
		float NdotV_ibl = clamp(dot(frame.N, frame.V), 0.0, 1.0);
		vec2 brdfSamplePoint = clamp(vec2(NdotV_ibl, params.roughness), vec2(0.0), vec2(1.0));
		vec2 f_ab = max(texture(brdfLUT, brdfSamplePoint).rg, vec2(0.0));
		vec3 f_dielectric_fresnel_ibl = computeIBLGGXFresnelFromSample(
			NdotV_ibl, params.roughness, f_ab, params.dielectricF0, 1.0);
		diffuseIBLOut = vec3(0.0);
		specularIBLOut = mix(f_diffuse, f_specular_dielectric, f_dielectric_fresnel_ibl) * params.ambientOcclusion;
		return;
	}
	float NdotV_ibl = clamp(dot(frame.N, frame.V), 0.0, 1.0);
	vec2 brdfSamplePoint = clamp(vec2(NdotV_ibl, params.roughness), vec2(0.0), vec2(1.0));
	vec2 f_ab = max(texture(brdfLUT, brdfSamplePoint).rg, vec2(0.0));
	vec3 f_metal_fresnel_ibl = computeIBLGGXFresnelFromSample(
		NdotV_ibl, params.roughness, f_ab, params.baseColor, 1.0);
	vec3 f_metal_brdf_ibl = f_metal_fresnel_ibl * f_specular_metal;
	vec3 f_dielectric_fresnel_ibl = computeIBLGGXFresnelFromSample(
		NdotV_ibl, params.roughness, f_ab, params.dielectricF0, params.specularFactor);
	vec3 f_dielectric_brdf_ibl = mix(f_diffuse, f_specular_dielectric, f_dielectric_fresnel_ibl);

	if (params.iridescenceFactor > 0.001 && params.iridescenceThickness > 0.0)
	{
		float NdotV = clamp(dot(frame.N, frame.V), 0.0, 1.0);
		vec3 iridescenceFresnel_dielectric = evalIridescence(1.0, params.iridescenceIor, NdotV, params.iridescenceThickness, params.dielectricF0);
		vec3 iridescenceFresnel_metallic = evalIridescence(1.0, params.iridescenceIor, NdotV, params.iridescenceThickness, params.baseColor);
		f_metal_brdf_ibl = mix(f_metal_brdf_ibl, f_specular_metal * iridescenceFresnel_metallic, params.iridescenceFactor);
		f_dielectric_brdf_ibl = mix(f_dielectric_brdf_ibl, rgb_mix(f_diffuse, f_specular_dielectric, iridescenceFresnel_dielectric), params.iridescenceFactor);
	}

	diffuseIBLOut = vec3(0.0);
	specularIBLOut = mix(f_dielectric_brdf_ibl, f_metal_brdf_ibl, params.metallic) * params.ambientOcclusion;
}

vec3 evaluateClearcoatDirect(in SurfaceFrame frame, in MaterialParams params, in vec3 lightDir, in vec3 lightIntensity, float lightFactor)
{
	if (params.clearcoat <= 0.0)
	{
		return vec3(0.0);
	}

	vec3 V = frame.V;
	vec3 L = normalize(lightDir);
	vec3 H = normalize(L + V);
	float NdotL = max(dot(frame.Ncoat, L), 0.0);
	float NdotV = max(dot(frame.Ncoat, V), 0.0);
	float NdotH = max(dot(frame.Ncoat, H), 0.0);
	if (NdotL <= 0.0 || NdotV <= 0.0)
	{
		return vec3(0.0);
	}

	float alpha = params.clearcoatRoughness * params.clearcoatRoughness;
	float D = distributionGGX(frame.Ncoat, H, alpha);
	float G = geometrySmith(frame.Ncoat, V, L, alpha);
	vec3 clearcoatBRDF = vec3((D * G) / max(4.0 * NdotV, 0.001));
	return lightIntensity * clearcoatBRDF * params.clearcoat * lightFactor;
}

vec3 evaluateClearcoatIBL(in SurfaceFrame frame, in MaterialParams params)
{
	// envMapEnabled deliberately NOT checked here - see evaluateSheenIBL()'s doc comment.
	if (params.clearcoat <= 0.0 || !useIBL)
	{
		return vec3(0.0);
	}

	vec3 R = reflect(frame.I, frame.Ncoat);
	R = normalize(envMapRotationMatrix * R);
	vec3 Rprefilter = toPrefilterDirection(R);

	float maxReflectionLod = float(max(prefilterMipLevels - 1, 0));
	float lod = clamp(params.clearcoatRoughness * maxReflectionLod, 0.0, maxReflectionLod);
	vec3 prefilteredColor = textureLod(prefilterMap, Rprefilter, lod).rgb;
	prefilteredColor = max(prefilteredColor, vec3(0.0));
	prefilteredColor *= envMapExposure;
	return prefilteredColor * params.clearcoat * params.ambientOcclusion;
}

// ---- Shading entry points ---------------------------------------------------

// ========== LEGACY BLINN-PHONG SHADING FUNCTION ==========
vec4 shadeBlinnPhong(LightSource source, LightModel model, Material mat, vec3 position, vec3 normal)
{
	vec2 clippedTexCoord = v_texCoord0;
	float ambientOcclusion = 1.0;

	if (hasAOMap)
	{
		float texAO = samplePackedChannelValue(
			aoMap,
			hasAOMap,
			getAOUV(),
			aoChannel,
			aoInvert,
			aoScale,
			aoBias,
			1.0
		);
		ambientOcclusion = clamp(mix(1.0, texAO, pbrLighting.occlusionStrength), 0.0001, 1.0);
	}

	// --- Normal / Parallax (same as before) ---
	if (shadingNormalMode != 1 && hasNormalTexture)
		normal = calcBumpedNormal(texture_normal, getNormalTextureUV());

	if (shadingNormalMode != 1 && hasHeightTexture)
	{
		clippedTexCoord = applyParallaxMapping(getHeightUV(), texture_height, heightScale, hasHeightTexture);
		normal = calcBumpedNormal(normalMap, clippedTexCoord);
	}

	// --- Lighting vectors ---
	// ADS stays in view space: the active camera convention already rotates the
	// scene into the desired basis, so the inspection light remains the legacy
	// camera-aligned +Z direction.
	vec3 lightDir, viewDir;
	vec3 inspectionDir = normalize(vec3(0.0, 0.0, 1.0));

	viewDir = inspectionDir;
	lightDir = inspectionDir;

	vec3 halfVector = normalize(lightDir + viewDir);
	float nDotVP = max(dot(normal, normalize(lightDir + viewDir)), 0.0);
	float nDotHV = max(0.0, dot(normal, halfVector));
	float pf = pow(nDotHV, mat.shininess);

	// --- Material terms ---
	vec3 matAmbient = mat.ambient;
	vec3 matDiffuse = mat.diffuse;
	vec3 matSpecular = mat.specular * pf;
	vec3 matEmissive = mat.emission;

	if (hasDiffuseTexture)
	{
		vec3 d = texture(texture_diffuse, getDiffuseTextureUV()).rgb;
		matAmbient *= d;
		matDiffuse *= d;
	}

	if (hasVertexColors)
		matDiffuse *= v_color.rgb;

	if (length(normal) < 0.01)
	{
		// No vertex normals: compute face normal from screen-space position derivatives
		vec3 dx = dFdx(v_position);
		vec3 dy = dFdy(v_position);
		vec3 faceN = cross(dx, dy);
		if (length(faceN) < 0.0001)
			return vec4(matDiffuse, 1.0); // Degenerate primitive (point/line): return flat color
		normal = normalize(faceN);
	}

	// Geometry term
	matDiffuse  *= nDotVP;

	if (hasSpecularTexture)
	{
		// ADS specular is cascaded from metallic texture
		// Combine with roughness for proper metallic-roughness behavior:
		// specular = metallic × (1 - roughness)
		float metallicValue = texture(texture_specular, getSpecularTextureUV()).r;
		float roughnessValue = 0.5; // Default if no roughness texture

		// If roughness texture is available, use it to modulate specularity
		if (hasRoughnessMap)
		{
			roughnessValue = texture(roughnessMap, getRoughnessUV()).r;
		}

		// Combine: metallic determines if reflective, roughness determines intensity
		float specularity = metallicValue * (1.0 - roughnessValue);
		matSpecular = vec3(specularity) * pf;
	}
	if (hasEmissiveTexture)
		matEmissive = texture(texture_emissive, getEmissiveTextureUV()).rgb;

	// --- Build lighting buckets ---
	vec3 ambient = source.ambient * matAmbient * model.ambient * ambientOcclusion;
	vec3 diffuse = vec3(0.0);
	vec3 specular = vec3(0.0);
	
	if (useDefaultLights || floorRendering)
	{
		diffuse = source.diffuse * matDiffuse;
		specular = source.specular * matSpecular;
	}

	vec3 baseNoSpec, specOnly;
	vec3 sceneColor = matEmissive + ambient;

	if (useDefaultLights && shadowsEnabled && realismEnabled &&
		(selfShadowsEnabled || floorRendering || hasVolumeScattering))
	{
		float shadowFactor = calculateShadowVariableKernel(
			fs_in_shadow.FragPosLightSpace,
			fs_in_shadow.FragPos,
			fs_in_shadow.lightPos
		);
		shadowFactor = clamp(shadowFactor, 0.0, 0.7);
		float lightFactor = 1.0 - shadowFactor;

		vec3 ambientContrib = ambient * 0.6;
		vec3 directDiffuse = lightFactor * diffuse;
		vec3 directSpecular = lightFactor * specular;

		baseNoSpec = sceneColor + ambientContrib + directDiffuse;
		specOnly = directSpecular;
	}
	else
	{
		baseNoSpec = sceneColor + diffuse;
		specOnly = specular;
	}

	// --- Floor override (non-reflected) ---
	// Ensure the floor is actually translucent even if its material is OPAQUE
	if (floorRendering && !isReflectedPass)
	{
		float fa = clamp(floorAlpha, 0.0, 1.0);

		// View-angle term to avoid "whiteout" when looking straight down.
		// Analytical world-space floor normal — consistent regardless of camera rotation.
		vec3 Nf = (worldUpAxis == 1) ? vec3(0.0, -1.0, 0.0) : vec3(0.0, 0.0, -1.0);
		vec3 Vf = normalize(cameraPos - v_position);
		float NdotVf = abs(dot(Nf, Vf));
		// Fresnel-like dampening of spec when NdotV is high (looking straight down)
		float fresDampen = mix(1.0 - floorFresnelDampen, 1.0, pow(1.0 - NdotVf, 5.0));

		// Scale specular; keep non-spec premultiplied
		vec3 floorRGB = baseNoSpec * fa + specOnly * (floorSpecularScale * fresDampen);

		if (hdrToneMapping) floorRGB = applyToneMapping(floorRGB);
		if (gammaCorrection) floorRGB = pow(floorRGB, vec3(1.0 / screenGamma));
		return vec4(floorRGB, fa);
	}

	vec3 composed;
	composed = baseNoSpec + specOnly;

	// ADS reflection with exposure
	if (useIBL && envMapEnabled)
	{
		vec3 I = normalize(cameraDir);
		vec3 N = getUnsignedWorldGeometryNormal();
		vec3 offset = normalize(cameraPos - v_reflectionPosition);
		vec3 I_offset = normalize(I - offset * 0.3);  // Blend factor adjustable
		vec3 R = reflect(-I_offset, N);
		R = envMapRotationMatrix * -R;
		R = normalize(R);

		float specLum = dot(material.specular, vec3(0.299, 0.587, 0.114));
		float diffuseLum = dot(material.diffuse, vec3(0.299, 0.587, 0.114));

		// Derive metallic-like factor from ADS: metals have high spec/low diff
		float adsMetallic = specLum / (specLum + diffuseLum + 0.001);
		adsMetallic = clamp(adsMetallic, 0.0, 1.0);

		float NdotV = max(dot(-I, N), 0.0);

		// Gentler fresnel powers
		float nonMetallicFresnelPower = mix(2.0, 3.5, 1.0 - specLum); // Reduced
		float metallicFresnelPower = 1.2; // Reduced
		float fresnelPower = mix(nonMetallicFresnelPower, metallicFresnelPower, adsMetallic);
		float fresnel = pow(1.0 - NdotV, fresnelPower);

		// Limit grazing angle effect
		float grazingLimit = mix(0.6, 0.9, adsMetallic); // Metals can handle more
		fresnel = clamp(fresnel, 0.0, grazingLimit);

		float surfaceRoughness = 1.0 - (material.shininess / 128.0);
		float roughnessReduction = pow(1.0 - surfaceRoughness, 2.0);

		// Reduced base strengths
		float metallicStrength = mix(0.2, 0.4, specLum);
		float glossyStrength = mix(0.25, 0.5, specLum);
		float diffuseStrength = mix(0.02, 0.12, specLum);

		bool isHighSpecular = specLum > 0.5;
		bool isDiffuseDominant = dot(material.diffuse, vec3(0.299, 0.587, 0.114)) > specLum * 2.0;
		float nonMetallicStrength = isHighSpecular && !isDiffuseDominant ? glossyStrength : diffuseStrength;
		float baseReflectionStrength = mix(nonMetallicStrength, metallicStrength, adsMetallic);

		// Additional roughness damping for very rough surfaces
		float roughnessDamping = mix(0.3, 1.0, 1.0 - surfaceRoughness);
		float reflectionStrength = clamp(baseReflectionStrength * fresnel * roughnessReduction * roughnessDamping, 0.0, 0.5);

		// === IMPROVED GRAZING LOD ===
		// Material-aware grazing influence: smooth metals/mirrors avoid extra blur
		float grazingInfluence = surfaceRoughness * (1.0 - adsMetallic * 0.8);

		// Grazing factor for LOD adjustment
		float grazingFactor = pow(1.0 - NdotV, mix(1.8, 2.5, surfaceRoughness));

		// Base LOD from roughness
		float baseLOD = surfaceRoughness * 7.0;

		// Extra grazing blur, but only for rough dielectrics
		float grazingLOD = grazingFactor * grazingInfluence * 4.0; // Scaled to match 7.0 max

		// Combine: mirrors/smooth metals keep sharp reflections
		float envLOD = clamp(baseLOD + grazingLOD, 0.0, 7.0);
		// ========================

		vec3 envColor = textureLod(envMap, R, envLOD).rgb;
		composed += envColor * reflectionStrength * ambientOcclusion;
	}

	// --- Tone/gamma ---
	if (hdrToneMapping)
	{
		composed *= envMapExposure;
		composed = applyToneMapping(composed);
	}
	if (gammaCorrection)
		composed = pow(composed, vec3(1.0 / screenGamma));

	return vec4(composed, 1.0);
}

vec4 calculatePBRLightingKHR(int renderMode, float side)
{
	vec2 normalUV = getNormalUV();
	if (hasHeightMap)
	{
		normalUV = applyParallaxMapping(getNormalUV(), heightMap, heightScale, hasHeightMap);
	}

	vec2 clearcoatNormalUV = getClearcoatNormalUV();
	if (hasHeightMap)
	{
		clearcoatNormalUV = applyParallaxMapping(getClearcoatNormalUV(), heightMap, heightScale, hasHeightMap);
	}

	MaterialParams params = gatherMaterialParams();
	SurfaceFrame frame = buildSurfaceFrame(side, normalUV, clearcoatNormalUV, lightSource.position - v_position);
	LayerContributions layers = makeEmptyLayerContributions();
	layers.emissive = params.emissive;

	if (params.unlit)
	{
		vec3 unlitColor = params.baseColor + params.emissive;
		if (hdrToneMapping) unlitColor = applyToneMapping(unlitColor);
		if (gammaCorrection) unlitColor = pow(unlitColor, vec3(1.0 / screenGamma));
		return vec4(unlitColor, 1.0);
	}

	float lightShadowFactor = 0.0;
	if (useDefaultLights && shadowsEnabled && realismEnabled &&
		(selfShadowsEnabled || floorRendering || hasVolumeScattering))
	{
		float s = calculateShadowVariableKernel(
			fs_in_shadow.FragPosLightSpace,
			fs_in_shadow.FragPos,
			fs_in_shadow.lightPos
		);
		lightShadowFactor = clamp(s, 0.0, 0.85);
	}
	float lightFactor = 1.0 - lightShadowFactor;

	if (hasPunctualLights && usePunctualLights)
	{
		// Punctual lights have no shadow map; the shadow factor (lightFactor) is
		// computed from the default directional light's shadow map and must not
		// attenuate punctual light contributions.
		const float punctualLightFactor = 1.0;
		for (int i = 0; i < lightCount; ++i)
		{
			vec3 lightDir;
			vec3 lightIntensity;
			evaluatePunctualLight(lights[i], lightDir, lightIntensity);

			if (max3(lightIntensity) <= 0.0)
			{
				continue;
			}

			vec3 directDiffuse;
			vec3 directSpecular;
			evaluateBaseDirect(frame, params, lightDir, lightIntensity, punctualLightFactor, directDiffuse, directSpecular);

			if (length(params.sheenColor) > 0.0)
			{
				float sheenStrength = max3(params.sheenColor) * kSheenStrength;
				float NdotV = clamp(dot(frame.N, frame.V), 0.0, 1.0);
				float NdotL = clamp(dot(frame.N, lightDir), 0.0, 1.0);
				// Khronos-compliant: min(1-E(NdotV), 1-E(NdotL)) for punctual lights.
				// Matches Khronos pbr.frag lines 341-342.
				float l_albedoSheenScaling = min(
					1.0 - sheenStrength * computeSheenScaling(NdotV, params.sheenRoughness),
					1.0 - sheenStrength * computeSheenScaling(NdotL, params.sheenRoughness));
				directDiffuse *= l_albedoSheenScaling;
				directSpecular *= l_albedoSheenScaling;
				layers.sheenDirect += evaluateSheenDirect(frame, params, lightDir, lightIntensity, punctualLightFactor);
			}

			if (params.clearcoat > 0.0)
			{
				layers.clearcoatDirect += evaluateClearcoatDirect(frame, params, lightDir, lightIntensity, punctualLightFactor);
			}

			layers.baseDirectDiffuse += directDiffuse;
			layers.baseDirectSpecular += directSpecular;
		}
	}

	if (useDefaultLights || floorRendering)
	{
		vec3 lightIntensity = lightSource.diffuse;

		if (max3(lightIntensity) > 0.0)
		{
			vec3 directDiffuse;
			vec3 directSpecular;
			evaluateBaseDirect(frame, params, frame.L, lightIntensity, lightFactor, directDiffuse, directSpecular);

			if (length(params.sheenColor) > 0.0)
			{
				float sheenStrength = max3(params.sheenColor) * kSheenStrength;
				float NdotV = clamp(dot(frame.N, frame.V), 0.0, 1.0);
				float NdotL = clamp(dot(frame.N, frame.L), 0.0, 1.0);
				// Khronos-compliant: min(1-E(NdotV), 1-E(NdotL)) for punctual lights.
				// Matches Khronos pbr.frag lines 341-342.
				float l_albedoSheenScaling = min(
					1.0 - sheenStrength * computeSheenScaling(NdotV, params.sheenRoughness),
					1.0 - sheenStrength * computeSheenScaling(NdotL, params.sheenRoughness));
				directDiffuse *= l_albedoSheenScaling;
				directSpecular *= l_albedoSheenScaling;
				layers.sheenDirect += evaluateSheenDirect(frame, params, frame.L, lightIntensity, lightFactor);
			}

			if (params.clearcoat > 0.0)
			{
				layers.clearcoatDirect += evaluateClearcoatDirect(frame, params, frame.L, lightIntensity, lightFactor);
			}

			layers.baseDirectDiffuse += directDiffuse;
			layers.baseDirectSpecular += directSpecular;
		}
	}

	evaluateBaseIBL(frame, params, layers.baseDiffuseIBL, layers.baseSpecularIBL);

	float iblSheenScaling = 1.0;
	if (length(params.sheenColor) > 0.0)
	{
		float sheenStrength = max3(params.sheenColor) * kSheenStrength;
		float NdotV_sheen = clamp(dot(frame.N, frame.V), 0.0, 1.0);
		// IBL sheen energy conservation: use min(charlieLUT.b, sheenELUT.r).
		//
		// Two BRDFs are in play for the sheen IBL:
		//   charlieLUT.b  — baked with D_Charlie(alphaG = roughness,  NOT squared) + V_Ashikhmin
		//   sheenELUT.r   — baked with D_Charlie(alphaG = roughness², squared)     + V_Sheen
		//
		// These conventions cross over with roughness:
		//   High roughness (>~0.3): sheenELUT.r >> charlieLUT.b
		//     → sheenELUT.r removes far more from base than charlieLUT.b × prefilteredSheenLight
		//       restores → near-black zebra stripes on fabric normal maps.
		//     → min = charlieLUT.b → balanced removal; fixes SheenHighHeel.
		//
		//   Low roughness (<~0.2): charlieLUT.b > sheenELUT.r
		//     (old convention is broader; new convention is near-delta at extreme grazing → tiny)
		//     → charlieLUT.b over-removes, making base too dark; sheenELUT.r ≈ 0 is more correct.
		//     → min = sheenELUT.r ≈ 0 → base barely affected; fixes SheenDamask (roughness=0.05).
		//
		// min(charlieLUT.b, sheenELUT.r) is always ≤ both → never over-darkens for either
		// roughness regime. At medium-high roughness the two terms converge so the min is
		// the tighter (charlieLUT.b) bound, keeping the split-sum self-consistent.
		// Punctual-light sheen scaling keeps sheenELUT.r (correct: runtime V_Sheen BRDF).
		float E_charlie = computeCharlieBRDF(NdotV_sheen, params.sheenRoughness);
		float E_sheen   = computeSheenScaling(NdotV_sheen, params.sheenRoughness);
		iblSheenScaling = 1.0 - sheenStrength * min(E_charlie, E_sheen);
	}

	// envMapEnabled deliberately NOT checked here - see evaluateSheenIBL()'s doc comment.
	if (useIBL && length(params.sheenColor) > 0.0)
	{
		layers.sheenIBL = evaluateSheenIBL(frame, params);
	}

	if (params.clearcoat > 0.0)
	{
		layers.clearcoatIBL = evaluateClearcoatIBL(frame, params);
	}

	layers.baseDiffuseIBL *= iblSheenScaling;
	layers.baseSpecularIBL *= iblSheenScaling;

	// SSS capture mode: store total incident irradiance × scatter ratio into the SSS FBO.
	// sampleCapturedSSSDiffuse() blurs this with the Burley kernel and adds it to outRGB.
	//
	// Design rationale for the omnidirectional capture:
	//   A translucent surface with diffuseTransmissionFactor=1 lets light enter from BOTH
	//   faces.  Sampling only frame.N (or weighting the back face less) makes side/rear
	//   fragments always dark — the Burley kernel (~10 px radius) cannot carry front-face
	//   brightness across a 300 px skull.  Instead we average the front and back hemisphere
	//   IBL so every fragment sees the full omnidirectional environment, and for direct
	//   lights we sum NdotLFront + NdotLBack so a back-lit point is not discarded.
	//   diffuseTransmissionColor is intentionally left out here; sampleCapturedSSSDiffuse
	//   applies it at blur time to avoid squaring it for coloured transmission materials.
	if (sssCapture)
	{
		vec3 captureColor = vec3(0.0);
		if (hasVolumeScattering)
		{
			vec3 singleScatter = multiToSingleScatter();
			float dtFactor = max(params.diffuseTransmissionFactor, 0.0);

			// ---- IBL: average of front + back hemisphere -------------------------
			// Both hemispheres contribute equally — incident light enters from either face.
			if (useIBL)
			{
				vec3 frontIrr = texture(irradianceMap, transformNormalForIBL(normalize( frame.N))).rgb;
				vec3 backIrr  = texture(irradianceMap, transformNormalForIBL(normalize(-frame.N))).rgb;
				captureColor += (frontIrr + backIrr) * 0.5 * envMapExposure;
			}

			// ---- Direct lights: sum front + back NdotL ---------------------------
			// Summing both sides means a side/rear fragment also captures light that
			// is reaching its inner face — which the Burley blur will scatter outward.
			if (useDefaultLights || floorRendering)
			{
				float NdotLFront = max(dot( frame.N, frame.L), 0.0);
				float NdotLBack  = max(dot(-frame.N, frame.L), 0.0);
				captureColor += lightSource.diffuse * (NdotLFront + NdotLBack) / PI;
			}
			if (hasPunctualLights && usePunctualLights)
			{
				for (int i = 0; i < lightCount; ++i)
				{
					vec3 lightDir;
					vec3 lightIntensity;
					evaluatePunctualLight(lights[i], lightDir, lightIntensity);
					if (max3(lightIntensity) <= 0.0)
						continue;
					float NdotLFront = max(dot( frame.N, lightDir), 0.0);
					float NdotLBack  = max(dot(-frame.N, lightDir), 0.0);
					captureColor += lightIntensity * (NdotLFront + NdotLBack) / PI;
				}
			}

			// Scale by scatter ratio and transmission factor
			captureColor *= singleScatter * dtFactor;

			// NOTE: Volume Beer-Lambert attenuation is intentionally NOT applied here.
			// That attenuation (pow(attenuationColor, thickness/attenuationDistance)) is
			// for direct transmission rays crossing the full slab — it is already applied
			// in evaluateBaseDirect and evaluateBaseIBL for the unscattered component.
			// The scattered SSS component's spatial decay is handled by burleyEval()
			// inside sampleCapturedSSSDiffuse(); adding full-thickness Beer-Lambert on top
			// would drive captureColor to zero for any real skull thickness.
		}
		return vec4(captureColor, sssObjectId);
	}

	vec3 outRGB = composeLayeredPBR(frame, params, layers);

	if (hasVolumeScattering && !interactionFastPath)
	{
		float diffuseTransmissionThickness = computeVolumeThickness(params.thickness);
		if (params.diffuseTransmissionFactor > 0.0 && diffuseTransmissionThickness > 0.0)
		{
			vec3 subsurface = sampleCapturedSSSDiffuse(params.attenuationDistance, params.diffuseTransmissionColor);
			float sssWeight = (1.0 - params.metallic) * (1.0 - params.transmission);
			float sssVisibility = 1.0;
			if (useDefaultLights && shadowsEnabled)
			{
				// Let shadows read clearly on top of the SSS resolve instead of letting
				// the gathered scatter fully wash out the shadowed side of the model.
				sssVisibility = lightFactor;
			}
			outRGB += subsurface * sssWeight * sssVisibility;
		}
	}

	if (floorRendering && !isReflectedPass)
	{
		float fa = clamp(floorAlpha, 0.0, 1.0);
		// Floor normal faces downward (away from camera); use abs() so top-down view gives
		// NdotVf ≈ 1 (minimum dampen) and grazing view gives NdotVf ≈ 0 (full dampen).
		float NdotVf = abs(dot(frame.Ng, frame.V));
		float fresDampen = mix(1.0 - floorFresnelDampen, 1.0, pow(1.0 - NdotVf, 5.0));
		vec3 floorBase = (layers.emissive + layers.sheenDirect + layers.sheenIBL +
			layers.baseDirectDiffuse +
			layers.baseDiffuseIBL) * fa;
		vec3 floorSpec = (layers.baseDirectSpecular +
			layers.baseSpecularIBL) * (floorSpecularScale * fresDampen);
		vec3 floorRGB = floorBase + floorSpec;
		if (hdrToneMapping) floorRGB = applyToneMapping(floorRGB);
		if (gammaCorrection) floorRGB = pow(floorRGB, vec3(1.0 / screenGamma));
		return vec4(floorRGB, fa);
	}

	if (hdrToneMapping) outRGB = applyToneMapping(outRGB);
	if (gammaCorrection) outRGB = pow(outRGB, vec3(1.0 / screenGamma));
	return vec4(outRGB, 1.0);
}

// ----------------------------------------------------------------------------
// Calculate PBR lighting based on the render mode
vec4 calculatePBRLighting(int renderMode, float side) // side 1 = front, -1 = back
{
	return calculatePBRLightingKHR(renderMode, side);
}
