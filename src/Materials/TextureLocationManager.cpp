#include "TextureLocationManager.h"
#include "SceneMesh.h"
#include "Material.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDebug>
#include <QUrl>
#include <set>
#include <map>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

TextureLocationManager::TextureLocationManager()
{
}

TextureMetadata TextureLocationManager::resolveTexture(const QString& path,
    const QString& baseDir)
{
    TextureMetadata meta;
    meta.originalPath = path;

    if (path.isEmpty())
    {
        return meta;  // Return empty metadata for empty path
    }

    // Step 0: Handle "glb://filepath::image_N" embedded-texture URIs.
    // These are not real disk paths — the texture lives inside a GLB binary.
    // Extract it to a session-scoped temp file on first access and cache the result.
    if (path.startsWith("glb://") && path.contains("::"))
    {
        // Parse  "glb://D:/path/model.glb::image_3"
        //         ^^^^^^ scheme  ^^^^^^^^ filepath  ^^^^^^^ image name
        const QString afterScheme = path.mid(6);           // strip "glb://"
        const int sepPos          = afterScheme.lastIndexOf("::");
        const QString glbFilePath = afterScheme.left(sepPos);
        const QString imageName   = afterScheme.mid(sepPos + 2); // e.g. "image_3"

        // Extract the numeric index from "image_N"
        int imageIndex = -1;
        const int underPos = imageName.lastIndexOf('_');
        if (underPos >= 0)
        {
            bool ok = false;
            imageIndex = imageName.mid(underPos + 1).toInt(&ok);
            if (!ok) imageIndex = -1;
        }

        if (imageIndex >= 0 && QFile::exists(glbFilePath))
        {
            QString tempPath;
            if (extractGlbEmbeddedTexture(glbFilePath, imageIndex, tempPath))
            {
                meta.resolvedPath = tempPath;
                meta.fileSize     = static_cast<uint64_t>(QFileInfo(tempPath).size());
                meta.hash         = hashFile(tempPath);
                return meta;
            }
        }

        // Could not resolve — return empty metadata
        qWarning() << "[TextureLocationManager] Could not extract embedded texture:" << path;
        return meta;
    }

    auto finalizeResolvedPath = [&](const QString& candidatePath, const QString& mode) -> bool
    {
        QFileInfo fi(candidatePath);
        if (!fi.exists() || !fi.isFile() || !fi.isReadable())
            return false;

        meta.resolvedPath = fi.absoluteFilePath();
        meta.fileSize = static_cast<uint64_t>(fi.size());
        meta.hash = hashFile(meta.resolvedPath);
        qDebug() << QString("[TextureLocationManager] Resolved (%1):").arg(mode)
            << path << "->" << meta.resolvedPath;
        return true;
    };

    const QString decodedPath = QUrl::fromPercentEncoding(path.toUtf8());
    const bool hasDecodedVariant = (decodedPath != path);

    // Step 1: Try path as provided, then a percent-decoded variant.
    if (finalizeResolvedPath(path, "direct"))
        return meta;
    if (hasDecodedVariant && finalizeResolvedPath(decodedPath, "percent-decoded"))
        return meta;

    // Step 2: Try relative to baseDir (if provided), including a decoded variant.
    if (!baseDir.isEmpty())
    {
        if (finalizeResolvedPath(baseDir + "/" + path, "relative to baseDir"))
            return meta;
        if (hasDecodedVariant &&
            finalizeResolvedPath(baseDir + "/" + decodedPath, "relative to baseDir, percent-decoded"))
        {
            return meta;
        }
    }

    // Step 3: Try relative to current working directory, including a decoded variant.
    if (finalizeResolvedPath(QDir::currentPath() + "/" + path, "relative to CWD"))
        return meta;
    if (hasDecodedVariant &&
        finalizeResolvedPath(QDir::currentPath() + "/" + decodedPath, "relative to CWD, percent-decoded"))
    {
        return meta;
    }

    // Not found - log warning but continue
    qWarning() << "[TextureLocationManager] Texture not found:" << path;
    return meta;  // Return with empty resolvedPath
}

