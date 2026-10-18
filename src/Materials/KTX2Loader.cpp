#include "KTX2Loader.h"
#include <fstream>
#include <QDebug>
#include <QOpenGLContext>
#include <cstring>

#ifndef GL_COMPRESSED_RGBA_BC7_UNORM
#define GL_COMPRESSED_RGBA_BC7_UNORM 0x8E8D
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB 0x8E8F
#endif

KTX2Loader::KTX2Loader()
{
}

KTX2Loader::~KTX2Loader()
{
}

bool KTX2Loader::initializeOpenGL()
{
    // Initialize the OpenGL function pointers
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
    {
        qWarning() << "No OpenGL context current";
        return false;
    }

    // Initialize function pointers
    initializeOpenGLFunctions();
    glInitialized = true;

    // Initialize basisu transcoder (only once)
    if (!basisuInitialized)
    {
        basist::basisu_transcoder_init();
        basisuInitialized = true;
        qDebug() << "Basisu transcoder initialized";
    }

    return true;
}

bool KTX2Loader::readFileToMemory(const std::string& filePath, std::vector<uint8_t>& outData)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        qWarning() << "Failed to open KTX2 file:" << QString::fromStdString(filePath);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    outData.resize(size);
    if (!file.read(reinterpret_cast<char*>(outData.data()), size))
    {
        qWarning() << "Failed to read KTX2 file data";
        return false;
    }

    qDebug() << "Loaded KTX2 file" << QString::fromStdString(filePath)
        << "(" << (size / 1024 / 1024) << "MB)";
    return true;
}

GPUCapabilities KTX2Loader::detectGPUCapabilities()
{
    GPUCapabilities caps;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
    {
        qWarning() << "No OpenGL context for capability detection";
        return caps;
    }

    // Create a local object to call member functions
    QOpenGLFunctions_4_5_Core functions;
    functions.initializeOpenGLFunctions();

    // Call glGetString through the object
    const GLubyte* extensionsPtr = functions.glGetString(GL_EXTENSIONS);
    const char* extensions = (const char*)extensionsPtr;

    if (extensions)
    {
        std::string extString(extensions);

        if (extString.find("GL_EXT_texture_compression_s3tc") != std::string::npos)
        {
            caps.supportsBC7 = true;
            qDebug() << "GPU supports BC7";
        }
        if (extString.find("GL_KHR_texture_compression_astc_ldr") != std::string::npos)
        {
            caps.supportsASTC = true;
            qDebug() << "GPU supports ASTC";
        }
        if (extString.find("GL_ARB_texture_compression_bptc") != std::string::npos)
        {
            caps.supportsBC6H = true;
            qDebug() << "GPU supports BC6H";
        }
    }

    if (!caps.supportsBC7 && !caps.supportsASTC)
    {
        qDebug() << "GPU doesn't support BC7 or ASTC";
    }

    return caps;
}

basist::transcoder_texture_format KTX2Loader::selectCompressionFormat(
    const basist::ktx2_transcoder& transcoder,
    const GPUCapabilities& gpuCaps)
{
    // Simple approach: apply user's compression mode preference uniformly to all textures
    // Default is UNCOMPRESSED_RGBA32 for best quality
    // User can switch to BC7 or ASTC for smaller memory footprint
    // Note: Proper texture-type-specific compression will be implemented later

    switch (compressionMode)
    {
    case CompressionMode::UNCOMPRESSED_RGBA32:
        qDebug() << "Using RGBA32 uncompressed (default - best quality)";
        return basist::transcoder_texture_format::cTFRGBA32;

    case CompressionMode::BC7_COMPRESSED:
        if (gpuCaps.supportsBC7)
        {
            qDebug() << "Using BC7 compressed (user preference)";
            return basist::transcoder_texture_format::cTFBC7_RGBA;
        }
        qDebug() << "BC7 not supported, falling back to RGBA32";
        return basist::transcoder_texture_format::cTFRGBA32;

    case CompressionMode::BC7_ALT_COMPRESSED:
        if (gpuCaps.supportsBC7)
        {
            qDebug() << "Using BC7 alternative compressed (user preference)";
            // Note: Basisu doesn't have a specific "alternative" BC7 mode,
			// so we use the standard BC7 format here.
            return basist::transcoder_texture_format::cTFBC7_RGBA;
        }
		qDebug() << "BC7 not supported, falling back to RGBA32";
		return basist::transcoder_texture_format::cTFBC7_ALT;

    case CompressionMode::ASTC_COMPRESSED:
        if (gpuCaps.supportsASTC)
        {
            qDebug() << "Using ASTC 4x4 compressed (user preference)";
            return basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
        }
        qDebug() << "ASTC not supported, falling back to RGBA32";
        return basist::transcoder_texture_format::cTFRGBA32;

    default:
        return basist::transcoder_texture_format::cTFRGBA32;
    }
}

