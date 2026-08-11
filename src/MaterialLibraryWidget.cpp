#include "MaterialLibraryWidget.h"
#include "MaterialRegistry.h"
#include <QTreeWidgetItem>
#include <QFontMetrics>
#include <QStandardPaths>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QDir>
#include <QSysInfo>
#include <QSaveFile>
#include <QMessageBox>
#include <QKeyEvent>
#include "PathUtils.h"


QMap<QString, std::function<Material()>> MaterialLibraryWidget::s_materialMap;
QVector<QPair<QString, QVector<QPair<QString, QString>>>> MaterialLibraryWidget::s_groups;
QString MaterialLibraryWidget::s_jsonPath;
QString MaterialLibraryWidget::s_userJsonPath;
QSet<QString> MaterialLibraryWidget::s_userMaterialKeys;


// Helper: return the per-user materials path (create dir if needed)
static QString userMaterialsFilePath()
{
	// Qt's AppDataLocation typically returns something like:
	//   Windows: C:/Users/<user>/AppData/Roaming/<OrgName>/<AppName>
	//   macOS:   ~/Library/Application Support/<AppName>
	//   Linux:   ~/.local/share/<AppName>
	QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

	if (base.isEmpty())
	{
#if defined(Q_OS_WIN)
		base = QDir::homePath() + "/AppData/Roaming/ModelViewer";
#elif defined(Q_OS_MACOS)
		base = QDir::homePath() + "/Library/Application Support/ModelViewer";
#else
		base = QDir::homePath() + "/.local/share/ModelViewer";
#endif
	}

	// ensure we don't accidentally append another "ModelViewer" if base already contains it
	// normalize to avoid trailing slashes differences
	QString normalized = QDir(base).cleanPath(base);
	if (!normalized.endsWith(QStringLiteral("ModelViewer")))
	{
		normalized = QDir(normalized).filePath(QStringLiteral("ModelViewer"));
	}

	QDir d(normalized);
	if (!d.exists())
	{
		if (!d.mkpath("."))
		{
			qWarning() << "Failed to create user materials directory:" << normalized;
		}
	}
	return d.filePath("materials.json");
}

static bool ensureFileWritable(const QString& filePath, QString* err)
{
	QFile f(filePath);
	if (!f.exists()) return true; // nothing to do

	QFile::Permissions perms = f.permissions();
	// check if owner write is present
	if (perms & QFileDevice::WriteOwner) return true;

	// try to set owner write permission (best-effort)
	QFile::Permissions newPerms = perms | QFileDevice::WriteOwner;
	if (f.setPermissions(newPerms)) return true;

	if (err) *err = QString("File exists but is not writable and permissions could not be changed: %1").arg(filePath);
	return false;
}

static bool writeVariantRootToFile(const QVariantMap& rootVar, const QString& outPath, QString* err)
{
	// Ensure directory is present
	QDir dir = QFileInfo(outPath).dir();
	if (!dir.exists())
	{
		if (!dir.mkpath("."))
		{
			if (err) *err = QString("Failed to create directory: %1").arg(dir.absolutePath());
			return false;
		}
	}

	// If file exists and not writable then try to fix or bail with clear message
	if (QFile::exists(outPath))
	{
		if (!ensureFileWritable(outPath, err)) return false;
	}

	QJsonDocument doc = QJsonDocument::fromVariant(rootVar);
	QSaveFile out(outPath);
	if (!out.open(QIODevice::WriteOnly))
	{
		if (err) *err = QString("Failed to open file '%1' for writing: %2").arg(outPath, out.errorString());
		return false;
	}
	out.write(doc.toJson(QJsonDocument::Indented));
	if (!out.commit())
	{
		// commit failed: could be permission or file lock
		if (err) *err = QString("Failed to commit file '%1': %2").arg(outPath, out.errorString());
		return false;
	}
	return true;
}

MaterialLibraryWidget::MaterialLibraryWidget(QWidget* parent)
	: QTreeWidget(parent)
{
	setHeaderHidden(true);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setMouseTracking(true);

	// Populate materials (JSON first, fallback to built-ins)
	populateMaterials();
	
	connect(this, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (selectedItems().isEmpty()) return;
		QTreeWidgetItem* item = selectedItems().first();
		if (!item) return;

		QString key = item->data(0, Qt::UserRole).toString();
		if (!key.isEmpty() && MaterialLibraryWidget::s_materialMap.contains(key))
			emit materialPreview(MaterialLibraryWidget::s_materialMap[key]());
		else
			emit materialPreview(Material::DEFAULT_MAT()); 
		});

	// ADD itemDoubleClicked connection (after itemSelectionChanged):
	connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		if (!item) return;

		QString key = item->data(0, Qt::UserRole).toString();
		if (!key.isEmpty() && MaterialLibraryWidget::s_materialMap.contains(key))
			emit materialSelected(MaterialLibraryWidget::s_materialMap[key]());
		else
			emit materialSelected(Material::DEFAULT_MAT());
		});

	// Apply the currently selected material with the spacebar.
	// This mirrors the double-click behavior for keyboard users.
	setFocusPolicy(Qt::StrongFocus);

	// connect when user hovers over an item
	connect(this, &QTreeWidget::itemEntered,
		this, &MaterialLibraryWidget::handleItemEntered);

	connect(&MaterialRegistry::instance(), &MaterialRegistry::materialsChanged,
		this, &MaterialLibraryWidget::populateMaterials);

}

void MaterialLibraryWidget::keyPressEvent(QKeyEvent* event)
{
	if (event && event->key() == Qt::Key_Space)
	{
		QList<QTreeWidgetItem*> sel = selectedItems();
		if (!sel.isEmpty())
		{
			QTreeWidgetItem* item = sel.first();
			QString key = item->data(0, Qt::UserRole).toString();
			if (!key.isEmpty() && MaterialLibraryWidget::s_materialMap.contains(key))
				emit materialSelected(MaterialLibraryWidget::s_materialMap[key]());
			else
				emit materialSelected(Material::DEFAULT_MAT());
		}
		event->accept();
		return;
	}

	QTreeWidget::keyPressEvent(event);
}

void MaterialLibraryWidget::deleteSelectedMaterial()
{
	QList<QTreeWidgetItem*> sel = selectedItems();
	if (sel.isEmpty()) return;
	QTreeWidgetItem* item = sel.first();
	if (item->childCount()) return; // group header

	QString key = item->data(0, Qt::UserRole).toString();
	if (key.isEmpty()) return;

	// Only allow deleting user materials
	if (!s_userMaterialKeys.contains(key))
	{
		QMessageBox::information(this, tr("Cannot delete"),
			tr("This material is part of the shipped library and cannot be deleted. "
				"You can create a user material with the same key to override it."));
		return;
	}

	// find its group label
	QString groupLabel;
	for (const auto& g : s_groups)
	{
		for (const auto& it : g.second)
		{
			if (it.second == key) { groupLabel = g.first; break; }
		}
		if (!groupLabel.isEmpty()) break;
	}
	if (groupLabel.isEmpty())
	{
		qWarning() << "Could not find group for key" << key;
		return;
	}

	QString err;
	if (!MaterialLibraryWidget::removeUserMaterialFromUserLocation(groupLabel, key, this, &err))
	{
		QMessageBox::warning(this, tr("Delete failed"), err);
		return;
	}

	// notify widgets
	Q_EMIT MaterialRegistry::instance().materialsChanged();
}

void MaterialLibraryWidget::selectMaterialByKey(const QString& key)
{	
	QList<QTreeWidgetItem*> items = findItems(QString("*"),
		Qt::MatchWildcard | Qt::MatchRecursive);
	for (QTreeWidgetItem* item : items)
	{
		if (item->childCount() == 0)
		{ // leaf
			QString itemKey = item->data(0, Qt::UserRole).toString();
			if (itemKey == key)
			{
				setCurrentItem(item);
				scrollToItem(item);
				// emit same signal as if user clicked it				
				if (!key.isEmpty() && MaterialLibraryWidget::s_materialMap.contains(key))
					emit materialSelected(MaterialLibraryWidget::s_materialMap[key]());
				else
					emit materialSelected(Material::DEFAULT_MAT());
				break;
			}
		}
	}
}