TexturePackage TextureLocationManager::packageTextures(
    const std::vector<SceneMesh*>& meshes,
    const QString& outputDirectory,
    const QString& textureSubfolder)
{
    TexturePackage package;
    package.textureSubfolder = textureSubfolder;
    package.textureDirectory = outputDirectory + "/" + textureSubfolder;

    // Create textures directory
    QDir texDir(package.textureDirectory);
    if (!texDir.mkpath("."))
    {
        qCritical() << "[TextureLocationManager] Failed to create texture directory:"
            << package.textureDirectory;
        return package;
    }

    qDebug() << "[TextureLocationManager] Packaging textures to:"
        << package.textureDirectory;

    std::set<QString> processedPaths;      // Track processed original paths
    std::map<QByteArray, QString> hashMap; // Hash -> output filename mapping

    // Collect all textures from all meshes
    for (const auto* mesh : meshes)
    {
        if (!mesh) continue;

        const auto& material = mesh->getMaterial();

        // Iterate through all texture types (25 total)
        for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
        {
            auto type = static_cast<Material::TextureType>(i);
            const auto& tex = material.texture(type);

            if (tex.path.empty()) continue;

            QString texPath = QString::fromStdString(tex.path);

            // Skip if already processed
            if (processedPaths.count(texPath))
            {
                qDebug() << "[TextureLocationManager] Already processed:" << texPath;
                continue;
            }

            // Resolve the texture path
            TextureMetadata meta = resolveTexture(texPath);

            if (meta.resolvedPath.isEmpty())
            {
                qWarning() << "[TextureLocationManager] Could not resolve texture:" << texPath;
                continue;
            }

            // Check for duplicates by content hash
            if (hashMap.count(meta.hash))
            {
                // This is a duplicate - reuse existing output name
                meta.outputName = hashMap[meta.hash];
                meta.relativePath = textureSubfolder + "/" + meta.outputName;

                qDebug() << "[TextureLocationManager] Duplicate texture detected:"
                    << QFileInfo(meta.resolvedPath).fileName()
                    << "-> reusing" << meta.outputName;
            }
            else
            {
                // New unique texture - copy it to output directory
                QString originalName = QFileInfo(meta.resolvedPath).fileName();
                meta.outputName = generateUniqueFilename(
                    package.textureDirectory,
                    originalName);

                QString destPath = package.textureDirectory + "/" + meta.outputName;

                if (QFile::copy(meta.resolvedPath, destPath))
                {
                    meta.relativePath = textureSubfolder + "/" + meta.outputName;
                    package.totalSize += meta.fileSize;
                    hashMap[meta.hash] = meta.outputName;

                    qDebug() << "[TextureLocationManager] Texture copied:"
                        << originalName << "->" << meta.outputName
                        << QString("(%1 MB)").arg(meta.fileSize / (1024.0 * 1024.0), 0, 'f', 2);
                }
                else
                {
                    qWarning() << "[TextureLocationManager] Failed to copy texture:"
                        << meta.resolvedPath << "->" << destPath;
                    continue;
                }
            }

            // Record the mapping for material updates
            package.pathMapping[texPath] = meta.relativePath;
            package.textures.push_back(meta);
            processedPaths.insert(texPath);
        }

        // Also package textures from variant materials so KHR_materials_variants
        // export has all required texture files available.
        if (mesh->hasVariants())
        {
            for (const auto& varMat : mesh->allVariantMaterials())
            {
                for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
                {
                    auto type = static_cast<Material::TextureType>(i);
                    const auto& tex = varMat.texture(type);
                    if (tex.path.empty()) continue;

                    QString texPath = QString::fromStdString(tex.path);
                    if (processedPaths.count(texPath)) continue;

                    TextureMetadata meta = resolveTexture(texPath);
                    if (meta.resolvedPath.isEmpty())
                    {
                        qWarning() << "[TextureLocationManager] Could not resolve variant texture:" << texPath;
                        continue;
                    }

                    if (hashMap.count(meta.hash))
                    {
                        meta.outputName   = hashMap[meta.hash];
                        meta.relativePath = textureSubfolder + "/" + meta.outputName;
                    }
                    else
                    {
                        QString originalName = QFileInfo(meta.resolvedPath).fileName();
                        meta.outputName = generateUniqueFilename(package.textureDirectory, originalName);
                        QString destPath = package.textureDirectory + "/" + meta.outputName;
                        if (QFile::copy(meta.resolvedPath, destPath))
                        {
                            meta.relativePath = textureSubfolder + "/" + meta.outputName;
                            package.totalSize += meta.fileSize;
                            hashMap[meta.hash] = meta.outputName;
                        }
                        else
                        {
                            qWarning() << "[TextureLocationManager] Failed to copy variant texture:"
                                       << meta.resolvedPath << "->" << destPath;
                            continue;
                        }
                    }

                    package.pathMapping[texPath] = meta.relativePath;
                    package.textures.push_back(meta);
                    processedPaths.insert(texPath);
                }
            }
        }
    }

    qDebug() << "[TextureLocationManager] Packaging complete:"
        << package.textures.size() << "unique textures,"
        << package.duplicatesRemoved << "duplicates removed,"
        << QString("%1 MB").arg(package.totalSize / (1024.0 * 1024.0), 0, 'f', 2);

    return package;
}

bool TextureLocationManager::areFilesIdentical(const QString& path1,
    const QString& path2)
{
    QByteArray hash1 = hashFile(path1);
    QByteArray hash2 = hashFile(path2);

    if (hash1.isEmpty() || hash2.isEmpty())
    {
        return false;  // If we can't hash, assume different
    }

    return hash1 == hash2;
}

