#pragma once

#include <QWidget>
#include <QIcon>
#include <QHash>
#include <QPushButton>
#include <QLabel>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QMenu>
#include <QImage>
#include <QPainter>
#include <QVector3D>
#include <QDataStream>
#include <QFileDialog>
#include <QColorDialog>
#include <QComboBox>
#include <QCloseEvent>
#include <QUuid>

#include "Material.h"

// Cache structure for unsaved materials - stores material with its metadata
struct CachedMaterial
{
	Material material;        // The actual material with all properties
	QString name;               // User-provided name at creation time (e.g., "Red Plastic")
	QString group;              // Category/group at creation time (e.g., "Custom Materials")
};

// Forward declarations
class ModelViewer;
class ViewportWidget;
class MaterialPreviewWidget;

namespace Ui
{
	class MaterialPropertiesPanel;
}

class MaterialPropertiesPanel : public QWidget
{
	Q_OBJECT
public:
	explicit MaterialPropertiesPanel(QWidget* parent = nullptr);
	~MaterialPropertiesPanel() override;

	// Material binding
	void bindMaterial(Material* material);
	Material* material() const { return _material; }

	// Initialization
	void initialize(ModelViewer* modelViewer, ViewportWidget* viewportWidget);

	// Preview widget access
	class MaterialPreviewWidget* getPreviewWidget() const { return _preview; }

	// Property getters - all 37 scalar properties
	QVector3D getAlbedoColor() const;
	float getMetalness() const;
	float getRoughness() const;
	float getIOR() const;
	float getOpacity() const;
	float getEmissiveStrength() const;
	QVector3D getEmissiveColor() const;
	float getClearcoat() const;
	float getClearcoatRoughness() const;
	QVector3D getSheenColor() const;
	float getSheenRoughness() const;
	float getTransmission() const;
	float getThickness() const;
	float getNormalScale() const;
	float getHeightScale() const;
	float getClearcoatNormalScale() const;
	float getOcclusionStrength() const;
	float getAnisotropyStrength() const;
	float getAnisotropyRotation() const;
	float getDiffuseTransmissionFactor() const;
	QVector3D getDiffuseTransmissionColor() const;
	float getSpecularFactor() const;
	QVector3D getSpecularColor() const;
	int getShadingModel() const;
	int getBlendMode() const;
	bool getTwoSided() const;
	bool getWireframe() const;
	float getAlphaThreshold() const;
	bool getUnlit() const;
	float getIridescenceStrength() const;
	float getIridescenceThickness() const;
	float getIridescenceIOR() const;
	float getIridescenceThinFilmThickness() const;

	// Texture path getter
	QString getTexturePath(Material::TextureType type) const;

	// Unsaved materials tracking (used by ModelViewer on close)
	QSet<QString> getUnsavedMaterialKeys() const { return _unsavedMaterialKeys; }
	void removeMaterialFromUnsaved(const QString& key) { _unsavedMaterialKeys.remove(key); }

	// Batch save operations (for unsaved materials before close)
	void beginSaveUnsavedMaterials();   // Block signals before batch save
	void endSaveUnsavedMaterials();     // Unblock signals and refresh tree after batch save

	// Mesh material editing
	void createUnsavedMaterialFromMesh(
		const QString& meshName,
		const Material& meshMaterial);
	void setEditingMeshUuid(const QUuid& uuid);

	// Cleanup helper for temporary mesh materials category
	void removeEmptyMeshMaterialsCategory();

signals:
	void materialChanged(Material* material);
	void materialApplied(const Material& material);
	void meshMaterialApplied(const QUuid& meshUuid, const Material& material);
	void textureSamplerChanged(Material* material, Material::TextureType type);
	void textureCacheClearRequested();

private slots:
	// Scalar property handlers
	void onAlbedoColorPicked();
	void onMetallicChanged(double value);
	void onRoughnessChanged(double value);
	void onIORChanged(double value);
	void onOpacityChanged(double value);
	void onEmissiveStrengthChanged(double value);
	void onEmissiveColorPicked();
	void onClearcoatChanged(double value);
	void onClearcoatRoughnessChanged(double value);
	void onSheenColorPicked();
	void onSheenRoughnessChanged(double value);
	void onTransmissionChanged(double value);
	void onThicknessChanged(double value);
	void onNormalScaleChanged(double value);
	void onHeightScaleChanged(double value);
	void onClearcoatNormalScaleChanged(double value);
	void onOcclusionStrengthChanged(double value);
	void onAnisotropyStrengthChanged(double value);
	void onAnisotropyRotationChanged(double value);
	void onDiffuseTransmissionFactorChanged(double value);
	void onDiffuseTransmissionColorPicked();
	void onSpecularFactorChanged(double value);
	void onSpecularColorPicked();
	void onAttenuationColorPicked();
	void onShadingModelChanged(int index);
	void onBlendModeChanged(int index);
	void onTwoSidedToggled(bool checked);
	void onWireframeToggled(bool checked);
	void onAlphaThresholdChanged(double value);
	void onUnlitToggled(bool checked);
	void onIridescenceStrengthChanged(double value);
	void onIridescenceThicknessChanged(double value);
	void onIridescenceIORChanged(double value);
	void onIridescenceThinFilmThicknessChanged(double value);

