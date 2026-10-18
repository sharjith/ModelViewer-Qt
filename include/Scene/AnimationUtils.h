#pragma once

#include "GltfAnimationData.h"

#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

// ---------------------------------------------------------------------------
// AnimationUtils
//
// Stateless helpers for animation key sampling and Assimp/glTF data conversion.
// All functions are pure — they operate only on their arguments.
// ---------------------------------------------------------------------------
namespace AnimationUtils
{

// ---- glTF key samplers -----------------------------------------------------

QVector3D sampleVec3Keys(const QVector<GltfAnimationVec3Key>& keys,
                          double timeSeconds, const QVector3D& fallback);

QVector2D sampleVec2Keys(const QVector<GltfAnimationVec2Key>& keys,
                          double timeSeconds, const QVector2D& fallback);

float sampleFloatKeys(const QVector<GltfAnimationFloatKey>& keys,
                       double timeSeconds, float fallback);

QVector4D sampleVec4Keys(const QVector<GltfAnimationVec4Key>& keys,
                          double timeSeconds, const QVector4D& fallback);

bool sampleBoolKeys(const QVector<GltfAnimationBoolKey>& keys,
                     double timeSeconds, bool fallback);

QVector<float> sampleWeightKeys(const QVector<GltfAnimationWeightsKey>& keys,
                                  double timeSeconds, const QVector<float>& fallback);

QQuaternion sampleQuatKeys(const QVector<GltfAnimationQuatKey>& keys,
                             double timeSeconds, const QQuaternion& fallback);

} // namespace AnimationUtils