bool MaterialLibraryWidget::loadAllMaterials(const QString& jsonPath, QString* err)
{
	// store path for convenience
	s_jsonPath = jsonPath;

	// clear previous data
	s_materialMap.clear();
	s_groups.clear();
	s_userMaterialKeys.clear();  // Clear user material keys so they're rebuilt from the user JSON

	// Use the MaterialRegistry (it already knows how to parse the JSON file)
	MaterialRegistry& reg = MaterialRegistry::instance();

	QString localErr;
	bool jsonLoaded = reg.loadFromJsonFile(jsonPath, &localErr);

	if (jsonLoaded)
	{
		// Populate s_materialMap from JSON groups
		const auto groups = reg.groups();
		for (const auto& g : groups)
		{
			QVector<QPair<QString, QString>> itemsInGroup;
			for (const auto& it : g.items)
			{
				QVariantMap props = it.props; // capture by value
				const QString key = it.key;
				// Insert a factory that creates a Material from the props
				s_materialMap.insert(key, [props]() -> Material {
					return Material::fromVariantMap(props);
					});
				itemsInGroup.emplace_back(it.name, it.key);
			}
			s_groups.emplace_back(g.label, itemsInGroup);
		}		
	}
	else
	{
		// fallback to built-in registration if JSON failed
		if (err) *err = localErr;

		// Reuse your populateMaterialMapWithBuiltIns helper but fill static s_materialMap/s_groups
		s_groups = populateMaterialMapWithBuiltIns(s_materialMap);
	}

	// Always attempt to overlay user JSON
	QString userErr;
	if (!mergeUserMaterialsFromUserLocation(&userErr))
	{
		// Dont treat as fatal - just warn
		qWarning() << "User materials overlay failed:" << userErr;
	}

	if (err && jsonLoaded) *err = QString(); // clear error if system JSON loaded ok

	return true; // we still return true because built-ins were loaded
}

bool MaterialLibraryWidget::mergeUserMaterialsFromUserLocation(QString* err)
{
	const QString userPath = userMaterialsFilePath();
	// Store the user JSON path for later use by userMaterialsRootPath()
	s_userJsonPath = userPath;
	// If no user file exists, nothing to do (success)
	if (!QFile::exists(userPath))
	{
		if (err) *err = QString();
		return true;
	}

	QFile f(userPath);
	if (!f.open(QIODevice::ReadOnly))
	{
		if (err) *err = QString("Failed to open user materials file '%1': %2").arg(userPath, f.errorString());
		return false;
	}

	const QByteArray data = f.readAll();
	f.close();
	QJsonParseError perr;
	QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
	if (perr.error != QJsonParseError::NoError)
	{
		if (err) *err = QString("Failed to parse user materials JSON '%1': %2").arg(userPath, perr.errorString());
		return false;
	}

	if (!doc.isObject())
	{
		if (err) *err = QString("User materials JSON root is not an object: %1").arg(userPath);
		return false;
	}

	QJsonObject root = doc.object();
	// Expecting structure: { "groups": [ { "label": "...", "items": [ {...}, ... ] }, ... ] }
	if (!root.contains("groups") || !root.value("groups").isArray())
	{
		// nothing to merge (valid but empty structure)
		if (err) *err = QString();
		return true;
	}

	QJsonArray groupsArr = root.value("groups").toArray();

	// Merge logic:
	// - for each group in user JSON:
	//    - find existing group in s_groups by label; if found append items, else create new group entry
	// - for each item: insert/overwrite s_materialMap[key] with factory that uses the props

	for (const QJsonValue& gval : groupsArr)
	{
		if (!gval.isObject()) continue;
		QJsonObject gobj = gval.toObject();
		QString groupLabel = gobj.value("label").toString().trimmed();
		if (groupLabel.isEmpty()) continue;

		// Convert items array
		QJsonArray itemsArr = gobj.value("items").toArray();
		if (itemsArr.isEmpty())
		{
			// ensure a group exists even if empty
			bool foundEmpty = false;
			for (const auto& existingGroup : s_groups)
			{
				if (existingGroup.first == groupLabel) { foundEmpty = true; break; }
			}
			if (!foundEmpty) s_groups.emplace_back(groupLabel, QVector<QPair<QString, QString>>());
			continue;
		}

		// locate target group index in s_groups
		int targetIndex = -1;
		for (int i = 0; i < s_groups.size(); ++i)
		{
			if (s_groups[i].first == groupLabel) { targetIndex = i; break; }
		}
		bool groupExists = (targetIndex >= 0);
		if (!groupExists)
		{
			// create new empty group
			s_groups.emplace_back(groupLabel, QVector<QPair<QString, QString>>());
			targetIndex = s_groups.size() - 1;
		}

		// append/overwrite items
		for (const QJsonValue& itVal : itemsArr)
		{
			if (!itVal.isObject()) continue;
			QJsonObject itObj = itVal.toObject();

			// required keys: key, name. If missing, skip.
			QString key = itObj.value("key").toString().trimmed();
			QString name = itObj.value("name").toString().trimmed();
			if (key.isEmpty() || name.isEmpty()) continue;

			// Convert item object to QVariantMap for Material::fromVariantMap
			QVariantMap props = itObj.toVariantMap();

			// Insert/overwrite factory in shared map. User materials override existing keys.
			// Lambda captures props, userPath, and key to resolve relative texture paths
			s_materialMap.insert(key, [props, userPath, key]() -> Material {
				// Make a copy of props to resolve relative paths
				QVariantMap propsResolved = props;

				// Get the user materials root path and material folder
				QString userRoot = QFileInfo(userPath).dir().absolutePath();
				QString materialFolder = QDir(userRoot).filePath(key);

				// Resolve texture paths from relative (in user material folder) to absolute
				if (propsResolved.contains("textureMetadata"))
				{
					QVariantMap textureMetadataMap = propsResolved.value("textureMetadata").toMap();

					for (auto it = textureMetadataMap.begin(); it != textureMetadataMap.end(); ++it)
					{
						QVariantMap texMetadata = it.value().toMap();
						QString texPath = texMetadata.value("path").toString();

						// If path is relative (no directory separators), resolve it to user material folder
						if (!texPath.isEmpty() && !texPath.contains("/") && !texPath.contains("\\"))
						{
							// It's a relative path - resolve it to the material folder
							QString resolvedPath = QDir(materialFolder).filePath(texPath);
							texMetadata.insert("path", resolvedPath);
							textureMetadataMap.insert(it.key(), texMetadata);
						}
					}

					propsResolved.insert("textureMetadata", textureMetadataMap);
				}

				// Also resolve legacy texture path fields (for backward compatibility)
				QStringList texPathKeys = {
					"albedoMapPath", "normalMapPath", "metallicMapPath", "roughnessMapPath",
					"aoMapPath", "opacityMapPath", "emissiveMapPath", "heightMapPath",
					"transmissionMapPath", "iorMapPath", "sheenColorMapPath", "sheenRoughnessMapPath",
					"clearcoatColorMapPath", "clearcoatRoughnessMapPath", "clearcoatNormalMapPath",
					"iridescenceMapPath", "iridescenceThicknessMapPath"
				};

				for (const QString& pathKey : texPathKeys)
				{
					if (propsResolved.contains(pathKey))
					{
						QString texPath = propsResolved.value(pathKey).toString();
						// If relative path, resolve to user material folder
						if (!texPath.isEmpty() && !texPath.contains("/") && !texPath.contains("\\"))
						{
							QString resolvedPath = QDir(materialFolder).filePath(texPath);
							propsResolved.insert(pathKey, resolvedPath);
						}
					}
				}

				return Material::fromVariantMap(propsResolved);
				});

			s_userMaterialKeys.insert(key);

			// Add to target group (if an item with same key already exists in the group, remove it first)
			QVector<QPair<QString, QString>>& targetItems = s_groups[targetIndex].second;
			int existingPos = -1;
			for (int k = 0; k < targetItems.size(); ++k)
			{
				if (targetItems[k].second == key) { existingPos = k; break; }
			}
			if (existingPos >= 0)
			{
				// replace display name (user override)
				targetItems[existingPos].first = name;
			}
			else
			{
				targetItems.emplace_back(name, key);
			}
		} // items loop
	} // groups loop

	Q_EMIT MaterialRegistry::instance().materialsChanged();

	if (err) *err = QString();
	return true;
}


