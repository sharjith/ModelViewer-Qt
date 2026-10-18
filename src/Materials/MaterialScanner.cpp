// MaterialScanner.cpp
#include "MaterialScanner.h"
#include <QDir>
#include <QFileInfoList>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>

static QString lower(const QString& s) { return s.toLower(); }

MaterialsMap MaterialScanner::parseMaterialsFolder(const QString& rootFolder)
{
    MaterialsMap out;

    // tokens (ordered) used to detect each texture type in the filename
    const QStringList albedoTokens = { "alb", "albedo", "basecolor", "base_color", "base-color", "base", "diffuse", "diff", "col", "color" };
    const QStringList metallicTokens = { "spec", "specular", "metallic", "metalness", "metal", "m" };
    const QStringList normalTokens = { "normal", "normalgl", "normalmap", "normal_map", "nor", "nrm", "nm", "_n", "-n", "n" };
    const QStringList aoTokens = { "ao", "ambientocclusion", "ambient_occlusion", "occ", "occlusion" };

    const QStringList roughnessTokens = { "roughness","rough","r" };
    const QStringList emissiveTokens = { "emissive","emit","emission","glow","light" };
    const QStringList opacityTokens = { "opacity","alpha","transparency","mask","opa" };
    const QStringList heightTokens = { "height","disp","displacement","bump" };
    const QStringList transmissionTokens = { "transmission","trans","glass" };
    const QStringList iorTokens = { "ior","indexofrefraction","refract" };
    const QStringList sheenColorTokens = { "sheen","sheen_color" };
    const QStringList sheenRoughTokens = { "sheenrough","sheen_rough","sheenroughness" };
    const QStringList ccColorTokens = { "clearcoat","cc_color" };
    const QStringList ccRoughTokens = { "cc_rough","clearcoatroughness","clearcoat_rough" };
    const QStringList ccNormalTokens = { "cc_normal","clearcoatnormal","clearcoat_normal" };

    const QStringList packedAormTokens = {
    "aorm", "aormap", "aor", "ormap", "ormap", "ormap", "orm",
    "occlusion_roughness_metallic", "occlusionroughnessmetallic",
    "occlusion_roughness_metal", "occlusion_roughness_metalmap",
    "occlusion-roughness-metallic", "ao_rm", "ao_rm_map", "ao_r_m",
    "ao-rm", "aormap", "ormap", "aor_map", "orm_map", "arm"
    };

    const QStringList normalIdentifierTokens = {
    "normal", "normalmap", "normal_map", "normal-map", "nrm", "nm", "_n", "-n"
    };


    // allowed file extensions (add more if you need)
    const QStringList exts = { "png", "jpg", "jpeg", "tga", "bmp", "dds", "hdr", "exr", "ktx2" };

    QDir root(rootFolder);
    if (!root.exists()) return out;

    // find immediate subdirectories (each is a material)
    QFileInfoList matDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    auto tokenMatchesInFilename = [](const QString& fnameLower, const QString& tokenLower) -> bool {
        // tokenLower is expected to be ascii letters/numbers; fnameLower is already lowercased
        // Build pattern that matches token as standalone segment: (^|[^a-z0-9])token([^a-z0-9]|$)
        QString pat = QString("(^|[^a-z0-9])%1([^a-z0-9]|$)").arg(QRegularExpression::escape(tokenLower));
        QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        return re.match(fnameLower).hasMatch();
        };

    auto filenameLooksLikeNormal = [&](const QString& fnameLower) -> bool {
        for (const QString& nt : normalIdentifierTokens)
        {
            if (tokenMatchesInFilename(fnameLower, nt)) return true;
        }
        return false;
        };

    for (const QFileInfo& dirInfo : matDirs)
    {
        QDir matDir(dirInfo.absoluteFilePath());
        QFileInfoList files = matDir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);

        TextureMap texMap; // will populate keys only for found textures

        // Keep track of which logical types should be skipped because a packed AORM was found.
        QSet<QString> skipTypes;       

        // --- packed AORM detection (prefer packed if present) ---
        QString packedPath;
        QString packedChanOrder = QStringLiteral("r,g,b"); // default convention R=AO G=Roughness B=Metallic

        for (const QFileInfo& f : files)
        {
            QString extension = f.suffix().toLower();
            if (!exts.contains(extension)) continue; // skip unknown extensions

            QString fname = lower(f.completeBaseName());

            if (filenameLooksLikeNormal(fname)) continue;

            for (const QString& ptok : packedAormTokens)
            {
                if (tokenMatchesInFilename(fname, ptok))
                {
                    // found a packed AORM-like file
                    packedPath = f.absoluteFilePath();

                    // attempt a tiny heuristic to infer ordering from filename (e.g. "..._bgr" or "...brg")
                    // keep this minimal: check for common substrings
                    if (fname.contains("bgr")) packedChanOrder = QStringLiteral("b,g,r");
                    else if (fname.contains("brg")) packedChanOrder = QStringLiteral("b,r,g");
                    else if (fname.contains("grb")) packedChanOrder = QStringLiteral("g,r,b");
                    else if (fname.contains("gbr")) packedChanOrder = QStringLiteral("g,b,r");
                    else if (fname.contains("rbg")) packedChanOrder = QStringLiteral("r,b,g");
                    else packedChanOrder = QStringLiteral("r,g,b");

                    break;
                }
            }
            if (!packedPath.isEmpty()) break;
        }

        if (!packedPath.isEmpty())
        {
            // assign logical keys to the packed path so older code still sees AO/roughness/metallic
            texMap.insert("ao", packedPath);
            texMap.insert("roughness", packedPath);
            texMap.insert("metallic", packedPath);
            // meta entry: "<path>|<chanOrder>"
            texMap.insert("packed:aorm", QString("%1|%2").arg(packedPath, packedChanOrder));

            // skip these individual map detection since packed map takes precedence
            skipTypes.insert("ao");
            skipTypes.insert("roughness");
            skipTypes.insert("metallic");
        }
        // --- end packed AORM detection ---

        // We'll keep track of best match per type:
        auto tryAssign = [&](const QStringList& tokens, const QString& type) {
            // if this type is provided by packed map, skip assigning it here
            if (skipTypes.contains(type)) return;

            // Priority: exact token in filename (ordered by token list). If multiple files match token, choose file with best extension priority.
            QString bestPath;
            int bestTokenIndex = INT_MAX;
            int bestExtIndex = INT_MAX;
            int bestTokenPosition = -1; // position of token in filename

            for (const QFileInfo& f : files)
            {
                QString fname = lower(f.completeBaseName()); // filename without extension, lowercase
                QString extension = f.suffix().toLower();
                int extIndex = exts.indexOf(extension);
                if (extIndex < 0) extIndex = exts.size(); // unlisted extensions get low priority

                // Skip the packed AORM file when detecting other map types
                if (!packedPath.isEmpty() && f.absoluteFilePath() == packedPath) continue;

                // Skip files that look like normal maps when detecting non-normal types
                if (type != "normal" && filenameLooksLikeNormal(fname)) continue;

                // find earliest token that occurs in filename
                for (int ti = 0; ti < tokens.size(); ++ti)
                {
                    const QString& tok = tokens[ti];
                    if (tokenMatchesInFilename(fname, tok))
                    {
                        // Find the LAST occurrence of this token in the filename
                        int tokenPos = fname.lastIndexOf(tok);

                        bool betterMatch = false;
                        if (ti < bestTokenIndex)
                        {
                            // Higher priority token
                            betterMatch = true;
                        }
                        else if (ti == bestTokenIndex)
                        {
                            if (tokenPos > bestTokenPosition)
                            {
                                // Same token priority, but appears later in filename
                                betterMatch = true;
                            }
                            else if (tokenPos == bestTokenPosition && extIndex < bestExtIndex)
                            {
                                // Same position, better extension
                                betterMatch = true;
                            }
                        }

                        if (betterMatch)
                        {
                            bestTokenIndex = ti;
                            bestExtIndex = extIndex;
                            bestTokenPosition = tokenPos;
                            bestPath = f.absoluteFilePath();
                        }
                        break; // stop checking tokens for this file (we found a token)
                    }
                }
            }

            // Fallbacks: some users name files like "matname_d", "matname_m", "matname_n"
            if (bestPath.isEmpty())
            {
                for (const QFileInfo& f : files)
                {
                    QString fname = f.completeBaseName().toLower();

                    // Skip packed AORM and normal-looking files in fallbacks too
                    if (!packedPath.isEmpty() && f.absoluteFilePath() == packedPath) continue;
                    if (type != "normal" && filenameLooksLikeNormal(fname)) continue;

                    if (type == "albedo")
                    {
                        if (fname.endsWith("_d") || fname.endsWith("-d") || fname.endsWith("_diff") || fname.endsWith("diffuse")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "metallic")
                    {
                        if (fname.endsWith("_m") || fname.endsWith("-m") || fname.endsWith("_metal")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "normal")
                    {
                        if (fname.endsWith("_n") || fname.endsWith("-n") || fname.endsWith("_normal") || fname.endsWith("_nrm")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "ao")
                    {
                        if (fname.endsWith("_ao") || fname.endsWith("-ao") || fname.endsWith("_occl")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "roughness")
                    {
                        if (fname.endsWith("_r") || fname.endsWith("-r") || fname.endsWith("_rough")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "emissive")
                    {
                        if (fname.endsWith("_e") || fname.endsWith("-e") || fname.endsWith("_emit")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "opacity")
                    {
                        if (fname.endsWith("_a") || fname.endsWith("-a") || fname.endsWith("_alpha")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "height")
                    {
                        if (fname.endsWith("_h") || fname.endsWith("-h") || fname.endsWith("_disp") || fname.endsWith("_bump")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "transmission")
                    {
                        if (fname.endsWith("_t") || fname.endsWith("-t") || fname.contains("glass")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type == "ior")
                    {
                        if (fname.contains("ior") || fname.contains("refract")) { bestPath = f.absoluteFilePath(); break; }
                    }
                    else if (type.startsWith("sheen") || type.startsWith("cc_"))
                    {
                        // basic fallback for sheen / clearcoat related maps
                        if (fname.contains("sheen") || fname.contains("clearcoat") || fname.contains("cc_")) { bestPath = f.absoluteFilePath(); break; }
                    }
                }
            }

            if (!bestPath.isEmpty()) texMap.insert(type, bestPath);
            };

        // run token matchers for all map types
        tryAssign(albedoTokens, "albedo");
        tryAssign(metallicTokens, "metallic");
        tryAssign(normalTokens, "normal");
        tryAssign(aoTokens, "ao");
        tryAssign(roughnessTokens, "roughness");
        tryAssign(emissiveTokens, "emissive");
        tryAssign(opacityTokens, "opacity");
        //tryAssign(heightTokens, "height");
        tryAssign(transmissionTokens, "transmission");
        tryAssign(iorTokens, "ior");
        tryAssign(sheenColorTokens, "sheen_color");
        tryAssign(sheenRoughTokens, "sheen_rough");
        tryAssign(ccColorTokens, "cc_color");
        tryAssign(ccRoughTokens, "cc_rough");
        tryAssign(ccNormalTokens, "cc_normal");

        // As a last-ditch fallback: if the folder contains exactly 4 files or files with no clear naming,
        // optionally try heuristics by file size / typical naming. For now we won't guess aggressively.
        // Insert the material only if at least one texture was found (modify if you want to include empty mats)
        if (!texMap.isEmpty())
        {
            out.insert(dirInfo.fileName(), texMap);
        }
        else
        {
            // Optionally include material entry with empty texture map
            out.insert(dirInfo.fileName(), texMap);
        }
    }

    return out;
}


void MaterialScanner::populateWithMaterials(QComboBox* combo, const MaterialsMap& materials)
{
    if (!combo) return;
    combo->clear();
    // preserve iteration order of materials (QMap sorts keys) - if you want insertion order use QList of pairs instead
    for (auto it = materials.constBegin(); it != materials.constEnd(); ++it)
    {
        combo->addItem(it.key());
    }
}

void MaterialScanner::populateWithMaterials(QListWidget* list, const MaterialsMap& materials)
{
    if (!list) return;
    list->clear();
    // preserve iteration order of materials (QMap sorts keys) - if you want insertion order use QList of pairs instead
    for (auto it = materials.constBegin(); it != materials.constEnd(); ++it)
    {
        list->addItem(it.key());
	}
}

void MaterialScanner::populateWithMaterials(QTreeWidget* tree, const MaterialsMap& materials)
{
    if (!tree) return;
    tree->clear();

    // Map to store categorized materials: category name -> list of material names
    QMap<QString, QStringList> categorizedMaterials;

    // Parse materials and group by category prefix (e.g., "Metal_Bronze" -> category: "Metal", name: "Bronze")
    for (auto it = materials.constBegin(); it != materials.constEnd(); ++it)
    {
        const QString& materialName = it.key();

        // Check if name contains underscore separator
        int underscorePos = materialName.indexOf('_');

        if (underscorePos > 0)
        {
            // Extract category and display name
            QString category = materialName.left(underscorePos);
            QString displayName = materialName.mid(underscorePos + 1);

            categorizedMaterials[category].append(materialName);
        }
        else
        {
            // No category prefix, add to "Uncategorized"
            categorizedMaterials["Uncategorized"].append(materialName);
        }
    }

    // Build the tree structure
    for (auto catIt = categorizedMaterials.constBegin(); catIt != categorizedMaterials.constEnd(); ++catIt)
    {
        const QString& categoryName = catIt.key();
        const QStringList& materialList = catIt.value();

        // Skip empty categories
        if (materialList.isEmpty()) continue;

        // Create category parent item
        QTreeWidgetItem* categoryItem = new QTreeWidgetItem(tree);
        categoryItem->setText(0, QString("%1 (%2)").arg(categoryName).arg(materialList.size()));
        categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable); // Make category non-selectable

        // Optional: Make category items bold
        QFont font = categoryItem->font(0);
        font.setBold(true);
        categoryItem->setFont(0, font);

        // Add materials as children
        for (const QString& materialName : materialList)
        {
            QTreeWidgetItem* materialItem = new QTreeWidgetItem(categoryItem);

            // Display name without category prefix (e.g., "Bronze" instead of "Metal_Bronze")
            int underscorePos = materialName.indexOf('_');
            QString displayName = (underscorePos > 0) ? materialName.mid(underscorePos + 1) : materialName;

            materialItem->setText(0, displayName);
            materialItem->setData(0, Qt::UserRole, materialName); // Store original full name for retrieval
        }

        // Expand category by default if it has few items
        if (materialList.size() <= 10)
        {
            categoryItem->setExpanded(true);
        }
    }

    // Sort categories alphabetically (but Uncategorized at the end)
    tree->sortItems(0, Qt::AscendingOrder);

    // Move Uncategorized to the end if it exists
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        if (item->text(0).startsWith("Uncategorized"))
        {
            tree->takeTopLevelItem(i);
            tree->addTopLevelItem(item);
            break;
        }
    }

    tree->expandAll();
}