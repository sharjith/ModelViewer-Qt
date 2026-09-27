#pragma once

#include <QHash>
#include <QMap>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
// types.h (not just matrix4x4.h) is required: matrix4x4.h only *declares*
// aiMatrix4x4t<float>'s constructors, while their actual inline definitions
// live in matrix4x4.inl, which only types.h pulls in. Without it, this header
// compiles fine (relying on some other translation unit to have included
// types.h and emit the weak inline symbols this one's undefined references
// bind to at link time) but is fragile - whether the link actually succeeds
// depends on unrelated Debug/Release inlining choices elsewhere in the binary.
#include <assimp/types.h>

enum class GltfAnimationTargetPath
{
    Translation,
    Rotation,
    Scale,
    Weights,
    Pointer
};

enum class GltfAnimationBindingTargetKind
{
    Node,
    Mesh
};

enum class GltfAnimationPointerProperty
{
    None,
    Offset,
    Scale,
    Rotation,
    Visibility,
    BaseColorFactor
};

enum class GltfAnimationPointerTargetKind
{
    None,
    MaterialTextureTransform,
    NodeVisibility
};

enum class GltfAnimationTextureTarget
{
    Unknown,
    Albedo,
    Metallic,
    Roughness,
    MetallicRoughness,
    Normal,
    Occlusion,
    Emissive,
    Transmission,
    Thickness,
    IOR,
    SheenColor,
    SheenRoughness,
    Clearcoat,
    ClearcoatRoughness,
    ClearcoatNormal,
    Iridescence,
    IridescenceThickness,
    SpecularFactor,
    SpecularColor,
    Anisotropy,
    DiffuseTransmission,
    DiffuseTransmissionColor,
    Diffuse,
    SpecularGlossiness
};

struct GltfAnimationVec3Key
{
    double    timeSeconds = 0.0;
    QVector3D value;
};

struct GltfAnimationVec4Key
{
    double    timeSeconds = 0.0;
    QVector4D value;
};

struct GltfAnimationQuatKey
{
    double      timeSeconds = 0.0;
    QQuaternion value;
};

struct GltfAnimationVec2Key
{
    double    timeSeconds = 0.0;
    QVector2D value;
};

struct GltfAnimationFloatKey
{
    double timeSeconds = 0.0;
    float  value = 0.0f;
};

struct GltfAnimationBoolKey
{
    double timeSeconds = 0.0;
    bool   value = false;
};

struct GltfAnimationWeightsKey
{
    double         timeSeconds = 0.0;
    QVector<float> values;
};

struct GltfAnimationChannel
{
    GltfAnimationBindingTargetKind targetKind = GltfAnimationBindingTargetKind::Node;
    QString                 targetNodeName;
    int                     targetNodeIndex = -1;
    QUuid                   targetMeshUuid;
    GltfAnimationTargetPath targetPath = GltfAnimationTargetPath::Translation;
    QString                 targetPointer;
    GltfAnimationPointerTargetKind pointerTargetKind = GltfAnimationPointerTargetKind::None;
    int                     targetMaterialIndex = -1;
    GltfAnimationTextureTarget textureTarget = GltfAnimationTextureTarget::Unknown;
    GltfAnimationPointerProperty pointerProperty = GltfAnimationPointerProperty::None;
    QVector<GltfAnimationVec3Key> vec3Keys;
    QVector<GltfAnimationVec4Key> vec4Keys;
    QVector<GltfAnimationQuatKey> quatKeys;
    QVector<GltfAnimationVec2Key> vec2Keys;
    QVector<GltfAnimationFloatKey> floatKeys;
    QVector<GltfAnimationBoolKey> boolKeys;
    QVector<GltfAnimationWeightsKey> weightKeys;
};

struct GltfAnimationClip
{
    QString                     name;
    double                      durationSeconds = 0.0;
    bool                        hasNodeTransforms = false;
    bool                        hasSkinning = false;
    bool                        hasMorphAnimations = false;
    bool                        hasPointerAnimations = false;
    QVector<GltfAnimationChannel> channels;
};

struct GltfSkinJoint
{
    QString     nodeName;
    aiMatrix4x4 inverseBindMatrix;
};

struct GltfAnimationNodeVisibilityState
{
    int     nodeIndex = -1;
    int     parentNodeIndex = -1;
    QString nodeName;
    bool    defaultVisible = true;
};

struct GltfAnimationNodeBinding
{
    int     nodeIndex = -1;
    QString nodeName;
    bool    hasAiChildPath = false;
    QVector<int> aiChildPath;
};

struct GltfAnimationLightBinding
{
    int     parsedLightIndex = -1;
    int     lightDefinitionIndex = -1;
    int     nodeIndex = -1;
    QString nodeName;
};

struct GltfAnimationData
{
    QString sourceFile;
    QVector<GltfAnimationClip> clips;
    bool hasNodeAnimations = false;
    bool hasSkinning = false;
    bool hasMorphAnimations = false;
    bool hasPointerAnimations = false;
    QVector<GltfAnimationNodeBinding> nodeBindings;
    QVector<GltfAnimationNodeVisibilityState> nodeVisibilityStates;
    QVector<GltfAnimationLightBinding> lightBindings;
    aiMatrix4x4 rootInverseTransform;

    bool isEmpty() const { return clips.isEmpty(); }
};