// -------------------- saveUserMaterialToUserLocation (updated with confirmation) --------------------
bool MaterialLibraryWidget::saveUserMaterialToUserLocation(const QString& groupLabel,
	const QString& key,
	const QString& name,
	const Material& mat,
	QWidget* parent,
	QString* err)
{
	if (groupLabel.trimmed().isEmpty())
	{
		if (err) *err = "Empty group label";
		return false;
	}
	if (key.trimmed().isEmpty())
	{
		if (err) *err = "Empty material key";
		return false;
	}

	const QString userPath = userMaterialsFilePath();

	// Load existing root (if present)
	QVariantMap rootVar;
	if (QFile::exists(userPath))
	{
		QFile in(userPath);
		if (!in.open(QIODevice::ReadOnly))
		{
			if (err) *err = QString("Failed to open user materials file '%1' for reading: %2").arg(userPath, in.errorString());
			return false;
		}
		QByteArray data = in.readAll();
		in.close();
		QJsonParseError perr;
		QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
		if (perr.error != QJsonParseError::NoError)
		{
			if (err) *err = QString("Failed to parse user materials JSON '%1': %2").arg(userPath, perr.errorString());
			return false;
		}
		rootVar = doc.toVariant().toMap();
	}
	else
	{
		rootVar.clear();
	}

	// Ensure groups list exists
	QVariantList groupsList = rootVar.value("groups").toList();

	// Locate or create the target group
	int groupIndex = -1;
	for (int i = 0; i < groupsList.size(); ++i)
	{
		QVariantMap g = groupsList[i].toMap();
		QString label = g.value("label").toString();
		if (label == groupLabel) { groupIndex = i; break; }
	}
	if (groupIndex < 0)
	{
		QVariantMap newGroup;
		newGroup.insert("label", groupLabel);
		newGroup.insert("items", QVariantList());
		groupsList.append(QVariant(newGroup));
		groupIndex = groupsList.size() - 1;
	}

	// Prepare mat props and item object
	QVariantMap matProps = mat.toVariantMap(); // must produce full fields
	matProps.insert("key", key);
	matProps.insert("name", name);

	// CRITICAL: Convert absolute texture paths to relative and copy texture files
	// Create material folder in user library for storing texture files
	QString userRoot = QFileInfo(userPath).dir().absolutePath();
	QString materialFolder = QDir(userRoot).filePath(key);
	QDir().mkpath(materialFolder);  // Create folder if it doesn't exist

	// Process textureMetadata: convert absolute paths to relative and copy files
	if (matProps.contains("textureMetadata"))
	{
		QVariantMap textureMetadataMap = matProps.value("textureMetadata").toMap();

		for (auto it = textureMetadataMap.begin(); it != textureMetadataMap.end(); ++it)
		{
			QVariantMap texMetadata = it.value().toMap();
			QString originalPath = texMetadata.value("path").toString();

			if (!originalPath.isEmpty())
			{
				QFileInfo origInfo(originalPath);
				if (origInfo.exists())
				{
					// Copy the texture file to material folder with relative path
					QString filename = origInfo.fileName();
					QString targetPath = QDir(materialFolder).filePath(filename);
					if (QFile::copy(originalPath, targetPath))
					{
						// Store relative path (just filename) in the material properties
						texMetadata.insert("path", filename);
						textureMetadataMap.insert(it.key(), texMetadata);
					}
				}
			}
		}
		matProps.insert("textureMetadata", textureMetadataMap);
	}

	// Work on items list
	QVariantMap targetGroup = groupsList[groupIndex].toMap();
	QVariantList itemsList = targetGroup.value("items").toList();

	int existingItemIndex = -1;
	for (int i = 0; i < itemsList.size(); ++i)
	{
		QVariantMap itm = itemsList[i].toMap();
		QString existingKey = itm.value("key").toString();
		if (existingKey == key) { existingItemIndex = i; break; }
	}

	// If item exists, ask for confirmation (if parent provided)
	if (existingItemIndex >= 0)
	{
		if (parent)
		{
			QMessageBox::StandardButton reply =
				QMessageBox::question(parent,
					QStringLiteral("Overwrite Material?"),
					QStringLiteral("A material with this key already exists. Overwrite it?"),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No);
			if (reply != QMessageBox::Yes)
			{
				if (err) *err = QStringLiteral("User cancelled overwrite");
				return false;
			}
		}
		itemsList[existingItemIndex] = QVariant(matProps);
	}
	else
	{
		itemsList.append(QVariant(matProps));
	}

	targetGroup.insert("items", itemsList);
	groupsList[groupIndex] = QVariant(targetGroup);
	rootVar.insert("groups", groupsList);

	// Write back
	if (!writeVariantRootToFile(rootVar, userPath, err)) return false;

	// Update runtime caches: s_materialMap & s_groups
	QVariantMap propsForCache = matProps;

	// Create lambda that resolves relative texture paths when loading from user library
	s_materialMap.insert(key, [propsForCache, userPath, key]() -> Material {
		// Get the user materials root path
		QString userRoot = QFileInfo(userPath).dir().absolutePath();
		QString materialFolder = QDir(userRoot).filePath(key);

		// Make a copy of props to resolve relative paths
		QVariantMap propsResolved = propsForCache;

		// Resolve texture paths from relative (in user material folder) to absolute
		if (propsResolved.contains("textureMetadata"))
		{
			QVariantMap textureMetadataMap = propsResolved.value("textureMetadata").toMap();

			for (auto it = textureMetadataMap.begin(); it != textureMetadataMap.end(); ++it)
			{
				QVariantMap texMetadata = it.value().toMap();
				QString texPath = texMetadata.value("path").toString();

				// If path is relative (no directory separators), resolve it to user material folder
				if (!texPath.isEmpty() && !texPath.contains("/") && !texPath.contains("\\"))
				{
					// It's a relative path - resolve it to the material folder
					QString resolvedPath = QDir(materialFolder).filePath(texPath);
					texMetadata.insert("path", resolvedPath);
					textureMetadataMap.insert(it.key(), texMetadata);
				}
			}

			propsResolved.insert("textureMetadata", textureMetadataMap);
		}

		// Also resolve legacy texture path fields (for backward compatibility)
		QStringList texPathKeys = {
			"albedoMapPath", "normalMapPath", "metallicMapPath", "roughnessMapPath",
			"aoMapPath", "opacityMapPath", "emissiveMapPath", "heightMapPath",
			"transmissionMapPath", "iorMapPath", "sheenColorMapPath", "sheenRoughnessMapPath",
			"clearcoatColorMapPath", "clearcoatRoughnessMapPath", "clearcoatNormalMapPath",
			"iridescenceMapPath", "iridescenceThicknessMapPath"
		};

		for (const QString& key : texPathKeys)
		{
			if (propsResolved.contains(key))
			{
				QString texPath = propsResolved.value(key).toString();
				// If relative path, resolve to user material folder
				if (!texPath.isEmpty() && !texPath.contains("/") && !texPath.contains("\\"))
				{
					QString resolvedPath = QDir(materialFolder).filePath(texPath);
					propsResolved.insert(key, resolvedPath);
				}
			}
		}

		return Material::fromVariantMap(propsResolved);
	});
	s_userMaterialKeys.insert(key);
	
	// Update s_groups
	int sGroupIndex = -1;
	for (int i = 0; i < s_groups.size(); ++i)
	{
		if (s_groups[i].first == groupLabel) { sGroupIndex = i; break; }
	}
	if (sGroupIndex < 0)
	{
		QVector<QPair<QString, QString>> v;
		v.emplace_back(name, key);
		s_groups.emplace_back(groupLabel, std::move(v));
	}
	else
	{
		QVector<QPair<QString, QString>>& v = s_groups[sGroupIndex].second;
		int pos = -1;
		for (int i = 0; i < v.size(); ++i)
		{
			if (v[i].second == key) { pos = i; break; }
		}
		if (pos >= 0) v[pos].first = name;
		else v.emplace_back(name, key);
	}

	Q_EMIT MaterialRegistry::instance().materialsChanged();

	if (err) *err = QString();
	return true;
}

