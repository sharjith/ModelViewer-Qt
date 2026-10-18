#pragma once
class SceneMesh;


#include <QString>
#include <QMap>
#include <vector>

class RenderableMesh;
/**
 * @class TextureMetadata
 * @brief Tracks metadata for a single texture through the packaging process
 */
struct TextureMetadata
{
    QString originalPath;           ///< Original path from material
    QString resolvedPath;           ///< Absolute path after resolution
    QString outputName;             ///< Filename in output texture directory
    QString relativePath;           ///< Relative path for glTF export
    bool isEmbedded = false;        ///< Whether embedded in glTF
    uint64_t fileSize = 0;
    QByteArray hash;                ///< SHA256 for deduplication
};

/**
 * @class TexturePackage
 * @brief Container for all textures after packaging
 */
struct TexturePackage
{
    std::vector<TextureMetadata> textures;  ///< All packaged textures
    QMap<QString, QString> pathMapping;     ///< Original path -> output relative path
    QString textureDirectory;               ///< Absolute path to output textures directory
    QString textureSubfolder;               ///< Leaf folder name (e.g. "MyModel_textures")
    uint64_t totalSize = 0;                 ///< Total size of all textures
    int duplicatesRemoved = 0;              ///< Count of duplicates eliminated
};

/**
 * @class TextureLocationManager
 * @brief Handles texture path resolution, copying, deduplication, and packaging
 *
 * This class ensures all textures referenced by materials are properly located,
 * copied to a standard output directory, and referenced with portable relative paths.
 *
 * Features:
 * - Multi-level path resolution (absolute, relative, CWD)
 * - Automatic texture file copying
 * - Content-based deduplication (SHA256 hashing)
 * - Collision handling (automatic renaming)
 * - Portable relative path generation
 */
class TextureLocationManager
{
public:
    explicit TextureLocationManager();
    ~TextureLocationManager() = default;

    /**
     * @brief Resolve a texture path to an absolute, verified path
     * @param path Original texture path (may be relative or absolute)
     * @param baseDir Base directory for relative path resolution (optional)
     * @return TextureMetadata with resolved path or empty if not found
     */
    TextureMetadata resolveTexture(const QString& path,
        const QString& baseDir = "");

    /**
     * @brief Package all textures from a set of meshes
     *
     * This performs the complete packaging workflow:
     * 1. Collects all unique textures from all materials
     * 2. Resolves each texture path
     * 3. Detects and removes duplicates
     * 4. Copies files to output directory with collision handling
     * 5. Generates relative paths for portability
     *
     * @param meshes Vector of SceneMesh pointers to process
     * @param outputDirectory Base directory for texture output
     * @return TexturePackage with all metadata and path mappings
     */
    TexturePackage packageTextures(const std::vector<SceneMesh*>& meshes,
        const QString& outputDirectory,
        const QString& textureSubfolder);

    /**
     * @brief Check if two files are identical (by SHA256 hash)
     * @param path1 First file path
     * @param path2 Second file path
     * @return true if files have identical content
     */
    bool areFilesIdentical(const QString& path1, const QString& path2);

private:
    /**
     * @brief Compute SHA256 hash of a file
     * @param path File to hash
     * @return SHA256 hash as QByteArray, empty if file cannot be read
     */
    QByteArray hashFile(const QString& path);

    /**
     * @brief Generate a unique filename to avoid collisions
     *
     * If the original filename exists in the directory, appends a number
     * suffix before the file extension. E.g. "texture.png" -> "texture_1.png"
     *
     * @param directory Directory to check for collisions
     * @param originalFilename Original filename
     * @return Unique filename guaranteed not to exist in directory
     */
    QString generateUniqueFilename(const QString& directory,
        const QString& originalFilename);

    /**
     * @brief Extract an embedded texture from a GLB/GLTF source file.
     *
     * Handles "glb://filepath::image_N" URIs.  The first call for a given
     * source file loads all of its embedded textures into a per-session temp
     * directory and caches the results.  Subsequent calls for the same
     * (file, index) pair are served from the cache without re-loading.
     *
     * @param glbFilePath  Absolute path to the source .glb / .gltf file
     * @param imageIndex   Zero-based embedded texture index within that file
     * @param outTempPath  Receives the absolute path of the extracted temp file
     * @return true on success, false if the source cannot be read or index is out of range
     */
    bool extractGlbEmbeddedTexture(const QString& glbFilePath,
                                   int            imageIndex,
                                   QString&       outTempPath);

    /// Cache: "filepath::N" -> absolute path of the extracted temp file.
    /// Populated lazily by extractGlbEmbeddedTexture().
    QMap<QString, QString> _glbEmbeddedCache;
};
