#include "TextureParametersDialog.h"
#include "ui_TextureParametersDialog.h"
#include "LanguageManager.h"

#include <QOpenGLFunctions_4_5_Core>

TextureParametersDialog::TextureParametersDialog(QWidget* parent)
    : QDialog(parent)
    , _ui(new Ui::TextureParametersDialog)
    , _currentTextureType("Unknown")
{
    _ui->setupUi(this);

    // Initialize sampler dropdown mappings
    initializeSamplerMappings();

    // Set reasonable window properties - tr() wrapped (previously a bare
    // string literal, so this title never appeared in the .ts files at all
    // and stayed in English regardless of the app's language setting).
    setWindowTitle(tr("Texture Parameters"));

    // See ExplodedViewPanel's/ClippingPlanesEditor's identical connection -
    // without this, a live language switch in Settings left this dialog
    // showing whatever language was active at construction until the next
    // app restart.
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        _ui->retranslateUi(this);
        setWindowTitle(tr("Texture Parameters"));
        });

    connect(_ui->btnDefault, &QPushButton::clicked, this, [this]() {
        // Reset to default values
        TextureTransform defaultTransform;
        SamplerSettings defaultSamplers;
        setAllParameters(defaultTransform, defaultSamplers);
		});
}

TextureParametersDialog::~TextureParametersDialog()
{
    delete _ui;
}

void TextureParametersDialog::setTextureType(const QString& textureType)
{
    _currentTextureType = textureType;
    
    // Format: "Metallic Texture Parameters" from "metallic"
    QString displayName = textureType;
    if (!displayName.isEmpty())
    {
        displayName[0] = displayName[0].toUpper();
    }
    
    setWindowTitle(displayName + " Texture Parameters");
}

// ============================================================================
// Transform Parameter Methods
// ============================================================================

void TextureParametersDialog::setTransform(const TextureTransform& transform)
{
    _ui->spinScaleU->blockSignals(true);
    _ui->spinScaleV->blockSignals(true);
    _ui->spinOffsetU->blockSignals(true);
    _ui->spinOffsetV->blockSignals(true);
    _ui->spinRotation->blockSignals(true);
    _ui->spinTexCoord->blockSignals(true);

    _ui->spinScaleU->setValue(transform.texScale.x());
    _ui->spinScaleV->setValue(transform.texScale.y());
    _ui->spinOffsetU->setValue(transform.texOffset.x());
    _ui->spinOffsetV->setValue(transform.texOffset.y());
	_ui->spinRotation->setValue(transform.texRotation * 180.0f / M_PI);
    _ui->spinTexCoord->setValue(transform.texCoord);

    _ui->spinScaleU->blockSignals(false);
    _ui->spinScaleV->blockSignals(false);
    _ui->spinOffsetU->blockSignals(false);
    _ui->spinOffsetV->blockSignals(false);
    _ui->spinRotation->blockSignals(false);
    _ui->spinTexCoord->blockSignals(false);
}

TextureParametersDialog::TextureTransform TextureParametersDialog::getTransform() const
{
    TextureTransform transform;
    transform.texScale = QVector2D(_ui->spinScaleU->value(), _ui->spinScaleV->value());
    transform.texOffset = QVector2D(_ui->spinOffsetU->value(), _ui->spinOffsetV->value());
    transform.texRotation = static_cast<float>(_ui->spinRotation->value()) * M_PI / 180.0f;
    transform.texCoord = _ui->spinTexCoord->value();
    return transform;
}

// ============================================================================
// Sampler Settings Methods
// ============================================================================

void TextureParametersDialog::setSamplerSettings(const SamplerSettings& samplers)
{
    _ui->comboWrapS->blockSignals(true);
    _ui->comboWrapT->blockSignals(true);
    _ui->comboMagFilter->blockSignals(true);
    _ui->comboMinFilter->blockSignals(true);

    _ui->comboWrapS->setCurrentText(wrapModeToString(samplers.wrapS));
    _ui->comboWrapT->setCurrentText(wrapModeToString(samplers.wrapT));
    _ui->comboMagFilter->setCurrentText(filterToString(samplers.magFilter, true));
    _ui->comboMinFilter->setCurrentText(filterToString(samplers.minFilter, false));

    _ui->comboWrapS->blockSignals(false);
    _ui->comboWrapT->blockSignals(false);
    _ui->comboMagFilter->blockSignals(false);
    _ui->comboMinFilter->blockSignals(false);
}