// -------------------- removeUserMaterialFromUserLocation --------------------
bool MaterialLibraryWidget::removeUserMaterialFromUserLocation(const QString& groupLabel,
	const QString& key,
	QWidget* parent,
	QString* err)
{
	if (groupLabel.trimmed().isEmpty())
	{
		if (err) *err = "Empty group label";
		return false;
	}
	if (key.trimmed().isEmpty())
	{
		if (err) *err = "Empty material key";
		return false;
	}

	const QString userPath = userMaterialsFilePath();
	if (!QFile::exists(userPath))
	{
		if (err) *err = QString("User materials file does not exist: %1").arg(userPath);
		return false;
	}

	// Optionally ask user for confirmation
	if (parent)
	{
		QMessageBox::StandardButton reply =
			QMessageBox::question(parent,
				QStringLiteral("Remove Material?"),
				QStringLiteral("Are you sure you want to remove this material from your library?"),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
		if (reply != QMessageBox::Yes)
		{
			if (err) *err = QStringLiteral("User cancelled removal");
			return false;
		}
	}

	// Load existing file
	QFile in(userPath);
	if (!in.open(QIODevice::ReadOnly))
	{
		if (err) *err = QString("Failed to open user materials file '%1' for reading: %2").arg(userPath, in.errorString());
		return false;
	}
	QByteArray data = in.readAll();
	in.close();
	QJsonParseError perr;
	QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
	if (perr.error != QJsonParseError::NoError || !doc.isObject())
	{
		if (err) *err = QString("Failed to parse user materials JSON '%1': %2").arg(userPath, perr.errorString());
		return false;
	}
	QVariantMap rootVar = doc.toVariant().toMap();
	QVariantList groupsList = rootVar.value("groups").toList();

	// Find group and remove the item by key
	int groupIndex = -1;
	for (int i = 0; i < groupsList.size(); ++i)
	{
		QVariantMap g = groupsList[i].toMap();
		if (g.value("label").toString() == groupLabel) { groupIndex = i; break; }
	}
	if (groupIndex < 0)
	{
		if (err) *err = QString("Group '%1' not found in user file").arg(groupLabel);
		return false;
	}

	QVariantMap targetGroup = groupsList[groupIndex].toMap();
	QVariantList itemsList = targetGroup.value("items").toList();
	int foundIndex = -1;
	for (int i = 0; i < itemsList.size(); ++i)
	{
		QVariantMap itm = itemsList[i].toMap();
		if (itm.value("key").toString() == key) { foundIndex = i; break; }
	}
	if (foundIndex < 0)
	{
		if (err) *err = QString("Material key '%1' not found in group '%2'").arg(key, groupLabel);
		return false;
	}

	itemsList.removeAt(foundIndex);
	targetGroup.insert("items", itemsList);
	groupsList[groupIndex] = QVariant(targetGroup);
	rootVar.insert("groups", groupsList);

	// Write file back
	if (!writeVariantRootToFile(rootVar, userPath, err)) return false;

	// Update runtime caches: remove from s_materialMap and s_groups
	s_materialMap.remove(key);
	s_userMaterialKeys.remove(key);

	// Remove from s_groups[groupIndex] too
	int sGroupIndex = -1;
	for (int i = 0; i < s_groups.size(); ++i)
	{
		if (s_groups[i].first == groupLabel) { sGroupIndex = i; break; }
	}
	if (sGroupIndex >= 0)
	{
		QVector<QPair<QString, QString>>& v = s_groups[sGroupIndex].second;
		int pos = -1;
		for (int i = 0; i < v.size(); ++i)
		{
			if (v[i].second == key) { pos = i; break; }
		}
		if (pos >= 0) v.removeAt(pos);
		// If the group becomes empty, optionally remove the group; we keep empty group to mirror file.
	}

	Q_EMIT MaterialRegistry::instance().materialsChanged();

	if (err) *err = QString();
	return true;
}

// -------------------- saveAllUserMaterials --------------------
bool MaterialLibraryWidget::saveAllUserMaterials(const QString& filePath,
	QWidget* parent,
	QString* err)
{
	QString outPath = filePath;
	const QString defaultUserPath = userMaterialsFilePath();
	if (outPath.isEmpty()) outPath = defaultUserPath;

	// If file exists and is different from default and parent given, prompt before overwriting
	if (QFile::exists(outPath) && parent)
	{
		// If saving to default user path, still ask? We'll only ask if it's not default to be safe
		if (outPath != defaultUserPath)
		{
			QMessageBox::StandardButton reply =
				QMessageBox::question(parent,
					QStringLiteral("Overwrite File?"),
					QStringLiteral("The target file already exists. Overwrite it?"),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No);
			if (reply != QMessageBox::Yes)
			{
				if (err) *err = QStringLiteral("User cancelled overwrite");
				return false;
			}
		}
	}

	// Build rootVariant from s_groups & s_materialMap
	QVariantMap rootVar;
	QVariantList groupsList;
	for (const auto& gpair : s_groups)
	{
		QVariantMap gmap;
		gmap.insert("label", gpair.first);
		QVariantList itemsList;
		for (const auto& itemPair : gpair.second)
		{
			const QString displayName = itemPair.first;
			const QString key = itemPair.second;

			// If material factory exists, create material and serialize; else write a bare item with key/name.
			QVariantMap itemMap;
			itemMap.insert("key", key);
			itemMap.insert("name", displayName);

			if (s_materialMap.contains(key))
			{
				// create material instance and call toVariantMap()
				Material m = s_materialMap[key]();
				QVariantMap props = m.toVariantMap();
				// ensure key & name present
				props.insert("key", key);
				props.insert("name", displayName);
				itemMap = props;
			}
			else
			{
				// no factory: keep only key/name
			}
			itemsList.append(QVariant(itemMap));
		}
		gmap.insert("items", itemsList);
		groupsList.append(QVariant(gmap));
	}
	rootVar.insert("groups", groupsList);

	// write to outPath
	if (!writeVariantRootToFile(rootVar, outPath, err)) return false;

	if (err) *err = QString();
	return true;
}

void MaterialLibraryWidget::handleItemEntered(QTreeWidgetItem* item, int column)
{

	QRect rect = visualItemRect(item);
	QFontMetrics metrics(font());
	QString fullText = item->text(column);
	QString elidedText = metrics.elidedText(fullText, Qt::ElideRight, rect.width());

	if (elidedText != fullText)
	{
		item->setToolTip(column, fullText);
	}
	else
	{
		item->setToolTip(column, QString()); // Clear tooltip if not needed
	}
}

void MaterialLibraryWidget::populateMaterials()
{
	clear();

	// Only reload if maps are empty (first load or after clear)
	if (s_groups.isEmpty() && s_materialMap.isEmpty())
	{
		QString pathToTry = s_jsonPath;
		if (pathToTry.isEmpty()) pathToTry = PathUtils::getDataDirectory() + "/data/catalogs/materials.json";
		QString err;
		loadAllMaterials(pathToTry, &err);
	}

	// Build tree from s_groups
	// False positive: QTreeWidgetItem(parent, ...) is Qt's own ownership-
	// via-constructor idiom (both groupItem below, parented to `this`, and
	// its children, parented to groupItem) - the analyzer doesn't model
	// Qt's parent/child deletion, only plain new/delete pairing. Adding an
	// explicit delete here would be a real double-free once Qt's own
	// ownership (clear(), or this widget's own destruction) deletes it.
	for (const auto& gpair : s_groups) // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
	{
		QTreeWidgetItem* groupItem = new QTreeWidgetItem(this, QStringList() << gpair.first);
		for (const auto& itemPair : gpair.second)
		{
			QTreeWidgetItem* child = new QTreeWidgetItem(groupItem, QStringList() << itemPair.first);
			child->setData(0, Qt::UserRole, itemPair.second);
		}
	}

	expandAll();

	// Select first item
	if (topLevelItemCount() > 0)
	{
		QTreeWidgetItem* firstGroup = topLevelItem(0);
		if (firstGroup && firstGroup->childCount() > 0)
		{
			QTreeWidgetItem* firstItem = firstGroup->child(0);
			setCurrentItem(firstItem);
			QString key = firstItem->data(0, Qt::UserRole).toString();
			if (!key.isEmpty() && s_materialMap.contains(key))
				emit materialPreview(s_materialMap[key]());
			else
				emit materialPreview(Material::DEFAULT_MAT());
		}
	}
	else
	{
		emit materialPreview(Material::DEFAULT_MAT());
	}
}


// Helper: register all built-in factories into _materialMap and return the built-in grouping
// Return type: QVector of (groupLabel, vector of (displayName, key))
QVector<QPair<QString, QVector<QPair<QString, QString>>>> MaterialLibraryWidget::populateMaterialMapWithBuiltIns(
	QMap<QString, std::function<Material()>>& materialMap)
{
	QVector<QPair<QString, QVector<QPair<QString, QString>>>> builtInGroups;

	// --- Metals ---
	{
		QString group = QStringLiteral("Metals");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("METAL_ALUMINUM", []() { return Material::METAL_ALUMINUM(); });
		items.emplace_back(QStringLiteral("Aluminum"), QStringLiteral("METAL_ALUMINUM"));

		materialMap.insert("METAL_BRASS", []() { return Material::METAL_BRASS(); });
		items.emplace_back(QStringLiteral("Brass"), QStringLiteral("METAL_BRASS"));

		materialMap.insert("METAL_BRONZE", []() { return Material::METAL_BRONZE(); });
		items.emplace_back(QStringLiteral("Bronze"), QStringLiteral("METAL_BRONZE"));

		materialMap.insert("METAL_CHROME", []() { return Material::METAL_CHROME(); });
		items.emplace_back(QStringLiteral("Chrome"), QStringLiteral("METAL_CHROME"));

		materialMap.insert("METAL_COBALT", []() { return Material::METAL_COBALT(); });
		items.emplace_back(QStringLiteral("Cobalt"), QStringLiteral("METAL_COBALT"));

		materialMap.insert("METAL_COPPER", []() { return Material::METAL_COPPER(); });
		items.emplace_back(QStringLiteral("Copper"), QStringLiteral("METAL_COPPER"));

		materialMap.insert("METAL_GOLD", []() { return Material::METAL_GOLD(); });
		items.emplace_back(QStringLiteral("Gold"), QStringLiteral("METAL_GOLD"));

		materialMap.insert("METAL_IRON_RAW", []() { return Material::METAL_IRON_RAW(); });
		items.emplace_back(QStringLiteral("Iron (Raw)"), QStringLiteral("METAL_IRON_RAW"));

		materialMap.insert("METAL_MAGNESIUM", []() { return Material::METAL_MAGNESIUM(); });
		items.emplace_back(QStringLiteral("Magnesium"), QStringLiteral("METAL_MAGNESIUM"));

		materialMap.insert("METAL_NICKEL", []() { return Material::METAL_NICKEL(); });
		items.emplace_back(QStringLiteral("Nickel"), QStringLiteral("METAL_NICKEL"));

		materialMap.insert("METAL_PEWTER", []() { return Material::METAL_PEWTER(); });
		items.emplace_back(QStringLiteral("Pewter"), QStringLiteral("METAL_PEWTER"));

		materialMap.insert("METAL_PLATINUM", []() { return Material::METAL_PLATINUM(); });
		items.emplace_back(QStringLiteral("Platinum"), QStringLiteral("METAL_PLATINUM"));

		materialMap.insert("METAL_SILVER", []() { return Material::METAL_SILVER(); });
		items.emplace_back(QStringLiteral("Silver"), QStringLiteral("METAL_SILVER"));

		materialMap.insert("METAL_STEEL", []() { return Material::METAL_STEEL(); });
		items.emplace_back(QStringLiteral("Steel"), QStringLiteral("METAL_STEEL"));

		materialMap.insert("METAL_TITANIUM", []() { return Material::METAL_TITANIUM(); });
		items.emplace_back(QStringLiteral("Titanium"), QStringLiteral("METAL_TITANIUM"));

		materialMap.insert("METAL_TUNGSTEN", []() { return Material::METAL_TUNGSTEN(); });
		items.emplace_back(QStringLiteral("Tungsten"), QStringLiteral("METAL_TUNGSTEN"));

		materialMap.insert("METAL_ZINC", []() { return Material::METAL_ZINC(); });
		items.emplace_back(QStringLiteral("Zinc"), QStringLiteral("METAL_ZINC"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Brushed Metals ---
	{
		QString group = QStringLiteral("Brushed Metals");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("BRUSHED_ALUMINUM", []() { return Material::BRUSHED_ALUMINUM(); });
		items.emplace_back(QStringLiteral("Brushed Aluminum"), QStringLiteral("BRUSHED_ALUMINUM"));

		materialMap.insert("BRUSHED_STEEL", []() { return Material::BRUSHED_STEEL(); });
		items.emplace_back(QStringLiteral("Brushed Steel"), QStringLiteral("BRUSHED_STEEL"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Stones & Gems ---
	{
		QString group = QStringLiteral("Stones & Gems");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("STONE_BASALT", []() { return Material::STONE_BASALT(); });
		items.emplace_back(QStringLiteral("Basalt"), QStringLiteral("STONE_BASALT"));

		materialMap.insert("STONE_EMERALD", []() { return Material::STONE_EMERALD(); });
		items.emplace_back(QStringLiteral("Emerald"), QStringLiteral("STONE_EMERALD"));

		materialMap.insert("STONE_GRANITE", []() { return Material::STONE_GRANITE(); });
		items.emplace_back(QStringLiteral("Granite"), QStringLiteral("STONE_GRANITE"));

		materialMap.insert("STONE_JADE", []() { return Material::STONE_JADE(); });
		items.emplace_back(QStringLiteral("Jade"), QStringLiteral("STONE_JADE"));

		materialMap.insert("STONE_LIMESTONE", []() { return Material::STONE_LIMESTONE(); });
		items.emplace_back(QStringLiteral("Limestone"), QStringLiteral("STONE_LIMESTONE"));

		materialMap.insert("STONE_MARBLE", []() { return Material::STONE_MARBLE(); });
		items.emplace_back(QStringLiteral("Marble"), QStringLiteral("STONE_MARBLE"));

		materialMap.insert("STONE_OBSIDIAN", []() { return Material::STONE_OBSIDIAN(); });
		items.emplace_back(QStringLiteral("Obsidian"), QStringLiteral("STONE_OBSIDIAN"));

		materialMap.insert("STONE_PEARL", []() { return Material::STONE_PEARL(); });
		items.emplace_back(QStringLiteral("Pearl"), QStringLiteral("STONE_PEARL"));

		materialMap.insert("STONE_QUARTZITE", []() { return Material::STONE_QUARTZITE(); });
		items.emplace_back(QStringLiteral("Quartzite"), QStringLiteral("STONE_QUARTZITE"));

		materialMap.insert("STONE_RUBY", []() { return Material::STONE_RUBY(); });
		items.emplace_back(QStringLiteral("Ruby"), QStringLiteral("STONE_RUBY"));

		materialMap.insert("STONE_SANDSTONE", []() { return Material::STONE_SANDSTONE(); });
		items.emplace_back(QStringLiteral("Sandstone"), QStringLiteral("STONE_SANDSTONE"));

		materialMap.insert("STONE_SLATE", []() { return Material::STONE_SLATE(); });
		items.emplace_back(QStringLiteral("Slate"), QStringLiteral("STONE_SLATE"));

		materialMap.insert("STONE_SOAPSTONE", []() { return Material::STONE_SOAPSTONE(); });
		items.emplace_back(QStringLiteral("Soapstone"), QStringLiteral("STONE_SOAPSTONE"));

		materialMap.insert("STONE_TRAVERTINE", []() { return Material::STONE_TRAVERTINE(); });
		items.emplace_back(QStringLiteral("Travertine"), QStringLiteral("STONE_TRAVERTINE"));

		materialMap.insert("STONE_TURQUOISE", []() { return Material::STONE_TURQUOISE(); });
		items.emplace_back(QStringLiteral("Turquoise"), QStringLiteral("STONE_TURQUOISE"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Plastics ---
	{
		QString group = QStringLiteral("Plastics");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("BLACK_PLASTIC", []() { return Material::BLACK_PLASTIC(); });
		items.emplace_back(QStringLiteral("Black Plastic"), QStringLiteral("BLACK_PLASTIC"));

		materialMap.insert("BLUE_PLASTIC", []() { return Material::BLUE_PLASTIC(); });
		items.emplace_back(QStringLiteral("Blue Plastic"), QStringLiteral("BLUE_PLASTIC"));

		materialMap.insert("CYAN_PLASTIC", []() { return Material::CYAN_PLASTIC(); });
		items.emplace_back(QStringLiteral("Cyan Plastic"), QStringLiteral("CYAN_PLASTIC"));

		materialMap.insert("GREEN_PLASTIC", []() { return Material::GREEN_PLASTIC(); });
		items.emplace_back(QStringLiteral("Green Plastic"), QStringLiteral("GREEN_PLASTIC"));

		materialMap.insert("MAGENTA_PLASTIC", []() { return Material::MAGENTA_PLASTIC(); });
		items.emplace_back(QStringLiteral("Magenta Plastic"), QStringLiteral("MAGENTA_PLASTIC"));

		materialMap.insert("RED_PLASTIC", []() { return Material::RED_PLASTIC(); });
		items.emplace_back(QStringLiteral("Red Plastic"), QStringLiteral("RED_PLASTIC"));

		materialMap.insert("WHITE_PLASTIC", []() { return Material::WHITE_PLASTIC(); });
		items.emplace_back(QStringLiteral("White Plastic"), QStringLiteral("WHITE_PLASTIC"));

		materialMap.insert("YELLOW_PLASTIC", []() { return Material::YELLOW_PLASTIC(); });
		items.emplace_back(QStringLiteral("Yellow Plastic"), QStringLiteral("YELLOW_PLASTIC"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Rubbers ---
	{
		QString group = QStringLiteral("Rubbers");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("BLACK_RUBBER", []() { return Material::BLACK_RUBBER(); });
		items.emplace_back(QStringLiteral("Black Rubber"), QStringLiteral("BLACK_RUBBER"));

		materialMap.insert("BLUE_RUBBER", []() { return Material::BLUE_RUBBER(); });
		items.emplace_back(QStringLiteral("Blue Rubber"), QStringLiteral("BLUE_RUBBER"));

		materialMap.insert("CYAN_RUBBER", []() { return Material::CYAN_RUBBER(); });
		items.emplace_back(QStringLiteral("Cyan Rubber"), QStringLiteral("CYAN_RUBBER"));

		materialMap.insert("GREEN_RUBBER", []() { return Material::GREEN_RUBBER(); });
		items.emplace_back(QStringLiteral("Green Rubber"), QStringLiteral("GREEN_RUBBER"));

		materialMap.insert("MAGENTA_RUBBER", []() { return Material::MAGENTA_RUBBER(); });
		items.emplace_back(QStringLiteral("Magenta Rubber"), QStringLiteral("MAGENTA_RUBBER"));

		materialMap.insert("RED_RUBBER", []() { return Material::RED_RUBBER(); });
		items.emplace_back(QStringLiteral("Red Rubber"), QStringLiteral("RED_RUBBER"));

		materialMap.insert("WHITE_RUBBER", []() { return Material::WHITE_RUBBER(); });
		items.emplace_back(QStringLiteral("White Rubber"), QStringLiteral("WHITE_RUBBER"));

		materialMap.insert("YELLOW_RUBBER", []() { return Material::YELLOW_RUBBER(); });
		items.emplace_back(QStringLiteral("Yellow Rubber"), QStringLiteral("YELLOW_RUBBER"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Wood Materials ---
	{
		QString group = QStringLiteral("Wood Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("WOOD_BAMBOO", []() { return Material::WOOD_BAMBOO(); });
		items.emplace_back(QStringLiteral("Bamboo"), QStringLiteral("WOOD_BAMBOO"));

		materialMap.insert("WOOD_BIRCH", []() { return Material::WOOD_BIRCH(); });
		items.emplace_back(QStringLiteral("Birch"), QStringLiteral("WOOD_BIRCH"));

		materialMap.insert("WOOD_CEDAR", []() { return Material::WOOD_CEDAR(); });
		items.emplace_back(QStringLiteral("Cedar"), QStringLiteral("WOOD_CEDAR"));

		materialMap.insert("WOOD_CHERRY", []() { return Material::WOOD_CHERRY(); });
		items.emplace_back(QStringLiteral("Cherry"), QStringLiteral("WOOD_CHERRY"));

		materialMap.insert("WOOD_MAPLE", []() { return Material::WOOD_MAPLE(); });
		items.emplace_back(QStringLiteral("Maple"), QStringLiteral("WOOD_MAPLE"));

		materialMap.insert("WOOD_OAK", []() { return Material::WOOD_OAK(); });
		items.emplace_back(QStringLiteral("Oak"), QStringLiteral("WOOD_OAK"));

		materialMap.insert("WOOD_PINE", []() { return Material::WOOD_PINE(); });
		items.emplace_back(QStringLiteral("Pine"), QStringLiteral("WOOD_PINE"));

		materialMap.insert("WOOD_REDWOOD", []() { return Material::WOOD_REDWOOD(); });
		items.emplace_back(QStringLiteral("Redwood"), QStringLiteral("WOOD_REDWOOD"));

		materialMap.insert("WOOD_TEAK", []() { return Material::WOOD_TEAK(); });
		items.emplace_back(QStringLiteral("Teak"), QStringLiteral("WOOD_TEAK"));

		materialMap.insert("WOOD_WALNUT", []() { return Material::WOOD_WALNUT(); });
		items.emplace_back(QStringLiteral("Walnut"), QStringLiteral("WOOD_WALNUT"));

		materialMap.insert("WOOD", []() { return Material::WOOD(); });
		items.emplace_back(QStringLiteral("Wood"), QStringLiteral("WOOD"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Concrete Materials ---
	{
		QString group = QStringLiteral("Concrete Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("CONCRETE", []() { return Material::CONCRETE(); });
		items.emplace_back(QStringLiteral("Concrete"), QStringLiteral("CONCRETE"));

		materialMap.insert("CONCRETE_DARK", []() { return Material::CONCRETE_DARK(); });
		items.emplace_back(QStringLiteral("Concrete Dark"), QStringLiteral("CONCRETE_DARK"));

		materialMap.insert("CONCRETE_LIGHT", []() { return Material::CONCRETE_LIGHT(); });
		items.emplace_back(QStringLiteral("Concrete Light"), QStringLiteral("CONCRETE_LIGHT"));

		materialMap.insert("CONCRETE_POLISHED", []() { return Material::CONCRETE_POLISHED(); });
		items.emplace_back(QStringLiteral("Concrete Polished"), QStringLiteral("CONCRETE_POLISHED"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Sheen Materials (cloth/fabric variants) ---
	{
		QString group = QStringLiteral("Sheen Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("FABRIC", []() { return Material::FABRIC(); });
		items.emplace_back(QStringLiteral("Fabric"), QStringLiteral("FABRIC"));

		materialMap.insert("MICROFIBER_CLOTH", []() { return Material::MICROFIBER_CLOTH(); });
		items.emplace_back(QStringLiteral("Microfiber Cloth"), QStringLiteral("MICROFIBER_CLOTH"));

		materialMap.insert("SATIN_FABRIC", []() { return Material::SATIN_FABRIC(); });
		items.emplace_back(QStringLiteral("Satin Fabric"), QStringLiteral("SATIN_FABRIC"));

		materialMap.insert("VELVET_RED", []() { return Material::VELVET_RED(); });
		items.emplace_back(QStringLiteral("Velvet Red"), QStringLiteral("VELVET_RED"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Leather Materials ---
	{
		QString group = QStringLiteral("Leather Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("LEATHER_BLACK", []() { return Material::LEATHER_BLACK(); });
		items.emplace_back(QStringLiteral("Leather Black"), QStringLiteral("LEATHER_BLACK"));

		materialMap.insert("LEATHER_BROWN", []() { return Material::LEATHER_BROWN(); });
		items.emplace_back(QStringLiteral("Leather Brown"), QStringLiteral("LEATHER_BROWN"));

		materialMap.insert("LEATHER_OXBLOOD", []() { return Material::LEATHER_OXBLOOD(); });
		items.emplace_back(QStringLiteral("Leather Oxblood"), QStringLiteral("LEATHER_OXBLOOD"));

		materialMap.insert("LEATHER_RED", []() { return Material::LEATHER_RED(); });
		items.emplace_back(QStringLiteral("Leather Red"), QStringLiteral("LEATHER_RED"));

		materialMap.insert("LEATHER_TAN", []() { return Material::LEATHER_TAN(); });
		items.emplace_back(QStringLiteral("Leather Tan"), QStringLiteral("LEATHER_TAN"));

		materialMap.insert("LEATHER_WHITE", []() { return Material::LEATHER_WHITE(); });
		items.emplace_back(QStringLiteral("Leather White"), QStringLiteral("LEATHER_WHITE"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Clearcoat / Car Paints ---
	{
		QString group = QStringLiteral("Clearcoat Materials");
		QVector<QPair<QString, QString>> items;

		materialMap.insert("CAR_PAINT_BURGUNDY", []() { return Material::CAR_PAINT_BURGUNDY(); });
		items.emplace_back(QStringLiteral("Car Paint Burgundy"), QStringLiteral("CAR_PAINT_BURGUNDY"));

		materialMap.insert("CAR_PAINT_CANDY_APPLE_RED", []() { return Material::CAR_PAINT_CANDY_APPLE_RED(); });
		items.emplace_back(QStringLiteral("Car Paint Candy Apple Red"), QStringLiteral("CAR_PAINT_CANDY_APPLE_RED"));

		materialMap.insert("CAR_PAINT_CHARCOAL_GRAY", []() { return Material::CAR_PAINT_CHARCOAL_GRAY(); });
		items.emplace_back(QStringLiteral("Car Paint Charcoal Gray"), QStringLiteral("CAR_PAINT_CHARCOAL_GRAY"));

		materialMap.insert("CAR_PAINT_CORAL", []() { return Material::CAR_PAINT_CORAL(); });
		items.emplace_back(QStringLiteral("Car Paint Coral"), QStringLiteral("CAR_PAINT_CORAL"));

		materialMap.insert("CAR_PAINT_CREAM_YELLOW", []() { return Material::CAR_PAINT_CREAM_YELLOW(); });
		items.emplace_back(QStringLiteral("Car Paint Cream Yellow"), QStringLiteral("CAR_PAINT_CREAM_YELLOW"));

		materialMap.insert("CAR_PAINT_DEEP_METALLIC_BLUE", []() { return Material::CAR_PAINT_DEEP_METALLIC_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Deep Metallic Blue"), QStringLiteral("CAR_PAINT_DEEP_METALLIC_BLUE"));

		materialMap.insert("CAR_PAINT_FOREST_GREEN", []() { return Material::CAR_PAINT_FOREST_GREEN(); });
		items.emplace_back(QStringLiteral("Car Paint Forest Green"), QStringLiteral("CAR_PAINT_FOREST_GREEN"));

		materialMap.insert("CAR_PAINT_GLOSSY_BLACK", []() { return Material::CAR_PAINT_GLOSSY_BLACK(); });
		items.emplace_back(QStringLiteral("Car Paint Glossy Black"), QStringLiteral("CAR_PAINT_GLOSSY_BLACK"));

		materialMap.insert("CAR_PAINT_GLOSSY_ORANGE", []() { return Material::CAR_PAINT_GLOSSY_ORANGE(); });
		items.emplace_back(QStringLiteral("Car Paint Glossy Orange"), QStringLiteral("CAR_PAINT_GLOSSY_ORANGE"));

		materialMap.insert("CAR_PAINT_GLOSSY_WHITE", []() { return Material::CAR_PAINT_GLOSSY_WHITE(); });
		items.emplace_back(QStringLiteral("Car Paint Glossy White"), QStringLiteral("CAR_PAINT_GLOSSY_WHITE"));

		materialMap.insert("CAR_PAINT_GLOSSY_YELLOW", []() { return Material::CAR_PAINT_GLOSSY_YELLOW(); });
		items.emplace_back(QStringLiteral("Car Paint Glossy Yellow"), QStringLiteral("CAR_PAINT_GLOSSY_YELLOW"));

		materialMap.insert("CAR_PAINT_IRIDESCENT_GREEN", []() { return Material::CAR_PAINT_IRIDESCENT_GREEN(); });
		items.emplace_back(QStringLiteral("Car Paint Iridescent Green"), QStringLiteral("CAR_PAINT_IRIDESCENT_GREEN"));

		materialMap.insert("CAR_PAINT_LAVENDER", []() { return Material::CAR_PAINT_LAVENDER(); });
		items.emplace_back(QStringLiteral("Car Paint Lavender"), QStringLiteral("CAR_PAINT_LAVENDER"));

		materialMap.insert("CAR_PAINT_MATTE_RED", []() { return Material::CAR_PAINT_MATTE_RED(); });
		items.emplace_back(QStringLiteral("Car Paint Matte Red"), QStringLiteral("CAR_PAINT_MATTE_RED"));

		materialMap.insert("CAR_PAINT_METALLIC_BLUE", []() { return Material::CAR_PAINT_METALLIC_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Blue"), QStringLiteral("CAR_PAINT_METALLIC_BLUE"));

		materialMap.insert("CAR_PAINT_METALLIC_BRONZE", []() { return Material::CAR_PAINT_METALLIC_BRONZE(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Bronze"), QStringLiteral("CAR_PAINT_METALLIC_BRONZE"));

		materialMap.insert("CAR_PAINT_METALLIC_CHAMPAGNE", []() { return Material::CAR_PAINT_METALLIC_CHAMPAGNE(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Champagne"), QStringLiteral("CAR_PAINT_METALLIC_CHAMPAGNE"));

		materialMap.insert("CAR_PAINT_METALLIC_COPPER", []() { return Material::CAR_PAINT_METALLIC_COPPER(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Copper"), QStringLiteral("CAR_PAINT_METALLIC_COPPER"));

		materialMap.insert("CAR_PAINT_METALLIC_GOLD", []() { return Material::CAR_PAINT_METALLIC_GOLD(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Gold"), QStringLiteral("CAR_PAINT_METALLIC_GOLD"));

		materialMap.insert("CAR_PAINT_METALLIC_GREEN", []() { return Material::CAR_PAINT_METALLIC_GREEN(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Green"), QStringLiteral("CAR_PAINT_METALLIC_GREEN"));

		materialMap.insert("CAR_PAINT_METALLIC_GUNMETAL", []() { return Material::CAR_PAINT_METALLIC_GUNMETAL(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Gunmetal"), QStringLiteral("CAR_PAINT_METALLIC_GUNMETAL"));

		materialMap.insert("CAR_PAINT_METALLIC_PURPLE", []() { return Material::CAR_PAINT_METALLIC_PURPLE(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Purple"), QStringLiteral("CAR_PAINT_METALLIC_PURPLE"));

		materialMap.insert("CAR_PAINT_METALLIC_RED", []() { return Material::CAR_PAINT_METALLIC_RED(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Red"), QStringLiteral("CAR_PAINT_METALLIC_RED"));

		materialMap.insert("CAR_PAINT_METALLIC_SILVER", []() { return Material::CAR_PAINT_METALLIC_SILVER(); });
		items.emplace_back(QStringLiteral("Car Paint Metallic Silver"), QStringLiteral("CAR_PAINT_METALLIC_SILVER"));

		materialMap.insert("CAR_PAINT_MIDNIGHT_BLUE", []() { return Material::CAR_PAINT_MIDNIGHT_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Midnight Blue"), QStringLiteral("CAR_PAINT_MIDNIGHT_BLUE"));

		materialMap.insert("CAR_PAINT_MINT_GREEN", []() { return Material::CAR_PAINT_MINT_GREEN(); });
		items.emplace_back(QStringLiteral("Car Paint Mint Green"), QStringLiteral("CAR_PAINT_MINT_GREEN"));

		materialMap.insert("CAR_PAINT_PEARL", []() { return Material::CAR_PAINT_PEARL(); });
		items.emplace_back(QStringLiteral("Car Paint Pearl"), QStringLiteral("CAR_PAINT_PEARL"));

		materialMap.insert("CAR_PAINT_PEARLESCENT_BLUE", []() { return Material::CAR_PAINT_PEARLESCENT_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Pearlescent Blue"), QStringLiteral("CAR_PAINT_PEARLESCENT_BLUE"));

		materialMap.insert("CAR_PAINT_POWDER_BLUE", []() { return Material::CAR_PAINT_POWDER_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Powder Blue"), QStringLiteral("CAR_PAINT_POWDER_BLUE"));

		materialMap.insert("CAR_PAINT_RED", []() { return Material::CAR_PAINT_RED(); });
		items.emplace_back(QStringLiteral("Car Paint Red"), QStringLiteral("CAR_PAINT_RED"));

		materialMap.insert("CAR_PAINT_SATIN_GRAY", []() { return Material::CAR_PAINT_SATIN_GRAY(); });
		items.emplace_back(QStringLiteral("Car Paint Satin Gray"), QStringLiteral("CAR_PAINT_SATIN_GRAY"));

		materialMap.insert("CAR_PAINT_SLATE_BLUE", []() { return Material::CAR_PAINT_SLATE_BLUE(); });
		items.emplace_back(QStringLiteral("Car Paint Slate Blue"), QStringLiteral("CAR_PAINT_SLATE_BLUE"));

		materialMap.insert("CAR_PAINT_TEAL", []() { return Material::CAR_PAINT_TEAL(); });
		items.emplace_back(QStringLiteral("Car Paint Teal"), QStringLiteral("CAR_PAINT_TEAL"));

		materialMap.insert("CAR_PAINT_WHITE", []() { return Material::CAR_PAINT_WHITE(); });
		items.emplace_back(QStringLiteral("Car Paint White"), QStringLiteral("CAR_PAINT_WHITE"));

		materialMap.insert("MATTE_GREY", []() { return Material::MATTE_GREY(); });
		items.emplace_back(QStringLiteral("Matte Grey"), QStringLiteral("MATTE_GREY"));

		materialMap.insert("PIANO_BLACK", []() { return Material::PIANO_BLACK(); });
		items.emplace_back(QStringLiteral("Piano Black"), QStringLiteral("PIANO_BLACK"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Transmission Materials (glass, crystal) ---
	{
		QString group = QStringLiteral("Transmission Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("COLORED_GLASS_GREEN", []() { return Material::COLORED_GLASS_GREEN(); });
		items.emplace_back(QStringLiteral("Colored Glass (Green)"), QStringLiteral("COLORED_GLASS_GREEN"));

		materialMap.insert("CRYSTAL_QUARTZ", []() { return Material::CRYSTAL_QUARTZ(); });
		items.emplace_back(QStringLiteral("Crystal Quartz"), QStringLiteral("CRYSTAL_QUARTZ"));

		materialMap.insert("FROSTED_GLASS", []() { return Material::FROSTED_GLASS(); });
		items.emplace_back(QStringLiteral("Frosted Glass"), QStringLiteral("FROSTED_GLASS"));

		materialMap.insert("GLASS", []() { return Material::GLASS(); });
		items.emplace_back(QStringLiteral("Glass"), QStringLiteral("GLASS"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Emissive Materials ---
	{
		QString group = QStringLiteral("Emissive Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("LED_BLUE", []() { return Material::LED_BLUE(); });
		items.emplace_back(QStringLiteral("LED Blue"), QStringLiteral("LED_BLUE"));

		materialMap.insert("LED_GREEN", []() { return Material::LED_GREEN(); });
		items.emplace_back(QStringLiteral("LED Green"), QStringLiteral("LED_GREEN"));

		materialMap.insert("LED_RED", []() { return Material::LED_RED(); });
		items.emplace_back(QStringLiteral("LED Red"), QStringLiteral("LED_RED"));

		materialMap.insert("LED_WHITE", []() { return Material::LED_WHITE(); });
		items.emplace_back(QStringLiteral("LED White"), QStringLiteral("LED_WHITE"));

		materialMap.insert("LED_YELLOW", []() { return Material::LED_YELLOW(); });
		items.emplace_back(QStringLiteral("LED Yellow"), QStringLiteral("LED_YELLOW"));

		materialMap.insert("NEON_BLUE", []() { return Material::NEON_BLUE(); });
		items.emplace_back(QStringLiteral("Neon Blue"), QStringLiteral("NEON_BLUE"));

		materialMap.insert("NEON_GREEN", []() { return Material::NEON_GREEN(); });
		items.emplace_back(QStringLiteral("Neon Green"), QStringLiteral("NEON_GREEN"));

		materialMap.insert("NEON_RED", []() { return Material::NEON_RED(); });
		items.emplace_back(QStringLiteral("Neon Red"), QStringLiteral("NEON_RED"));

		materialMap.insert("NEON_YELLOW", []() { return Material::NEON_YELLOW(); });
		items.emplace_back(QStringLiteral("Neon Yellow"), QStringLiteral("NEON_YELLOW"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Complex Materials ---
	{
		QString group = QStringLiteral("Complex Materials");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("CARBON_FIBER", []() { return Material::CARBON_FIBER(); });
		items.emplace_back(QStringLiteral("Carbon Fiber"), QStringLiteral("CARBON_FIBER"));

		materialMap.insert("IRIDESCENT_SOAP_BUBBLE", []() { return Material::IRIDESCENT_SOAP_BUBBLE(); });
		items.emplace_back(QStringLiteral("Iridescent Soap Bubble"), QStringLiteral("IRIDESCENT_SOAP_BUBBLE"));

		materialMap.insert("WET_ASPHALT", []() { return Material::WET_ASPHALT(); });
		items.emplace_back(QStringLiteral("Wet Asphalt"), QStringLiteral("WET_ASPHALT"));

		builtInGroups.emplace_back(group, items);
	}

	// --- Special ---
	{
		QString group = QStringLiteral("Special");
		QVector<QPair<QString, QString>> items;
		materialMap.insert("CERAMIC", []() { return Material::CERAMIC(); });
		items.emplace_back(QStringLiteral("Ceramic"), QStringLiteral("CERAMIC"));

		materialMap.insert("DIAMOND", []() { return Material::DIAMOND(); });
		items.emplace_back(QStringLiteral("Diamond"), QStringLiteral("DIAMOND"));

		materialMap.insert("PAPER", []() { return Material::PAPER(); });
		items.emplace_back(QStringLiteral("Paper"), QStringLiteral("PAPER"));

		materialMap.insert("SKIN", []() { return Material::SKIN(); });
		items.emplace_back(QStringLiteral("Skin"), QStringLiteral("SKIN"));

		materialMap.insert("WATER", []() { return Material::WATER(); });
		items.emplace_back(QStringLiteral("Water"), QStringLiteral("WATER"));

		builtInGroups.emplace_back(group, items);
	}

	return builtInGroups;
}

// ============================================================================
// Public Accessors for User Materials Path
// ============================================================================

QString MaterialLibraryWidget::userMaterialsRootPath()
{
	// Return the directory containing user materials.json (in AppData, not shipped catalogs)
	// This is the root folder where user materials are stored
	if (!s_userJsonPath.isEmpty()) {
		return QFileInfo(s_userJsonPath).dir().absolutePath();
	}
	// Fallback: compute it ourselves if not yet initialized
	QString path = userMaterialsFilePath(); // Get full path to materials.json in AppData
	return QFileInfo(path).dir().absolutePath();
}