QByteArray TextureLocationManager::hashFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[TextureLocationManager] Cannot open file for hashing:" << path;
        return QByteArray();
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);

    // Hash in chunks for memory efficiency (1 MB chunks)
    const int chunkSize = 1024 * 1024;
    QByteArray buffer;

    while (!file.atEnd())
    {
        buffer = file.read(chunkSize);
        if (buffer.isEmpty()) break;
        hasher.addData(buffer);
    }

    file.close();
    return hasher.result();
}

QString TextureLocationManager::generateUniqueFilename(
    const QString& directory,
    const QString& originalFilename)
{
    QFileInfo fi(originalFilename);
    QString baseName = fi.baseName();
    QString suffix = fi.suffix();

    // Check if original name is available
    QString candidate = originalFilename;
    if (!QFileInfo(directory + "/" + candidate).exists())
    {
        return candidate;
    }

    // Handle collision with suffix
    int counter = 1;
    while (counter <= 100000)
    {
        if (suffix.isEmpty())
        {
            candidate = QString("%1_%2").arg(baseName).arg(counter);
        }
        else
        {
            candidate = QString("%1_%2.%3")
                .arg(baseName)
                .arg(counter)
                .arg(suffix);
        }

        if (!QFileInfo(directory + "/" + candidate).exists())
        {
            qDebug() << "[TextureLocationManager] Generated unique filename for collision:"
                << originalFilename << "->" << candidate;
            return candidate;
        }

        counter++;
    }

    // Fallback - should never reach here
    qCritical() << "[TextureLocationManager] Cannot generate unique filename after 100000 attempts:"
        << originalFilename;
    return originalFilename + ".collision";
}

bool TextureLocationManager::extractGlbEmbeddedTexture(
    const QString& glbFilePath,
    int            imageIndex,
    QString&       outTempPath)
{
    // Check the per-instance cache first
    const QString cacheKey = glbFilePath + "::" + QString::number(imageIndex);
    auto it = _glbEmbeddedCache.find(cacheKey);
    if (it != _glbEmbeddedCache.end())
    {
        if (QFile::exists(it.value()))
        {
            outTempPath = it.value();
            return true;
        }
        // Cached path was cleaned up — fall through and re-extract
        _glbEmbeddedCache.erase(it);
    }

    // Load the source GLB with Assimp (no post-processing — we only need mTextures[])
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(glbFilePath.toStdString(), 0u);
    if (!scene)
    {
        qWarning() << "[TextureLocationManager] Assimp failed to open for texture extraction:"
                   << glbFilePath
                   << QString::fromLocal8Bit(importer.GetErrorString());
        return false;
    }

    if (imageIndex >= static_cast<int>(scene->mNumTextures))
    {
        qWarning() << "[TextureLocationManager] Image index" << imageIndex
                   << "out of range (file has" << scene->mNumTextures
                   << "embedded textures):" << glbFilePath;
        return false;
    }

    const aiTexture* tex = scene->mTextures[imageIndex];
    if (!tex)
    {
        qWarning() << "[TextureLocationManager] Null texture at index" << imageIndex
                   << "in:" << glbFilePath;
        return false;
    }

    // Build a deterministic temp file path using the source filename and index
    const QString baseName = QFileInfo(glbFilePath).baseName();
    QString format = QString::fromLocal8Bit(tex->achFormatHint);
    if (format.isEmpty()) format = "png";
    const QString tempDir  = QDir::tempPath() + "/ModelViewer_glb_cache";
    QDir().mkpath(tempDir);
    const QString tempFile = tempDir + "/" + baseName
                             + "_image_" + QString::number(imageIndex)
                             + "." + format;

    if (!QFile::exists(tempFile))
    {
        QFile out(tempFile);
        if (!out.open(QIODevice::WriteOnly))
        {
            qWarning() << "[TextureLocationManager] Cannot write temp texture:" << tempFile;
            return false;
        }

        if (tex->mHeight == 0)
        {
            // Compressed blob — write raw bytes
            out.write(reinterpret_cast<const char*>(tex->pcData),
                      static_cast<qint64>(tex->mWidth));
        }
        else
        {
            // Uncompressed RGBA — encode as PNG
            out.close();
            QImage img(
                reinterpret_cast<const uchar*>(tex->pcData),
                static_cast<int>(tex->mWidth),
                static_cast<int>(tex->mHeight),
                static_cast<int>(tex->mWidth) * 4,
                QImage::Format_RGBA8888);
            if (!img.save(tempFile, "PNG"))
            {
                qWarning() << "[TextureLocationManager] Failed to save RGBA texture:" << tempFile;
                QFile::remove(tempFile);
                return false;
            }
        }

        if (out.isOpen()) out.close();
        qDebug() << "[TextureLocationManager] Extracted GLB embedded texture:"
                 << glbFilePath << "image" << imageIndex << "->" << tempFile;
    }

    _glbEmbeddedCache[cacheKey] = tempFile;
    outTempPath = tempFile;
    return true;
}