uint32_t KTX2Loader::calculateBlockCount(uint32_t width, uint32_t height,
    basist::transcoder_texture_format format)
{
    switch (format)
    {
    case basist::transcoder_texture_format::cTFBC7_RGBA:
    case basist::transcoder_texture_format::cTFASTC_4x4_RGBA:
    case basist::transcoder_texture_format::cTFBC6H:
    {
        // 4x4 blocks
        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;
        return blocksX * blocksY;
    }

    case basist::transcoder_texture_format::cTFRGBA32:
        // No blocks - direct pixel data
        return width * height;

    case basist::transcoder_texture_format::cTFRGBA_HALF:
        // Half-float RGBA
        return width * height;

    default:
        qDebug() << "Unknown format in calculateBlockCount";
        return 0;
    }
}

void KTX2Loader::setGLFormatInfo(TranscodedTexture& texture)
{
    texture.isCompressed = false;

    switch (texture.format)
    {
    case basist::transcoder_texture_format::cTFBC7_RGBA:
        texture.glInternalFormat = GL_COMPRESSED_RGBA_BC7_UNORM;
        texture.glFormat = GL_RGBA;
        texture.glType = GL_UNSIGNED_BYTE;
        texture.isCompressed = true;
        break;

    case basist::transcoder_texture_format::cTFASTC_4x4_RGBA:
        texture.glInternalFormat = GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        texture.glFormat = GL_RGBA;
        texture.glType = GL_UNSIGNED_BYTE;
        texture.isCompressed = true;
        break;

    case basist::transcoder_texture_format::cTFBC6H:
        texture.glInternalFormat = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB;
        texture.glFormat = GL_RGB;
        texture.glType = GL_UNSIGNED_BYTE;
        texture.isCompressed = true;
        break;

    case basist::transcoder_texture_format::cTFRGBA32:
        texture.glInternalFormat = GL_RGBA8;
        texture.glFormat = GL_RGBA;
        texture.glType = GL_UNSIGNED_BYTE;
        texture.isCompressed = false;
        break;

    case basist::transcoder_texture_format::cTFRGBA_HALF:
        texture.glInternalFormat = GL_RGBA16F;
        texture.glFormat = GL_RGBA;
        texture.glType = GL_HALF_FLOAT;
        texture.isCompressed = false;
        break;

    default:
        qDebug() << "Unknown format in setGLFormatInfo";
        texture.glInternalFormat = GL_RGBA8;
        texture.glFormat = GL_RGBA;
        texture.glType = GL_UNSIGNED_BYTE;
        break;
    }
}

bool KTX2Loader::transcodeMipmapLevel(
    basist::ktx2_transcoder& transcoder,
    uint32_t levelIndex,
    basist::transcoder_texture_format format,
    std::vector<uint8_t>& outData)
{
    // Get dimensions for this mipmap level
    uint32_t width = transcoder.get_width();
    uint32_t height = transcoder.get_height();

    // Account for mipmap scaling
    for (uint32_t i = 0; i < levelIndex; i++)
    {
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }

    uint32_t blockCount = calculateBlockCount(width, height, format);
    if (blockCount == 0)
    {
        qDebug() << "Invalid block count for mipmap level" << levelIndex;
        return false;
    }

    // Allocate output buffer
    uint32_t requiredSize = 0;
    if (format == basist::transcoder_texture_format::cTFRGBA32)
    {
        requiredSize = blockCount * 4;  // 4 bytes per pixel
    }
    else if (format == basist::transcoder_texture_format::cTFRGBA_HALF)
    {
        requiredSize = blockCount * 8;  // 8 bytes per pixel (4 half-floats)
    }
    else
    {
        // Compressed formats: 16 bytes per 4x4 block
        requiredSize = blockCount * 16;
    }

    outData.resize(requiredSize);

    // Transcode the level
    // Use cDecodeFlagsHighQuality flag (value=32) for ASTC only
    // Note: The quality flag is optimized for BC1, BC3, ETC2, and ASTC formats
    // It does NOT improve BC7 and may actually degrade it
    uint32_t transcodeFlags = 0;
    if (format == basist::transcoder_texture_format::cTFBC7_RGBA || 
        format == basist::transcoder_texture_format::cTFASTC_4x4_RGBA ||
        format == basist::transcoder_texture_format::cTFBC7_ALT)
    {
        transcodeFlags = basist::cDecodeFlagsHighQuality;  // Better quality
        qDebug() << "Applying cDecodeFlagsHighQuality";
    }
    // BC7 transcodes best without the quality flag

    bool status = transcoder.transcode_image_level(
        levelIndex,         // level index (0, 1, 2, ...)
        0,                  // layer index  
        0,                  // face index
        outData.data(),
        blockCount,
        format,
        transcodeFlags,     // Quality flags (ASTC only)
        0,                  // output_row_pitch_in_blocks_or_pixels
        0)                  // output_rows_in_pixels
        ;

    if (!status)
    {
        qDebug() << "Failed to transcode mipmap level" << levelIndex;
        return false;
    }

    return true;
}