	// Texture handlers
	void onTextureButtonClicked(Material::TextureType type);
	void onClearAllTextures();

	// Preset handlers
	void onMaterialPresetSelected(const Material& material);
	void onMaterialDoubleClicked(const Material& material);
	void onCreateNewMaterial();  // Create new material (in-memory, unsaved)
	void onSaveToLibrary();      // Save or overwrite user material
	void onSaveAsToLibrary();    // Save as new copy of current material
	void onRefreshSelectedMaterialFromLibrary();      // Refresh material library from disk
	void onDeleteMaterial();
	void onRenameMaterial();     // Rename user-created material

	void onContextMenu(const QPoint& pos);

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;
	void closeEvent(QCloseEvent* event) override;

private:
	// Texture management
	struct MapSlot
	{
		QPushButton* button = nullptr;           // square thumbnail button
		QLabel* label = nullptr;                 // text under the button
		QToolButton* gear = nullptr;             // optional channel packing gear
		QToolButton* transformButton = nullptr;  // transform/sampler button
		QLabel* metaUv = nullptr;                // UV slot readout
		QLabel* metaColorSpace = nullptr;        // sRGB / Linear
		QLabel* metaPacking = nullptr;           // packed channel summary
		QString key;                             // e.g., "albedo", "roughness"
		Material::TextureType type;            // enum type
	};

	// Texture management helpers
	void registerTextureMaps();
	void setupTextureMetadataUI();
	void connectTextureSignals();
	void applyButtonEmptyIcon(MapSlot& m);
	void applyButtonImageIcon(MapSlot& m, const QString& file);
	QIcon makeIconFromFile(const QString& file, int edge = 90) const;
	static QIcon makeCheckerIcon(int w = 90, int h = 90, int cell = 8);

	// Material sync
	void setTextureMapPath(Material::TextureType type, const QString& file);
	void clearTextureMap(Material::TextureType type);
	void clearAllTexturesMaps();
	QString textureMapPath(Material::TextureType type) const;

	// Material loading
	void loadTextureImageFiles();  // Load texture image files from disk for current material
	void loadMaterialTexturesFromKey(const QString& materialKey);

	// Load scalar values from material into UI controls
	void loadScalarValuesFromMaterial();
	void updateScalarUI();

	// Texture operations
	void updateTexturePreview(Material::TextureType type);
	void updateTextureMetadata(Material::TextureType type);
	void openPackingDialogFor(Material::TextureType type);

	// Slot handlers
	void onColorPickerClicked(Material::TextureType type);
	void onFactorChanged(Material::TextureType type);
	void onTransformButtonClicked(Material::TextureType type);
	void onSearchTextChanged(const QString& text);

	// Helper to update unsaved material in shared map when scalars change
	void updateUnsavedMaterialInMap();
	void markMaterialAsModified();  // Mark non-factory materials as having unsaved changes
	void updateRefreshButtonState();  // Enable/disable refresh button based on material state
	void restoreAsterisksForUnsavedMaterials();  // Re-add asterisks after tree refresh

	void updatePreview();

	// Preset operations
	void populatePresetTree();
	void selectPresetInTree(const QString& presetName, bool userPreset);
	void applyMaterialPreset(const QString& presetName, const QString& presetFolder = QString(), bool userPreset = false);
	void loadMaterialPresetMetadata(const QString& presetName);
	bool saveCurrentPresetMetadata();
	QString materialLibraryRoot() const;
	QString userMaterialLibraryRoot() const;
	QString currentPresetMetadataPath() const;

	// Material library signal management
	void connectMaterialLibrarySignals();

	// Material save/unsaved state tracking
	QSet<QString> _unsavedMaterialKeys;  // Keys of materials created but not yet saved to JSON
	QMap<QString, CachedMaterial>* _materialCacheRef = nullptr;  // Reference to ModelViewer's MDI-scoped cache
	QString _currentMaterialKey;         // Key of currently bound material (if any)
	QString _currentMaterialGroup;       // Group/category of currently bound material (if unsaved)

	// Members
	Ui::MaterialPropertiesPanel* _ui = nullptr;
	Material* _material = nullptr;
	ModelViewer* _modelViewer = nullptr;
	ViewportWidget* _viewportWidget = nullptr;
	MaterialPreviewWidget* _preview = nullptr;

	QHash<Material::TextureType, MapSlot> _textureSlots;
	QIcon _checkerIcon;

	QString _lastUsedTextureFolder;
	QString _currentPresetName;
	QString _currentPresetFolder;
	bool _currentPresetIsUser = false;

	bool _updateInProgress = false;
	bool _texturesDirty = false;  // Track if current material has unsaved texture changes

	QUuid _editingMeshUuid;  // UUID of mesh being edited (null if not editing mesh material)

	// Helper to save textures before switching materials
	void saveCurrentMaterialTexturesBeforeSwitch();
};
