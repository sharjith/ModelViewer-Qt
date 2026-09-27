#pragma once


#include <QDialog>
#include <QVector2D>
#include <glm/glm.hpp>
#include <QOpenGLFunctions_4_5_Core>

namespace Ui {
class TextureParametersDialog;
}

class TextureParametersDialog : public QDialog
{
    Q_OBJECT

public:
    // Structure for sampler settings
    struct SamplerSettings
    {
        GLenum wrapS = GL_REPEAT;
        GLenum wrapT = GL_REPEAT;
        GLenum magFilter = GL_LINEAR;
        GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR;

        bool operator==(const SamplerSettings& other) const
        {
            return wrapS == other.wrapS &&
                   wrapT == other.wrapT &&
                   magFilter == other.magFilter &&
                   minFilter == other.minFilter;
        }
    };

    // Structure for texture transform (from Material)
    struct TextureTransform
    {
        int texCoord = 0;
        QVector2D texScale = QVector2D(1.0f, 1.0f);
        QVector2D texOffset = QVector2D(0.0f, 0.0f);
        float texRotation = 0.0f;
    };

    // Constructor
    explicit TextureParametersDialog(QWidget* parent = nullptr);
    ~TextureParametersDialog();

    // Set texture type in dialog title
    void setTextureType(const QString& textureType);

    // Set and get transform parameters
    void setTransform(const TextureTransform& transform);
    TextureTransform getTransform() const;

    // Set and get sampler settings
    void setSamplerSettings(const SamplerSettings& samplers);
    SamplerSettings getSamplerSettings() const;

    // Convenience methods to set all parameters at once
    void setAllParameters(const TextureTransform& transform, const SamplerSettings& samplers);
    
    struct AllParameters
    {
        TextureTransform transform;
        SamplerSettings samplers;
    };
    AllParameters getAllParameters() const;

private slots:
    // Dialog button slots (auto-connected in UI)
    // accept() and reject() are handled by Qt

private:
    // UI initialization helper
    void initializeSamplerMappings();

    // OpenGL enum to friendly name conversions
    QString wrapModeToString(GLenum mode) const;
    GLenum stringToWrapMode(const QString& str) const;
    
    QString filterToString(GLenum filter, bool isMag = true) const;
    GLenum stringToFilter(const QString& str, bool isMag = true) const;

    Ui::TextureParametersDialog* _ui;
    QString _currentTextureType;
};