bool KTX2Loader::loadKTX2(
    const std::string& filePath,
    TranscodedTexture& outTexture,
    const GPUCapabilities& gpuCaps,
    const std::string& mapType)
{
    if (!glInitialized)
    {
        qWarning() << "KTX2Loader not initialized - call initializeOpenGL() first";
        return false;
    }

    qDebug() << "\n=== Loading KTX2 File ===";

    // Step 1: Load file into memory
    std::vector<uint8_t> ktx2Data;
    if (!readFileToMemory(filePath, ktx2Data))
    {
        return false;
    }

    // Step 2: Create and initialize transcoder
    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(ktx2Data.data(), ktx2Data.size()))
    {
        qDebug() << "Failed to initialize KTX2 transcoder";
        return false;
    }

    // Step 3: Query texture properties
    uint32_t width = transcoder.get_width();
    uint32_t height = transcoder.get_height();
    uint32_t numLevels = transcoder.get_levels();

    qDebug() << "KTX2 Properties:";
    qDebug() << "  Dimensions:" << width << "x" << height;
    qDebug() << "  Mipmap levels:" << numLevels;
    qDebug() << "  Is HDR:" << (transcoder.is_hdr() ? "Yes" : "No");

    // Step 4: Select compression format based on user preference
    basist::transcoder_texture_format selectedFormat = selectCompressionFormat(transcoder, gpuCaps);

    // Step 5: Start transcoding
    transcoder.start_transcoding();

    // Step 6: Transcode first mipmap level (main texture)
    std::vector<uint8_t> levelData;
    if (!transcodeMipmapLevel(transcoder, 0, selectedFormat, levelData))
    {
        return false;
    }

    // Step 7: Fill output structure
    outTexture.data = levelData;
    outTexture.width = width;
    outTexture.height = height;
    outTexture.mipLevels = numLevels;
    outTexture.format = selectedFormat;

    // Step 8: Set OpenGL format information
    setGLFormatInfo(outTexture);

    // Step 9: Calculate memory info
    uint32_t compressedSize = levelData.size();
    uint32_t uncompressedSize = width * height * 4;  // RGBA uncompressed
    float compressionRatio = (float)compressedSize / uncompressedSize * 100.0f;

    qDebug() << "Transcoding Results:";
    qDebug() << "  Format:" << (int)selectedFormat;
    qDebug() << "  Output size:" << (compressedSize / 1024) << "KB";
    qDebug() << "  Uncompressed equivalent:" << (uncompressedSize / 1024) << "KB";
    qDebug() << "  Compression ratio:" << compressionRatio << "%";

    qDebug() << "KTX2 loading SUCCESSFUL";
    return true;
}

GLuint KTX2Loader::uploadToGPU(const TranscodedTexture& texture)
{
    if (!glInitialized)
    {
        qWarning() << "KTX2Loader not initialized";
        return 0;
    }

    qDebug() << "\n=== Uploading Texture to GPU ===";

    // Create OpenGL texture
    GLuint textureID = 0;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // NOTE: Samplers are NOT set here - they are set by the caller
    // This allows the caller to apply glTF sampler settings

    // Upload texture data
    if (texture.isCompressed)
    {
        // Compressed format
        qDebug() << "Uploading compressed texture (internalFormat:" << texture.glInternalFormat << ")";

        glCompressedTexImage2D(
            GL_TEXTURE_2D,
            0,  // mipmap level
            texture.glInternalFormat,
            texture.width,
            texture.height,
            0,  // border
            texture.data.size(),
            texture.data.data()
        );
    }
    else
    {
        // Uncompressed format
        qDebug() << "Uploading uncompressed texture (format:" << texture.glFormat << ")";

        glTexImage2D(
            GL_TEXTURE_2D,
            0,  // mipmap level
            texture.glInternalFormat,
            texture.width,
            texture.height,
            0,  // border
            texture.glFormat,
            texture.glType,
            texture.data.data()
        );
    }

    // Let GPU generate remaining mipmaps
    glGenerateMipmap(GL_TEXTURE_2D);

    // Unbind texture
    glBindTexture(GL_TEXTURE_2D, 0);

    qDebug() << "Texture uploaded to GPU, ID:" << textureID;
    return textureID;
}
