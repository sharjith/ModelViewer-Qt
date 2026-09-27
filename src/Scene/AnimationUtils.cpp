#include "AnimationUtils.h"

#include <algorithm>

namespace AnimationUtils
{

QVector3D sampleVec3Keys(const QVector<GltfAnimationVec3Key>& keys,
                          double timeSeconds, const QVector3D& fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            return keys[i - 1].value * (1.0f - t) + keys[i].value * t;
        }
    }
    return keys.back().value;
}

QVector2D sampleVec2Keys(const QVector<GltfAnimationVec2Key>& keys,
                          double timeSeconds, const QVector2D& fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            return keys[i - 1].value * (1.0f - t) + keys[i].value * t;
        }
    }
    return keys.back().value;
}

float sampleFloatKeys(const QVector<GltfAnimationFloatKey>& keys,
                       double timeSeconds, float fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            return keys[i - 1].value * (1.0f - t) + keys[i].value * t;
        }
    }
    return keys.back().value;
}

QVector4D sampleVec4Keys(const QVector<GltfAnimationVec4Key>& keys,
                          double timeSeconds, const QVector4D& fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            return keys[i - 1].value * (1.0f - t) + keys[i].value * t;
        }
    }
    return keys.back().value;
}

bool sampleBoolKeys(const QVector<GltfAnimationBoolKey>& keys,
                     double timeSeconds, bool fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    bool result = keys.front().value;
    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds < keys[i].timeSeconds)
            return result;
        result = keys[i].value;
    }
    return result;
}

QVector<float> sampleWeightKeys(const QVector<GltfAnimationWeightsKey>& keys,
                                  double timeSeconds, const QVector<float>& fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().values;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().values;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            const int count = std::max(keys[i - 1].values.size(), keys[i].values.size());
            QVector<float> result(count, 0.0f);
            for (int w = 0; w < count; ++w)
            {
                const float sv = w < keys[i - 1].values.size() ? keys[i - 1].values[w] : 0.0f;
                const float ev = w < keys[i].values.size()     ? keys[i].values[w]     : sv;
                result[w] = sv * (1.0f - t) + ev * t;
            }
            return result;
        }
    }
    return keys.back().values;
}

QQuaternion sampleQuatKeys(const QVector<GltfAnimationQuatKey>& keys,
                             double timeSeconds, const QQuaternion& fallback)
{
    if (keys.isEmpty())
        return fallback;
    if (timeSeconds <= keys.front().timeSeconds)
        return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds)
        return keys.back().value;

    for (int i = 1; i < keys.size(); ++i)
    {
        if (timeSeconds <= keys[i].timeSeconds)
        {
            const double start = keys[i - 1].timeSeconds;
            const double end   = keys[i].timeSeconds;
            const float  t     = end > start
                ? static_cast<float>((timeSeconds - start) / (end - start)) : 0.0f;
            return QQuaternion::slerp(keys[i - 1].value, keys[i].value, t).normalized();
        }
    }
    return keys.back().value;
}

} // namespace AnimationUtils