TextureParametersDialog::SamplerSettings TextureParametersDialog::getSamplerSettings() const
{
    SamplerSettings samplers;
    samplers.wrapS = stringToWrapMode(_ui->comboWrapS->currentText());
    samplers.wrapT = stringToWrapMode(_ui->comboWrapT->currentText());
    samplers.magFilter = stringToFilter(_ui->comboMagFilter->currentText(), true);
    samplers.minFilter = stringToFilter(_ui->comboMinFilter->currentText(), false);
    return samplers;
}

// ============================================================================
// Combined Parameters Methods
// ============================================================================

void TextureParametersDialog::setAllParameters(const TextureTransform& transform,
                                               const SamplerSettings& samplers)
{
    setTransform(transform);
    setSamplerSettings(samplers);
}

TextureParametersDialog::AllParameters TextureParametersDialog::getAllParameters() const
{
    AllParameters params;
    params.transform = getTransform();
    params.samplers = getSamplerSettings();
    return params;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void TextureParametersDialog::initializeSamplerMappings()
{
    // Wrap modes are already populated in UI
    // Set default selections
    _ui->comboWrapS->setCurrentIndex(0); // Repeat
    _ui->comboWrapT->setCurrentIndex(0); // Repeat
    _ui->comboMagFilter->setCurrentIndex(0); // Linear (Smooth)
    _ui->comboMinFilter->setCurrentIndex(0); // Trilinear (Best)
}

// ============================================================================
// Wrap Mode Conversions
// ============================================================================

QString TextureParametersDialog::wrapModeToString(GLenum mode) const
{
    switch (mode)
    {
        case GL_REPEAT:
            return "Repeat";
        case GL_CLAMP_TO_EDGE:
            return "Clamp";
        case GL_MIRRORED_REPEAT:
            return "Mirror";
        default:
            return "Repeat"; // Default fallback
    }
}

GLenum TextureParametersDialog::stringToWrapMode(const QString& str) const
{
    if (str == "Clamp")
        return GL_CLAMP_TO_EDGE;
    else if (str == "Mirror")
        return GL_MIRRORED_REPEAT;
    else // Default to "Repeat"
        return GL_REPEAT;
}

// ============================================================================
// Filter Conversions
// ============================================================================

QString TextureParametersDialog::filterToString(GLenum filter, bool isMag) const
{
    if (isMag)
    {
        // Magnification filter
        switch (filter)
        {
            case GL_LINEAR:
                return "Linear (Smooth)";
            case GL_NEAREST:
                return "Nearest (Pixelated)";
            default:
                return "Linear (Smooth)"; // Default fallback
        }
    }
    else
    {
        // Minification filter
        switch (filter)
        {
            case GL_LINEAR_MIPMAP_LINEAR:
                return "Trilinear (Best)";
            case GL_LINEAR_MIPMAP_NEAREST:
                return "Linear Mipmap";
            case GL_NEAREST_MIPMAP_LINEAR:
                return "Nearest Mipmap";
            case GL_NEAREST:
                return "Nearest";
            default:
                return "Trilinear (Best)"; // Default fallback
        }
    }
}

GLenum TextureParametersDialog::stringToFilter(const QString& str, bool isMag) const
{
    if (isMag)
    {
        // Magnification filter
        if (str == "Nearest (Pixelated)")
            return GL_NEAREST;
        else
            return GL_LINEAR; // Default to Linear
    }
    else
    {
        // Minification filter
        if (str == "Linear Mipmap")
            return GL_LINEAR_MIPMAP_NEAREST;
        else if (str == "Nearest Mipmap")
            return GL_NEAREST_MIPMAP_LINEAR;
        else if (str == "Nearest")
            return GL_NEAREST;
        else
            return GL_LINEAR_MIPMAP_LINEAR; // Default to Trilinear
    }
}
