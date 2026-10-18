// MaterialScanner.h
#pragma once

#include <QMap>
#include <QString>
#include <QComboBox>
#include <QListWidget>
#include <QTreeWidget>

using TextureMap = QMap<QString, QString>;         // key: "albedo","metallic","normal","ao" -> full path
using MaterialsMap = QMap<QString, TextureMap>;    // key: material folder name -> TextureMap

class MaterialScanner
{
public:
    // Scan rootFolder and return map of materials
    static MaterialsMap parseMaterialsFolder(const QString& rootFolder);

    // Populate combo box with keys of map. Keeps the same ordering as returned by parseMaterialsFolder.
    static void populateWithMaterials(QComboBox* combo, const MaterialsMap& materials);

	// Populate list box with keys of map. Keeps the same ordering as returned by parseMaterialsFolder.
	static void populateWithMaterials(QListWidget* list, const MaterialsMap& materials);

	// Populate tree widget with keys of map. Keeps the same ordering as returned by parseMaterialsFolder.
	// Group by material types (if type can be determined from name)
	static void populateWithMaterials(QTreeWidget* tree, const MaterialsMap& materials);
};
