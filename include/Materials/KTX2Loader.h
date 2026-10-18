#pragma once

#include <basisu/transcoder/basisu_transcoder.h>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLContext>
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

// User-selectable compression mode for rendering quality
enum class CompressionMode
{
    UNCOMPRESSED_RGBA32,    // Best quality (default)
    BC7_COMPRESSED,         // Good quality, smaller memory
	BC7_ALT_COMPRESSED,     // Alternative BC7 mode
    ASTC_COMPRESSED         // Good quality, smaller memory
};

// Represents a transcoded texture ready for GPU upload
struct TranscodedTexture
{
    std::vector<uint8_t> data;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;

    // The format the texture was transcoded to
    basist::transcoder_texture_format format;

    // OpenGL format (depends on transcoded format)
    GLint glInternalFormat;
    GLenum glFormat;
    GLenum glType;
    bool isCompressed;
};

// GPU format capability
struct GPUCapabilities
{
    bool supportsBC7 = false;
    bool supportsASTC = false;
    bool supportsBC6H = false;

    // Returns the best format for this GPU
    basist::transcoder_texture_format getBestFormat() const
    {
        if (supportsBC7)
            return basist::transcoder_texture_format::cTFBC7_RGBA;
        if (supportsASTC)
            return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        return basist::transcoder_texture_format::cTFRGBA32;
    }
};

class KTX2Loader : protected QOpenGLFunctions_4_5_Core
{
public:
    KTX2Loader();
    ~KTX2Loader();

    // Initialize OpenGL context (call once after OpenGL context is current)
    bool initializeOpenGL();

    // Load and transcode a KTX2 file
    // Returns true and fills outTexture on success
    // NOTE: Does NOT apply samplers - samplers are set separately by caller
    bool loadKTX2(
        const std::string& filePath,
        TranscodedTexture& outTexture,
        const GPUCapabilities& gpuCaps,
        const std::string& mapType = "baseColor"
    );

    // Upload transcoded texture to GPU
    // Returns OpenGL texture ID on success, 0 on failure
    // NOTE: Caller is responsible for setting samplers afterward
    GLuint uploadToGPU(const TranscodedTexture& texture);

    // Detect GPU capabilities (queries OpenGL)
    static GPUCapabilities detectGPUCapabilities();

    // Set compression mode (affects quality vs memory trade-off)
    void setCompressionMode(CompressionMode mode)
    {
        compressionMode = mode;
        qDebug() << "KTX2 compression mode set to:" << (int)mode;
    }

    // Get current compression mode
    CompressionMode getCompressionMode() const
    {
        return compressionMode;
    }

private:
    bool basisuInitialized = false;
    bool glInitialized = false;
    CompressionMode compressionMode = CompressionMode::UNCOMPRESSED_RGBA32;

    // Helper to read file into memory
    bool readFileToMemory(const std::string& filePath, std::vector<uint8_t>& outData);

    // Select transcoding format based on user's compression mode preference
    // Default is uncompressed. User can choose BC7 or ASTC for smaller memory.
    basist::transcoder_texture_format selectCompressionFormat(
        const basist::ktx2_transcoder& transcoder,
        const GPUCapabilities& gpuCaps
    );

    // Transcode a single mipmap level
    // Uses cTFHighQuality flag for BC7/ASTC to improve compression quality
    bool transcodeMipmapLevel(
        basist::ktx2_transcoder& transcoder,
        uint32_t levelIndex,
        basist::transcoder_texture_format format,
        std::vector<uint8_t>& outData
    );

    // Calculate block count for a given format
    uint32_t calculateBlockCount(uint32_t width, uint32_t height,
        basist::transcoder_texture_format format);

    // Set OpenGL format information based on transcoding format
    void setGLFormatInfo(TranscodedTexture& texture);
};
