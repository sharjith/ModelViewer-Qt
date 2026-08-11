#include "ChannelPackingEditorDialog.h"
#include "Material.h"
#include "LanguageManager.h"
#include "MaterialLibraryWidget.h"
#include "MaterialPreviewWidget.h"
#include "MaterialPropertiesPanel.h"
#include "ModelViewer.h"
#include "MaterialRegistry.h"
#include "MaterialTextureLibrary.h"
#include "PathUtils.h"
#include "TextureParametersDialog.h"
#include "ui_MaterialPropertiesPanel.h"
#include <QApplication>
#include <QColorDialog>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QCheckBox>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QBoxLayout>
#include <functional>

// ============================================================================
// JSON Helper Functions
// ============================================================================

namespace
{
	constexpr int PresetNameRole = Qt::UserRole;
	constexpr int PresetFolderRole = Qt::UserRole + 1;
	constexpr int PresetIsUserRole = Qt::UserRole + 2;

	struct PreviewTexModeOption
	{
		const char* label;
		int mode;
		bool separator;
	};

	void populatePreviewTexModeCombo(QComboBox* combo)
	{
		if (!combo) return;

		const int previousMode = combo->currentData().isValid()
			? combo->currentData().toInt()
			: static_cast<int>(TexViewMode::All);

		const PreviewTexModeOption options[] = {
			{ QT_TR_NOOP("All"), static_cast<int>(TexViewMode::All), false },
			{ nullptr, 0, true },
			{ QT_TR_NOOP("Albedo"), static_cast<int>(TexViewMode::Albedo), false },
			{ QT_TR_NOOP("Metallic"), static_cast<int>(TexViewMode::Metalness), false },
			{ QT_TR_NOOP("Roughness"), static_cast<int>(TexViewMode::Roughness), false },
			{ QT_TR_NOOP("AO"), static_cast<int>(TexViewMode::AO), false },
			{ QT_TR_NOOP("Emissive"), static_cast<int>(TexViewMode::Emissive), false },
			{ QT_TR_NOOP("Normal Map"), static_cast<int>(TexViewMode::Normal), false },
			{ QT_TR_NOOP("Height"), static_cast<int>(TexViewMode::Height), false },
			{ QT_TR_NOOP("Opacity"), static_cast<int>(TexViewMode::Opacity), false },
			{ nullptr, 0, true },
			{ QT_TR_NOOP("Transmission Strength"), static_cast<int>(TexViewMode::Transmission), false },
			{ QT_TR_NOOP("Sheen Color"), static_cast<int>(TexViewMode::SheenColor), false },
			{ QT_TR_NOOP("Sheen Roughness"), static_cast<int>(TexViewMode::SheenRoughness), false },
			{ QT_TR_NOOP("Clearcoat Strength"), static_cast<int>(TexViewMode::ClearcoatColor), false },
			{ QT_TR_NOOP("Clearcoat Roughness"), static_cast<int>(TexViewMode::ClearcoatRoughness), false },
			{ QT_TR_NOOP("Clearcoat Normal"), static_cast<int>(TexViewMode::ClearcoatNormal), false },
			{ QT_TR_NOOP("Specular Strength"), static_cast<int>(TexViewMode::SpecularFactor), false },
			{ QT_TR_NOOP("Specular Color"), static_cast<int>(TexViewMode::SpecularColor), false },
			{ QT_TR_NOOP("Anisotropic Strength"), static_cast<int>(TexViewMode::Anisotropy), false },
			{ QT_TR_NOOP("Iridescence Strength"), static_cast<int>(TexViewMode::Iridescence), false },
			{ QT_TR_NOOP("Iridescence Thickness"), static_cast<int>(TexViewMode::IridescenceThickness), false },
			{ QT_TR_NOOP("Volume Thickness"), static_cast<int>(TexViewMode::Thickness), false },
			{ QT_TR_NOOP("Diffuse Transmission Strength"), static_cast<int>(TexViewMode::DiffuseTransmission), false },
			{ QT_TR_NOOP("Diffuse Transmission Color"), static_cast<int>(TexViewMode::DiffuseTransmissionColor), false },
			{ QT_TR_NOOP("IOR"), static_cast<int>(TexViewMode::IOR), false },
		};

		QSignalBlocker blocker(combo);
		combo->clear();

		for (const PreviewTexModeOption& option : options)
		{
			if (option.separator)
			{
				combo->insertSeparator(combo->count());
				continue;
			}

			combo->addItem(QCoreApplication::translate("MaterialPropertiesPanel", option.label), option.mode);
		}

		int index = combo->findData(previousMode);
		if (index < 0)
			index = combo->findData(static_cast<int>(TexViewMode::All));
		if (index >= 0)
			combo->setCurrentIndex(index);
	}

	QString wrapModeToJson(GLenum mode)
	{
		switch (mode)
		{
		case GL_CLAMP_TO_EDGE:   return QStringLiteral("clamp_to_edge");
		case GL_MIRRORED_REPEAT: return QStringLiteral("mirrored_repeat");
		case GL_REPEAT:
		default:                 return QStringLiteral("repeat");
		}
	}

	GLenum wrapModeFromJson(const QString& mode, GLenum fallback = GL_REPEAT)
	{
		const QString lower = mode.trimmed().toLower();
		if (lower == QLatin1String("clamp") || lower == QLatin1String("clamp_to_edge"))
			return GL_CLAMP_TO_EDGE;
		if (lower == QLatin1String("mirror") || lower == QLatin1String("mirrored_repeat"))
			return GL_MIRRORED_REPEAT;
		return fallback;
	}

	QString filterToJson(GLenum filter)
	{
		switch (filter)
		{
		case GL_NEAREST:                return QStringLiteral("nearest");
		case GL_LINEAR:                 return QStringLiteral("linear");
		case GL_NEAREST_MIPMAP_NEAREST: return QStringLiteral("nearest_mipmap_nearest");
		case GL_LINEAR_MIPMAP_NEAREST:  return QStringLiteral("linear_mipmap_nearest");
		case GL_NEAREST_MIPMAP_LINEAR:  return QStringLiteral("nearest_mipmap_linear");
		case GL_LINEAR_MIPMAP_LINEAR:
		default:                        return QStringLiteral("linear_mipmap_linear");
		}
	}

	GLenum filterFromJson(const QString& filter, GLenum fallback)
	{
		const QString lower = filter.trimmed().toLower();
		if (lower == QLatin1String("nearest")) return GL_NEAREST;
		if (lower == QLatin1String("linear")) return GL_LINEAR;
		if (lower == QLatin1String("nearest_mipmap_nearest")) return GL_NEAREST_MIPMAP_NEAREST;
		if (lower == QLatin1String("linear_mipmap_nearest")) return GL_LINEAR_MIPMAP_NEAREST;
		if (lower == QLatin1String("nearest_mipmap_linear")) return GL_NEAREST_MIPMAP_LINEAR;
		if (lower == QLatin1String("linear_mipmap_linear")) return GL_LINEAR_MIPMAP_LINEAR;
		return fallback;
	}

	QString channelPackingSummary(const Material::ChannelPacking& packing)
	{
		static const char* channels[] = { "R", "G", "B", "A" };
		QString summary = (packing.channel >= 0 && packing.channel < 4)
			? QString::fromLatin1(channels[packing.channel])
			: QStringLiteral("-");

		if (packing.invert)
			summary.append(QStringLiteral(" inv"));
		if (!qFuzzyCompare(packing.scale, 1.0f))
			summary.append(QStringLiteral(" x%1").arg(packing.scale, 0, 'g', 2));
		if (!qFuzzyIsNull(packing.bias))
			summary.append(QStringLiteral(" %1").arg(packing.bias, 0, 'g', 2));

		return summary;
	}

	QJsonArray vec2ToJsonArray(const glm::vec2& v)
	{
		return QJsonArray{ v.x, v.y };
	}

	QJsonArray vec3ToJsonArray(const QVector3D& v)
	{
		return QJsonArray{ v.x(), v.y(), v.z() };
	}

	glm::vec2 vec2FromJson(const QJsonValue& value, const glm::vec2& fallback)
	{
		if (!value.isArray()) return fallback;
		const QJsonArray arr = value.toArray();
		if (arr.size() < 2) return fallback;
		return glm::vec2(
			static_cast<float>(arr.at(0).toDouble(fallback.x)),
			static_cast<float>(arr.at(1).toDouble(fallback.y)));
	}

	QVector3D vec3FromJson(const QJsonValue& value, const QVector3D& fallback)
	{
		if (!value.isArray()) return fallback;
		const QJsonArray arr = value.toArray();
		if (arr.size() < 3) return fallback;
		return QVector3D(
			static_cast<float>(arr.at(0).toDouble(fallback.x())),
			static_cast<float>(arr.at(1).toDouble(fallback.y())),
			static_cast<float>(arr.at(2).toDouble(fallback.z())));
	}
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

MaterialPropertiesPanel::MaterialPropertiesPanel(QWidget* parent)
	: QWidget(parent)
	, _ui(new Ui::MaterialPropertiesPanel)
{
	_ui->setupUi(this);

	if (_ui->comboBoxTexMode)
		populatePreviewTexModeCombo(_ui->comboBoxTexMode);

	connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
		_ui->retranslateUi(this);
		if (_ui->comboBoxTexMode)
			populatePreviewTexModeCombo(_ui->comboBoxTexMode);
		});

	// Enable right-click context menu
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested,
		this, &MaterialPropertiesPanel::onContextMenu);

	_preview = qobject_cast<MaterialPreviewWidget*>(_ui->previewWidget);
	if (_preview)
	{
		_preview->setPreviewProfile(PreviewProfile::TextureAuthoring);
	}

	_checkerIcon = makeCheckerIcon();
	registerTextureMaps();
	setupTextureMetadataUI();
	connectTextureSignals();

	// Connect search functionality for material library
	if (_ui->searchEdit)
	{
		connect(_ui->searchEdit, &QLineEdit::textChanged, this, &MaterialPropertiesPanel::onSearchTextChanged);
	}

	// Default checker on all texture buttons
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
		applyButtonEmptyIcon(it.value());

	// Enable drag/drop on texture buttons
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
	{
		if (it.value().button)
		{
			it.value().button->setAcceptDrops(true);
			it.value().button->installEventFilter(this);
		}
	}

	// Connect color picker buttons
	if (_ui->btnAlbedoColor)
		connect(_ui->btnAlbedoColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onAlbedoColorPicked);
	if (_ui->btnEmissiveColor)
		connect(_ui->btnEmissiveColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onEmissiveColorPicked);
	if (_ui->btnSheenColor)
		connect(_ui->btnSheenColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onSheenColorPicked);
	if (_ui->btnDiffTransColor)
		connect(_ui->btnDiffTransColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onDiffuseTransmissionColorPicked);
	if (_ui->btnSpecularColor)
		connect(_ui->btnSpecularColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onSpecularColorPicked);
	if (_ui->btnAttenuationColor)
		connect(_ui->btnAttenuationColor, &QPushButton::clicked, this, &MaterialPropertiesPanel::onAttenuationColorPicked);

	// Initialize material to default and bind
	bindMaterial(new Material());

	// Connect tree widget material selection signals
	connectMaterialLibrarySignals();

	// Load first material by default on panel initialization
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		// NOTE: MaterialLibraryWidget::populateMaterials() already selected the first item
		// during construction. We just need to get it and load it into the panel.
		QTreeWidgetItem* currentItem = libraryWidget->currentItem();
		if (currentItem)
		{
			QString materialKey = currentItem->data(0, Qt::UserRole).toString();
			if (!materialKey.isEmpty())
			{
				// Get the currently selected material from the library
				if (libraryWidget->sharedMaterialMap().contains(materialKey))
				{
					Material defaultMat = libraryWidget->sharedMaterialMap()[materialKey]();
					// Call our handler directly to load textures and update preview
					onMaterialPresetSelected(defaultMat);
				}
			}
		}
	}

	// Connect Clear buttons for each texture slot
	connect(_ui->toolButtonClearAllMaps, &QPushButton::clicked,
		this, &MaterialPropertiesPanel::clearAllTexturesMaps);
	// Connect cache clearing button with confirmation dialog
	connect(_ui->toolButtonClearTextureCache, &QPushButton::clicked, this, [this] {
		clearAllTexturesMaps();
		QMessageBox::information(nullptr, "Texture Cache Cleared",
			"The texture cache has been cleared.");
		});

	// Connect Apply button
	if (_ui->applyButton)
	{
		connect(_ui->applyButton, &QPushButton::clicked, this, [this]() {
			if (!_material) return;

			// If editing a mesh material, emit special signal
			if (!_editingMeshUuid.isNull())
			{
				emit meshMaterialApplied(_editingMeshUuid, *_material);
			}
			else
			{
				// Normal material apply
				emit materialApplied(*_material);
			}
			});
	}

	// Connect New button
	if (_ui->newButton)
	{
		connect(_ui->newButton, &QToolButton::clicked, this, &MaterialPropertiesPanel::onCreateNewMaterial);
	}

	// Connect Save to Library button
	if (_ui->saveButton)
	{
		connect(_ui->saveButton, &QPushButton::clicked, this, &MaterialPropertiesPanel::onSaveToLibrary);
	}

	// Connect Save As button
	if (_ui->saveButtonAs)
	{
		connect(_ui->saveButtonAs, &QToolButton::clicked, this, &MaterialPropertiesPanel::onSaveAsToLibrary);
	}

	// Connect Refresh button
	if (_ui->refreshTreeButton)
	{
		connect(_ui->refreshTreeButton, &QToolButton::clicked, this, &MaterialPropertiesPanel::onRefreshSelectedMaterialFromLibrary);
	}

	// Connect Delete button
	if (_ui->deleteButton)
	{
		connect(_ui->deleteButton, &QPushButton::clicked, this, &MaterialPropertiesPanel::onDeleteMaterial);
	}

	// Connect preview controls to updatePreview
	if (_ui->comboShape)
		connect(_ui->comboShape, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialPropertiesPanel::updatePreview);

	if (_ui->comboEnv)
		connect(_ui->comboEnv, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialPropertiesPanel::updatePreview);

	if (_ui->comboBoxTexMode)
		connect(_ui->comboBoxTexMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialPropertiesPanel::updatePreview);

	// Connect scalar property spinboxes to their change handlers
	if (_ui->metalnessSpin)
		connect(_ui->metalnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onMetallicChanged);
	if (_ui->roughnessSpin)
		connect(_ui->roughnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onRoughnessChanged);
	if (_ui->iorSpin)
		connect(_ui->iorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onIORChanged);
	if (_ui->opacitySpin)
		connect(_ui->opacitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onOpacityChanged);
	if (_ui->emissiveSpin)
		connect(_ui->emissiveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onEmissiveStrengthChanged);
	if (_ui->clearcoatSpin)
		connect(_ui->clearcoatSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onClearcoatChanged);
	if (_ui->clearcoatRoughnessSpin)
		connect(_ui->clearcoatRoughnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onClearcoatRoughnessChanged);
	if (_ui->sheenRoughnessSpin)
		connect(_ui->sheenRoughnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onSheenRoughnessChanged);
	if (_ui->transmissionSpin)
		connect(_ui->transmissionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onTransmissionChanged);
	if (_ui->thicknessSpin)
		connect(_ui->thicknessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onThicknessChanged);
	if (_ui->alphaThresholdSpin)
		connect(_ui->alphaThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onAlphaThresholdChanged);
	if (_ui->occlusionStrengthSpin)
		connect(_ui->occlusionStrengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onOcclusionStrengthChanged);
	if (_ui->doubleSpinBoxAnisotropyStrength)
		connect(_ui->doubleSpinBoxAnisotropyStrength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onAnisotropyStrengthChanged);
	if (_ui->doubleSpinBoxAnisotropyRotation)
		connect(_ui->doubleSpinBoxAnisotropyRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onAnisotropyRotationChanged);
	if (_ui->doubleSpinBoxDiffuseTransmissionFactor)
		connect(_ui->doubleSpinBoxDiffuseTransmissionFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onDiffuseTransmissionFactorChanged);
	if (_ui->doubleSpinBoxSpecularFactor)
		connect(_ui->doubleSpinBoxSpecularFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onSpecularFactorChanged);
	if (_ui->doubleSpinBoxNormalScale)
		connect(_ui->doubleSpinBoxNormalScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onNormalScaleChanged);
	if (_ui->doubleSpinBoxHeightScale)
		connect(_ui->doubleSpinBoxHeightScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onHeightScaleChanged);
	if (_ui->doubleSpinBoxClearcoatNormalScale)
		connect(_ui->doubleSpinBoxClearcoatNormalScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onClearcoatNormalScaleChanged);
	if (_ui->iridescenceFactorSpin)
		connect(_ui->iridescenceFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onIridescenceStrengthChanged);
	if (_ui->iridescenceThicknessMinSpin)
		connect(_ui->iridescenceThicknessMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onIridescenceThicknessChanged);
	if (_ui->iridescenceIorSpin)
		connect(_ui->iridescenceIorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onIridescenceIORChanged);
	if (_ui->iridescenceThicknessMaxSpin)
		connect(_ui->iridescenceThicknessMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MaterialPropertiesPanel::onIridescenceThinFilmThicknessChanged);

	// Connect combo boxes to their change handlers
	if (_ui->shadingCombo)
		connect(_ui->shadingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialPropertiesPanel::onShadingModelChanged);
	if (_ui->blendCombo)
		connect(_ui->blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialPropertiesPanel::onBlendModeChanged);

	// Connect checkboxes to their change handlers
	if (_ui->twoSidedCheck)
		connect(_ui->twoSidedCheck, &QCheckBox::toggled, this, &MaterialPropertiesPanel::onTwoSidedToggled);
	if (_ui->wireframeCheck)
		connect(_ui->wireframeCheck, &QCheckBox::toggled, this, &MaterialPropertiesPanel::onWireframeToggled);
	if (_ui->unlitCheck)
		connect(_ui->unlitCheck, &QCheckBox::toggled, this, &MaterialPropertiesPanel::onUnlitToggled);
}

MaterialPropertiesPanel::~MaterialPropertiesPanel()
{
	delete _ui;
}

void MaterialPropertiesPanel::closeEvent(QCloseEvent* event)
{
	// Check if there are any unsaved materials
	if (!_unsavedMaterialKeys.isEmpty())
	{
		QMessageBox::StandardButton reply =
			QMessageBox::question(this,
				tr("Unsaved Materials"),
				tr("You have %1 unsaved material(s) in the library. "
					"Would you like to save them before closing?").arg(_unsavedMaterialKeys.size()),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
				QMessageBox::Save);

		if (reply == QMessageBox::Cancel)
		{
			event->ignore();
			return;
		}
		else if (reply == QMessageBox::Save)
		{
			// For now, just warn the user to manually save each one
			// In a more sophisticated implementation, we could iterate and save all
			QMessageBox::information(this,
				tr("Manual Save Required"),
				tr("Please save each unsaved material individually by selecting it and clicking 'Save'."));
			event->ignore();
			return;
		}
		// If Discard, proceed with closing (ignore unsaved changes)
	}

	QWidget::closeEvent(event);
}

// ============================================================================
// Public API - Material Binding
// ============================================================================

void MaterialPropertiesPanel::bindMaterial(Material* material)
{
	_material = material;
	if (!_material)
	{
		qWarning() << "bindMaterial: material is null!";
		return;
	}

	// Reflect current textures -> button icons
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
	{
		const QString path = textureMapPath(it.key());
		if (path.isEmpty())
			applyButtonEmptyIcon(it.value());
		else
			applyButtonImageIcon(it.value(), path);
	}

	// Load all scalar property values from material (includes factors, colors, scales, etc.)
	loadScalarValuesFromMaterial();

	// Load texture scale values
	if (_ui->doubleSpinBoxNormalScale)
		_ui->doubleSpinBoxNormalScale->setValue(_material->normalScale());
	if (_ui->doubleSpinBoxHeightScale)
		_ui->doubleSpinBoxHeightScale->setValue(_material->heightScale());
	if (_ui->doubleSpinBoxClearcoatNormalScale)
		_ui->doubleSpinBoxClearcoatNormalScale->setValue(_material->clearcoatNormalScale());

	// IMPORTANT: Load texture image files BEFORE updatePreview()
	// This ensures textures are loaded and available for rendering in the preview
	loadTextureImageFiles();

	// Update preview
	updatePreview();

	// Reset dirty flag - material is now freshly bound
	_texturesDirty = false;

	emit materialChanged(_material);
}

void MaterialPropertiesPanel::initialize(ModelViewer* modelViewer, ViewportWidget* viewportWidget)
{
	_modelViewer = modelViewer;
	_viewportWidget = viewportWidget;

	qDebug() << "MaterialPropertiesPanel::initialize called with modelViewer:" << (modelViewer ? "non-null" : "NULL");

	// Pass ViewportWidget reference to preview widget for environment settings
	if (_preview && _viewportWidget)
	{
		_preview->setViewportWidget(_viewportWidget);
	}

	// Get reference to the MDI-scoped material cache from ModelViewer
	if (_modelViewer)
	{
		_materialCacheRef = _modelViewer->getMaterialCache();
		qDebug() << "MaterialPropertiesPanel::initialize - _materialCacheRef set to:" << (void*)_materialCacheRef;
	}
}

// ============================================================================
// Public API - Property Getters (simplified stubs)
// ============================================================================

QVector3D MaterialPropertiesPanel::getAlbedoColor() const
{
	if (!_material) return QVector3D();
	return _material->albedoColor();
}

float MaterialPropertiesPanel::getMetalness() const
{
	if (!_material) return 0.0f;
	return _material->metalness();
}

float MaterialPropertiesPanel::getRoughness() const
{
	if (!_material) return 0.5f;
	return _material->roughness();
}

float MaterialPropertiesPanel::getIOR() const
{
	if (!_material) return 1.5f;
	return _material->ior();
}

float MaterialPropertiesPanel::getOpacity() const
{
	if (!_material) return 1.0f;
	return _material->opacity();
}

float MaterialPropertiesPanel::getEmissiveStrength() const
{
	if (!_material) return 0.0f;
	return _material->emissiveStrength();
}

QVector3D MaterialPropertiesPanel::getEmissiveColor() const
{
	if (!_material) return QVector3D();
	return _material->emissive();
}

float MaterialPropertiesPanel::getClearcoat() const
{
	if (!_material) return 0.0f;
	return _material->clearcoat();
}

float MaterialPropertiesPanel::getClearcoatRoughness() const
{
	if (!_material) return 0.0f;
	return _material->clearcoatRoughness();
}

QVector3D MaterialPropertiesPanel::getSheenColor() const
{
	if (!_material) return QVector3D();
	return _material->sheenColor();
}

float MaterialPropertiesPanel::getSheenRoughness() const
{
	if (!_material) return 0.0f;
	return _material->sheenRoughness();
}

float MaterialPropertiesPanel::getTransmission() const
{
	if (!_material) return 0.0f;
	return _material->transmission();
}

float MaterialPropertiesPanel::getThickness() const { return _material ? _material->thicknessFactor() : 0.0f; }
float MaterialPropertiesPanel::getNormalScale() const { return _material ? _material->normalScale() : 1.0f; }
float MaterialPropertiesPanel::getHeightScale() const { return _material ? _material->heightScale() : 1.0f; }
float MaterialPropertiesPanel::getClearcoatNormalScale() const { return _material ? _material->clearcoatNormalScale() : 1.0f; }
float MaterialPropertiesPanel::getOcclusionStrength() const { return _material ? _material->occlusionStrength() : 1.0f; }
float MaterialPropertiesPanel::getAnisotropyStrength() const { return _material ? _material->anisotropyStrength() : 0.0f; }
float MaterialPropertiesPanel::getAnisotropyRotation() const { return _material ? _material->anisotropyRotation() : 0.0f; }
float MaterialPropertiesPanel::getDiffuseTransmissionFactor() const { return _material ? _material->diffuseTransmissionFactor() : 0.0f; }
QVector3D MaterialPropertiesPanel::getDiffuseTransmissionColor() const { return _material ? _material->diffuseTransmissionColorFactor() : QVector3D(1.0f, 1.0f, 1.0f); }
float MaterialPropertiesPanel::getSpecularFactor() const { return _material ? _material->specularFactor() : 1.0f; }
QVector3D MaterialPropertiesPanel::getSpecularColor() const { return _material ? _material->specularColor() : QVector3D(1.0f, 1.0f, 1.0f); }
int MaterialPropertiesPanel::getShadingModel() const { return _material ? static_cast<int>(_material->shadingModel()) : 0; }
int MaterialPropertiesPanel::getBlendMode() const { return _material ? static_cast<int>(_material->blendMode()) : 0; }
bool MaterialPropertiesPanel::getTwoSided() const { return _material ? _material->twoSided() : false; }
bool MaterialPropertiesPanel::getWireframe() const { return _material ? _material->wireframe() : false; }
float MaterialPropertiesPanel::getAlphaThreshold() const { return _material ? _material->alphaThreshold() : 0.5f; }
bool MaterialPropertiesPanel::getUnlit() const { return _material && _material->shadingModel() == Material::ShadingModel::Unlit; }
float MaterialPropertiesPanel::getIridescenceStrength() const { return _material ? _material->iridescenceFactor() : 0.0f; }
float MaterialPropertiesPanel::getIridescenceThickness() const { return _material ? _material->iridescenceThicknessMin() : 100.0f; }
float MaterialPropertiesPanel::getIridescenceIOR() const { return _material ? _material->iridescenceIor() : 1.3f; }
float MaterialPropertiesPanel::getIridescenceThinFilmThickness() const { return _material ? _material->iridescenceThicknessMax() : 400.0f; }

QString MaterialPropertiesPanel::getTexturePath(Material::TextureType type) const
{
	return textureMapPath(type);
}

// ============================================================================
// Scalar Property Slot Handlers (simplified stubs)
// ============================================================================

// Helper function to set button background color with contrasting text color
static void setButtonColorWithContrast(QPushButton* btn, const QColor& color)
{
	if (!btn) return;

	// Calculate luminance for text color contrast
	float r = color.redF();
	float g = color.greenF();
	float b = color.blueF();
	float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
	QString textColor = (luminance > 0.5f) ? "black" : "white";

	btn->setStyleSheet(QString("background-color: %1; color: %2;").arg(color.name(), textColor));
}

void MaterialPropertiesPanel::onAlbedoColorPicked()
{
	if (_updateInProgress || !_material) return;
	QColor existingColor = _ui->btnAlbedoColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Albedo Color"));
	if (color.isValid())
	{
		_material->setAlbedoColor(QVector3D(color.redF(), color.greenF(), color.blueF()));
		qDebug() << "onAlbedoColorPicked: Set albedo to" << color.name() << "for material key:" << _currentMaterialKey;
		qDebug() << "  Material albedo color in _material:" << _material->albedoColor();
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		qDebug() << "  After updateUnsavedMaterialInMap, cache entry albedo:"
			<< ((_materialCacheRef && _materialCacheRef->contains(_currentMaterialKey))
				? _materialCacheRef->find(_currentMaterialKey).value().material.albedoColor()
				: QVector3D(0, 0, 0));
		updateScalarUI();
		updatePreview();
		emit materialChanged(_material);
	}
}

void MaterialPropertiesPanel::onMetallicChanged(double value)
{
	if (_material && !_updateInProgress)
	{
		_material->setMetalness(static_cast<float>(value));
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updatePreview();
		emit materialChanged(_material);
	}
}
void MaterialPropertiesPanel::onRoughnessChanged(double value) { if (_material && !_updateInProgress) { _material->setRoughness(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onIORChanged(double value) { if (_material && !_updateInProgress) { _material->setIOR(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onOpacityChanged(double value) { if (_material && !_updateInProgress) { _material->setOpacity(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onEmissiveStrengthChanged(double value) { if (_material && !_updateInProgress) { _material->setEmissiveStrength(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }

void MaterialPropertiesPanel::onEmissiveColorPicked()
{
	if (_updateInProgress || !_material) return;
	QColor existingColor = _ui->btnEmissiveColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Emissive Color"));
	if (color.isValid())
	{
		_material->setEmissive(QVector3D(color.redF(), color.greenF(), color.blueF()));
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updateScalarUI();
		updatePreview();
		emit materialChanged(_material);
	}
}

void MaterialPropertiesPanel::onClearcoatChanged(double value) { if (_material && !_updateInProgress) { _material->setClearcoat(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onClearcoatRoughnessChanged(double value) { if (_material && !_updateInProgress) { _material->setClearcoatRoughness(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onSheenColorPicked()
{
	if (!_material || !_ui) return;
	QColor existingColor = _ui->btnSheenColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Sheen Color"));
	if (color.isValid())
	{
		_material->setSheenColor(QVector3D(color.redF(), color.greenF(), color.blueF()));
		setButtonColorWithContrast(_ui->btnSheenColor, color);
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updatePreview();
		emit materialChanged(_material);
	}
}
void MaterialPropertiesPanel::onSheenRoughnessChanged(double value) { if (_material && !_updateInProgress) { _material->setSheenRoughness(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onTransmissionChanged(double value) { if (_material && !_updateInProgress) { _material->setTransmission(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onThicknessChanged(double value) { if (_material && !_updateInProgress) { _material->setThicknessFactor(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onNormalScaleChanged(double value) { if (_material && !_updateInProgress) { _material->setNormalScale(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onHeightScaleChanged(double value) { if (_material && !_updateInProgress) { _material->setHeightScale(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onClearcoatNormalScaleChanged(double value) { if (_material && !_updateInProgress) { _material->setClearcoatNormalScale(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onOcclusionStrengthChanged(double value) { if (_material && !_updateInProgress) { _material->setOcclusionStrength(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onAnisotropyStrengthChanged(double value) { if (_material && !_updateInProgress) { _material->setAnisotropyStrength(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onAnisotropyRotationChanged(double value) { if (_material && !_updateInProgress) { _material->setAnisotropyRotation(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onDiffuseTransmissionFactorChanged(double value) { if (_material && !_updateInProgress) { _material->setDiffuseTransmissionFactor(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onDiffuseTransmissionColorPicked()
{
	if (!_material || !_ui) return;
	QColor existingColor = _ui->btnDiffTransColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Diffuse Transmission Color"));
	if (color.isValid())
	{
		_material->setDiffuseTransmissionColorFactor(QVector3D(color.redF(), color.greenF(), color.blueF()));
		setButtonColorWithContrast(_ui->btnDiffTransColor, color);
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updatePreview();
		emit materialChanged(_material);
	}
}
void MaterialPropertiesPanel::onSpecularFactorChanged(double value) { if (_material && !_updateInProgress) { _material->setSpecularFactor(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onSpecularColorPicked()
{
	if (!_material || !_ui) return;
	QColor existingColor = _ui->btnSpecularColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Specular Color"));
	if (color.isValid())
	{
		_material->setSpecularColor(QVector3D(color.redF(), color.greenF(), color.blueF()));
		setButtonColorWithContrast(_ui->btnSpecularColor, color);
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updatePreview();
		emit materialChanged(_material);
	}
}

void MaterialPropertiesPanel::onAttenuationColorPicked()
{
	if (!_material || !_ui) return;
	QColor existingColor = _ui->btnAttenuationColor->palette().button().color();
	QColor color = QColorDialog::getColor(existingColor, this, tr("Select Attenuation Color"));
	if (color.isValid())
	{
		_material->setAttenuationColor(QVector3D(color.redF(), color.greenF(), color.blueF()));
		setButtonColorWithContrast(_ui->btnAttenuationColor, color);
		updateUnsavedMaterialInMap();
		markMaterialAsModified();
		updatePreview();
		emit materialChanged(_material);
	}
}

void MaterialPropertiesPanel::onShadingModelChanged(int index) { if (_material && !_updateInProgress) { _material->setShadingModel(static_cast<Material::ShadingModel>(index)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onBlendModeChanged(int index) { if (_material && !_updateInProgress) { _material->setBlendMode(static_cast<Material::BlendMode>(index)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onTwoSidedToggled(bool checked) { if (_material && !_updateInProgress) { _material->setTwoSided(checked); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onWireframeToggled(bool checked) { if (_material && !_updateInProgress) { _material->setWireframe(checked); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onAlphaThresholdChanged(double value) { if (_material && !_updateInProgress) { _material->setAlphaThreshold(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onUnlitToggled(bool checked) { if (_material && !_updateInProgress) { _material->setUnlit(checked); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onIridescenceStrengthChanged(double value) { if (_material && !_updateInProgress) { _material->setIridescenceFactor(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onIridescenceThicknessChanged(double value) { if (_material && !_updateInProgress) { _material->setIridescenceThicknessMin(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onIridescenceIORChanged(double value) { if (_material && !_updateInProgress) { _material->setIridescenceIor(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }
void MaterialPropertiesPanel::onIridescenceThinFilmThicknessChanged(double value) { if (_material && !_updateInProgress) { _material->setIridescenceThicknessMax(static_cast<float>(value)); updateUnsavedMaterialInMap(); markMaterialAsModified(); updatePreview(); emit materialChanged(_material); } }

// ============================================================================
// Texture Management
// ============================================================================

void MaterialPropertiesPanel::registerTextureMaps()
{
	if (!_ui) return;

	// Helper lambda to safely insert texture slots
	auto insertSlot = [this](Material::TextureType type, QPushButton* btn, QLabel* lbl,
		QToolButton* gear, QToolButton* transform, const QString& key) {
			if (!btn) return;  // Button is required

			MapSlot slot;
			slot.button = btn;
			slot.label = lbl;
			slot.gear = gear;
			slot.transformButton = transform;
			slot.key = key;
			slot.type = type;

			_textureSlots.insert(type, slot);
		};

	// Register texture types that have UI buttons defined in the UI file
	// Only include textures where the button exists
	if (_ui->btnAlbedoTex) insertSlot(Material::TextureType::Albedo, _ui->btnAlbedoTex, _ui->lblAlbedo, nullptr, _ui->toolButtonAlbedoTexTrsf, "albedo");
	if (_ui->btnNormalTex) insertSlot(Material::TextureType::Normal, _ui->btnNormalTex, _ui->lblNormal, nullptr, _ui->toolButtonNormalTexTrsf, "normal");
	if (_ui->btnMetallicTex) insertSlot(Material::TextureType::Metallic, _ui->btnMetallicTex, _ui->lblMetallic, _ui->gearMetallic, _ui->toolButtonMetallicTexTrsf, "metallic");
	if (_ui->btnRoughnessTex) insertSlot(Material::TextureType::Roughness, _ui->btnRoughnessTex, _ui->lblRoughnessTex, _ui->gearRoughness, _ui->toolButtonRoughTexTrsf, "roughness");
	if (_ui->btnAOTex) insertSlot(Material::TextureType::AmbientOcclusion, _ui->btnAOTex, _ui->lblAmbientOcclusion, _ui->gearAO, _ui->toolButtonAOTexTrsf, "ao");
	if (_ui->btnOpacityTex) insertSlot(Material::TextureType::Opacity, _ui->btnOpacityTex, _ui->lblOpacityTex, _ui->gearOpacity, _ui->toolButtonOpacTexTrsf, "opacity");
	if (_ui->btnEmissiveTex) insertSlot(Material::TextureType::Emissive, _ui->btnEmissiveTex, _ui->lblEmissiveTex, nullptr, _ui->toolButtonEmissiveTexTrsf, "emissive");
	if (_ui->btnHeightTex) insertSlot(Material::TextureType::Height, _ui->btnHeightTex, _ui->lblHeightScaleLabel, nullptr, _ui->toolButtonHeightTexTrsf, "height");
	if (_ui->btnTransmissionTex) insertSlot(Material::TextureType::Transmission, _ui->btnTransmissionTex, _ui->lblTransmissionTex, nullptr, _ui->toolButtonTransTexTrsf, "transmission");
	if (_ui->btnIORTex) insertSlot(Material::TextureType::IOR, _ui->btnIORTex, _ui->lblIOR, nullptr, _ui->toolButtonIORTexTrsf, "ior");
	if (_ui->btnSheenColorTex) insertSlot(Material::TextureType::SheenColor, _ui->btnSheenColorTex, _ui->lblSheenColor, nullptr, _ui->toolButtonSheenColTexTrsf, "sheen_color");
	if (_ui->btnSheenRoughTex) insertSlot(Material::TextureType::SheenRoughness, _ui->btnSheenRoughTex, nullptr, nullptr, _ui->toolButtonSheenRghTexTrsf, "sheen_rough");
	if (_ui->btnCCColorTex) insertSlot(Material::TextureType::ClearcoatColor, _ui->btnCCColorTex, nullptr, nullptr, _ui->toolButtonClearCColTexTrsf, "clearcoat_color");
	if (_ui->btnCCRoughTex) insertSlot(Material::TextureType::ClearcoatRoughness, _ui->btnCCRoughTex, nullptr, nullptr, _ui->toolButtonClearCRghTexTrsf, "clearcoat_rough");
	if (_ui->btnCCNormalTex) insertSlot(Material::TextureType::ClearcoatNormal, _ui->btnCCNormalTex, nullptr, nullptr, _ui->toolButtonClearCNorTexTrsf, "clearcoat_normal");
	if (_ui->btnIridFactorTex) insertSlot(Material::TextureType::Iridescence, _ui->btnIridFactorTex, nullptr, nullptr, _ui->toolButtonIridColTexTrsf, "iridescence");
	if (_ui->btnIridescenceThicknessTex) insertSlot(Material::TextureType::IridescenceThickness, _ui->btnIridescenceThicknessTex, nullptr, nullptr, _ui->toolButtonIridRghTexTrsf, "iridescence_thickness");
	if (_ui->btnSpecFactorColorTex) insertSlot(Material::TextureType::SpecularFactor, _ui->btnSpecFactorColorTex, nullptr, nullptr, _ui->toolButtonSpecFactorTexTrsf, "specular_factor");
	if (_ui->btnSpecColorColorTex) insertSlot(Material::TextureType::SpecularColor, _ui->btnSpecColorColorTex, nullptr, nullptr, _ui->toolButtonSpecColorTexTrsf, "specular_color");
	if (_ui->btnAnisotropyColorTex) insertSlot(Material::TextureType::Anisotropy, _ui->btnAnisotropyColorTex, nullptr, nullptr, _ui->toolButtonAnisotropyTexTrsf, "anisotropy");
	if (_ui->btnDiffuseTransTex) insertSlot(Material::TextureType::DiffuseTransmission, _ui->btnDiffuseTransTex, nullptr, nullptr, _ui->toolButtonDiffuseTransTexTrsf, "diffuse_transmission");
	if (_ui->btnDiffuseTransColorTex) insertSlot(Material::TextureType::DiffuseTransmissionColor, _ui->btnDiffuseTransColorTex, nullptr, nullptr, _ui->toolButtonDiffuseTransColorTexTrsf, "diffuse_transmission_color");
	if (_ui->btnThicknessTex) insertSlot(Material::TextureType::Thickness, _ui->btnThicknessTex, _ui->lblThicknessTex, nullptr, _ui->toolButtonThicknessTexTrsf, "thickness");
}

void MaterialPropertiesPanel::setupTextureMetadataUI()
{
	if (!_ui) return;

	auto assignExistingMeta = [this](Material::TextureType type,
		const char* uvName, const char* colorName, const char* packingName)
	{
		auto it = _textureSlots.find(type);
		if (it == _textureSlots.end())
			return;

		it->metaUv = findChild<QLabel*>(QString::fromLatin1(uvName));
		it->metaColorSpace = findChild<QLabel*>(QString::fromLatin1(colorName));
		it->metaPacking = findChild<QLabel*>(QString::fromLatin1(packingName));
	};

	assignExistingMeta(Material::TextureType::Albedo, "lblAlbedoMetaUV", "lblAlbedoMetaColorSpace", "lblAlbedoMetaPacking");
	assignExistingMeta(Material::TextureType::Opacity, "lblOpacityMetaUV", "lblOpacityMetaColorSpace", "lblOpacityMetaPacking");
	assignExistingMeta(Material::TextureType::Emissive, "lblEmissiveMetaUV", "lblEmissiveMetaColorSpace", "lblEmissiveMetaPacking");
	assignExistingMeta(Material::TextureType::Metallic, "lblMetallicMetaUV", "lblMetallicMetaColorSpace", "lblMetallicMetaPacking");
	assignExistingMeta(Material::TextureType::AmbientOcclusion, "lblAOMetaUV", "lblAOMetaColorSpace", "lblAOMetaPacking");
	assignExistingMeta(Material::TextureType::Height, "lblHeightMetaUV", "lblHeightMetaColorSpace", "lblHeightMetaPacking");
	assignExistingMeta(Material::TextureType::Roughness, "lblRoughnessMetaUV", "lblRoughnessMetaColorSpace", "lblRoughnessMetaPacking");
	assignExistingMeta(Material::TextureType::Normal, "lblNormalMetaUV", "lblNormalMetaColorSpace", "lblNormalMetaPacking");
	assignExistingMeta(Material::TextureType::ClearcoatColor, "lblCCColorMetaUV", "lblCCColorMetaColorSpace", "lblCCColorMetaPacking");
	assignExistingMeta(Material::TextureType::ClearcoatRoughness, "lblCCRoughMetaUV", "lblCCRoughMetaColorSpace", "lblCCRoughMetaPacking");
	assignExistingMeta(Material::TextureType::ClearcoatNormal, "lblCCNormalMetaUV", "lblCCNormalMetaColorSpace", "lblCCNormalMetaPacking");
	assignExistingMeta(Material::TextureType::SheenColor, "lblSheenColorMetaUV", "lblSheenColorMetaColorSpace", "lblSheenColorMetaPacking");
	assignExistingMeta(Material::TextureType::SheenRoughness, "lblSheenRoughMetaUV", "lblSheenRoughMetaColorSpace", "lblSheenRoughMetaPacking");
	assignExistingMeta(Material::TextureType::Iridescence, "lblIridFactorMetaUV", "lblIridFactorMetaColorSpace", "lblIridFactorMetaPacking");
	assignExistingMeta(Material::TextureType::IridescenceThickness, "lblIridThicknessMetaUV", "lblIridThicknessMetaColorSpace", "lblIridThicknessMetaPacking");
	assignExistingMeta(Material::TextureType::Transmission, "lblTransmissionMetaUV", "lblTransmissionMetaColorSpace", "lblTransmissionMetaPacking");
	assignExistingMeta(Material::TextureType::IOR, "lblIORMetaUV", "lblIORMetaColorSpace", "lblIORMetaPacking");
	assignExistingMeta(Material::TextureType::Thickness, "lblThicknessMetaUV", "lblThicknessMetaColorSpace", "lblThicknessMetaPacking");
	assignExistingMeta(Material::TextureType::SpecularFactor, "lblSpecFactorMetaUV", "lblSpecFactorMetaColorSpace", "lblSpecFactorMetaPacking");
	assignExistingMeta(Material::TextureType::SpecularColor, "lblSpecColorMetaUV", "lblSpecColorMetaColorSpace", "lblSpecColorMetaPacking");
	assignExistingMeta(Material::TextureType::Anisotropy, "lblAnisotropyMetaUV", "lblAnisotropyMetaColorSpace", "lblAnisotropyMetaPacking");
	assignExistingMeta(Material::TextureType::DiffuseTransmission, "lblDiffuseTransMetaUV", "lblDiffuseTransMetaColorSpace", "lblDiffuseTransMetaPacking");
	assignExistingMeta(Material::TextureType::DiffuseTransmissionColor, "lblDiffuseTransColorMetaUV", "lblDiffuseTransColorMetaColorSpace", "lblDiffuseTransColorMetaPacking");

	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
		updateTextureMetadata(it.key());
}

void MaterialPropertiesPanel::connectTextureSignals()
{
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
	{
		auto& slot = it.value();
		if (!slot.button) continue;

		connect(slot.button, &QPushButton::clicked, this, [this, type = it.key()]() {
			onTextureButtonClicked(type);
			});

		// Set up context menu for texture button (right-click)
		slot.button->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(slot.button, &QWidget::customContextMenuRequested, this, [this, btn = slot.button, type = it.key()](const QPoint& pos) {
			QMenu menu(btn);

			// Channel Packing option (if gear button exists)
			if (_textureSlots[type].gear)
			{
				menu.addAction(tr("Channel Packing..."), this, [this, type]() { openPackingDialogFor(type); });
				menu.addSeparator();
			}

			// Replace option
			menu.addAction(tr("Replace..."), this, [this, btn]() { btn->click(); });

			// Clear option
			menu.addAction(tr("Clear"), this, [this, type]() {
				clearTextureMap(type);
				applyButtonEmptyIcon(_textureSlots[type]);
				updatePreview();
				emit materialChanged(_material);
				});

			menu.exec(btn->mapToGlobal(pos));
			});

		if (slot.transformButton)
		{
			connect(slot.transformButton, &QToolButton::clicked, this, [this, type = it.key()]() {
				onTransformButtonClicked(type);
				});
		}
	}

	// Connect gear buttons for channel packing
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
	{
		if (it.value().gear)
		{
			connect(it.value().gear, &QToolButton::clicked, this, [this, type = it.key()]() {
				openPackingDialogFor(type);
				});
		}
	}

	// Connect scale spinboxes for texture coordinate scaling
	if (_ui->doubleSpinBoxNormalScale)
	{
		connect(_ui->doubleSpinBoxNormalScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double value) {
				if (!_material) return;
				_material->setNormalScale(static_cast<float>(value));
				updatePreview();
				emit materialChanged(_material);
			});
	}

	if (_ui->doubleSpinBoxHeightScale)
	{
		connect(_ui->doubleSpinBoxHeightScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double value) {
				if (!_material) return;
				_material->setHeightScale(static_cast<float>(value));
				updatePreview();
				emit materialChanged(_material);
			});
	}

	if (_ui->doubleSpinBoxClearcoatNormalScale)
	{
		connect(_ui->doubleSpinBoxClearcoatNormalScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double value) {
				if (!_material) return;
				_material->setClearcoatNormalScale(static_cast<float>(value));
				updatePreview();
				emit materialChanged(_material);
			});
	}
}

void MaterialPropertiesPanel::applyButtonEmptyIcon(MapSlot& m)
{
	if (!m.button) return;
	m.button->setIcon(_checkerIcon);
	m.button->setIconSize(QSize(90, 90));
	m.button->setText(QString());
	m.button->setToolTip(QString());
	updateTextureMetadata(m.type);
}

void MaterialPropertiesPanel::applyButtonImageIcon(MapSlot& m, const QString& file)
{
	if (!m.button) return;
	m.button->setIcon(makeIconFromFile(file));
	m.button->setIconSize(QSize(90, 90));
	m.button->setToolTip(file);
	updateTextureMetadata(m.type);
}

QIcon MaterialPropertiesPanel::makeIconFromFile(const QString& file, int edge) const
{
	QImage img(file);
	if (img.isNull())
		return _checkerIcon;
	img = img.scaledToWidth(edge, Qt::SmoothTransformation);
	return QIcon(QPixmap::fromImage(img));
}

QIcon MaterialPropertiesPanel::makeCheckerIcon(int w, int h, int cell)
{
	QImage checkerImg(w, h, QImage::Format_RGB32);
	QPainter painter(&checkerImg);

	for (int y = 0; y < h; y += cell)
	{
		for (int x = 0; x < w; x += cell)
		{
			bool white = ((x / cell + y / cell) % 2) == 0;
			painter.fillRect(x, y, cell, cell, white ? Qt::white : Qt::lightGray);
		}
	}
	return QIcon(QPixmap::fromImage(checkerImg));
}

void MaterialPropertiesPanel::setTextureMapPath(Material::TextureType type, const QString& file)
{
	if (!_material) return;
	_lastUsedTextureFolder = QFileInfo(file).dir().absolutePath();

	Material::Texture tex = _material->texture(type);
	tex.path = file.toStdString();
	_material->setTexture(type, tex);

	auto& slot = _textureSlots[type];
	applyButtonImageIcon(slot, file);
	updateUnsavedMaterialInMap();
	markMaterialAsModified();
	updatePreview();
	emit textureSamplerChanged(_material, type);
}

void MaterialPropertiesPanel::clearTextureMap(Material::TextureType type)
{
	if (!_material) return;

	// Clear using NEW API (for preview and new code paths)
	Material::Texture resetTex;
	resetTex.type = Material::textureTypeToString(type).toStdString();
	_material->setTexture(type, resetTex);

	// ALSO clear using OLD API (for compatibility with onCustomMaterialApplied)
	// The old API stores texture paths in separate fields that are used when applying materials
	switch (type)
	{
	case Material::TextureType::Albedo:
		_material->clearAlbedoMap();
		break;
	case Material::TextureType::Normal:
		_material->clearNormalMap();
		break;
	case Material::TextureType::Metallic:
		_material->clearMetallicMap();
		break;
	case Material::TextureType::Roughness:
		_material->clearRoughnessMap();
		break;
	case Material::TextureType::AmbientOcclusion:
		_material->clearAOMap();
		break;
	case Material::TextureType::Opacity:
		_material->clearOpacityMap();
		break;
	case Material::TextureType::Emissive:
		_material->clearEmissiveMap();
		break;
	case Material::TextureType::Height:
		_material->clearHeightMap();
		break;
	case Material::TextureType::Transmission:
		_material->clearTransmissionMap();
		break;
	case Material::TextureType::IOR:
		_material->clearIORMap();
		break;
	case Material::TextureType::SheenColor:
		_material->clearSheenColorMap();
		break;
	case Material::TextureType::SheenRoughness:
		_material->clearSheenRoughnessMap();
		break;
	case Material::TextureType::ClearcoatColor:
		_material->clearClearcoatColorMap();
		break;
	case Material::TextureType::ClearcoatRoughness:
		_material->clearClearcoatRoughnessMap();
		break;
	case Material::TextureType::ClearcoatNormal:
		_material->clearClearcoatNormalMap();
		break;
	case Material::TextureType::Iridescence:
		_material->clearIridescenceMap();
		break;
	case Material::TextureType::IridescenceThickness:
		_material->clearIridescenceThicknessMap();
		break;
	case Material::TextureType::SpecularFactor:
		_material->clearSpecularFactorMap();
		break;
	case Material::TextureType::SpecularColor:
		_material->clearSpecularColorMap();
		break;
	case Material::TextureType::Anisotropy:
		_material->clearAnisotropyMap();
		break;
	case Material::TextureType::DiffuseTransmission:
		_material->clearDiffuseTransmissionMap();
		break;
	case Material::TextureType::DiffuseTransmissionColor:
		_material->clearDiffuseTransmissionColorMap();
		break;
	case Material::TextureType::Thickness:
		_material->clearThicknessMap();
		break;
	default:
		break;
	}

	auto& slot = _textureSlots[type];
	applyButtonEmptyIcon(slot);
	updateUnsavedMaterialInMap();
	markMaterialAsModified();
	// Note: updatePreview() is NOT called here. It's called once at the end of clearAllTexturesMaps()
	// to avoid multiple unnecessary preview updates
	emit textureSamplerChanged(_material, type);
}

void MaterialPropertiesPanel::clearAllTexturesMaps()
{
	if (!_material) return;

	// Clear all texture maps and update UI
	for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
	{
		clearTextureMap(it.key());
	}

	// Update preview to show cleared state
	updatePreview();

	// Reapply the cleared material to selected meshes in the viewer
	emit materialApplied(*_material);

	// Clear GPU texture cache to free memory
	emit textureCacheClearRequested();
}

QString MaterialPropertiesPanel::textureMapPath(Material::TextureType type) const
{
	if (!_material) return QString();
	return QString::fromStdString(_material->texture(type).path);
}


void MaterialPropertiesPanel::loadScalarValuesFromMaterial()
{
	if (!_material || !_ui) return;

	_updateInProgress = true;

	// Scalar numeric properties
	if (_ui->metalnessSpin) _ui->metalnessSpin->setValue(_material->metalness());
	if (_ui->roughnessSpin) _ui->roughnessSpin->setValue(_material->roughness());
	if (_ui->iorSpin) _ui->iorSpin->setValue(_material->ior());
	if (_ui->opacitySpin) _ui->opacitySpin->setValue(_material->opacity());
	if (_ui->emissiveSpin) _ui->emissiveSpin->setValue(_material->emissiveStrength());
	if (_ui->clearcoatSpin) _ui->clearcoatSpin->setValue(_material->clearcoat());
	if (_ui->clearcoatRoughnessSpin) _ui->clearcoatRoughnessSpin->setValue(_material->clearcoatRoughness());
	if (_ui->sheenRoughnessSpin) _ui->sheenRoughnessSpin->setValue(_material->sheenRoughness());
	if (_ui->transmissionSpin) _ui->transmissionSpin->setValue(_material->transmission());
	if (_ui->thicknessSpin) _ui->thicknessSpin->setValue(_material->thicknessFactor());
	if (_ui->alphaThresholdSpin) _ui->alphaThresholdSpin->setValue(_material->alphaThreshold());
	if (_ui->occlusionStrengthSpin) _ui->occlusionStrengthSpin->setValue(_material->occlusionStrength());
	if (_ui->aoSpin) _ui->aoSpin->setValue(_material->occlusionStrength());
	if (_ui->dispersionSpin) _ui->dispersionSpin->setValue(_material->dispersion());
	if (_ui->attenuationDistanceSpin) _ui->attenuationDistanceSpin->setValue(_material->attenuationDistance());
	if (_ui->iridescenceFactorSpin) _ui->iridescenceFactorSpin->setValue(_material->iridescenceFactor());
	if (_ui->iridescenceIorSpin) _ui->iridescenceIorSpin->setValue(_material->iridescenceIor());
	if (_ui->iridescenceThicknessMinSpin) _ui->iridescenceThicknessMinSpin->setValue(_material->iridescenceThicknessMin());
	if (_ui->iridescenceThicknessMaxSpin) _ui->iridescenceThicknessMaxSpin->setValue(_material->iridescenceThicknessMax());
	if (_ui->doubleSpinBoxAnisotropyStrength) _ui->doubleSpinBoxAnisotropyStrength->setValue(_material->anisotropyStrength());
	if (_ui->doubleSpinBoxAnisotropyRotation) _ui->doubleSpinBoxAnisotropyRotation->setValue(_material->anisotropyRotation());
	if (_ui->doubleSpinBoxDiffuseTransmissionFactor) _ui->doubleSpinBoxDiffuseTransmissionFactor->setValue(_material->diffuseTransmissionFactor());
	if (_ui->doubleSpinBoxSpecularFactor) _ui->doubleSpinBoxSpecularFactor->setValue(_material->specularFactor());
	if (_ui->doubleSpinBoxNormalScale) _ui->doubleSpinBoxNormalScale->setValue(_material->normalScale());
	if (_ui->doubleSpinBoxHeightScale) _ui->doubleSpinBoxHeightScale->setValue(_material->heightScale());
	if (_ui->doubleSpinBoxClearcoatNormalScale) _ui->doubleSpinBoxClearcoatNormalScale->setValue(_material->clearcoatNormalScale());

	// Color properties
	auto setColorButton = [this](QPushButton* btn, const QVector3D& color) {
		if (!btn) return;
		QColor qcolor(
			int(std::clamp(color.x(), 0.0f, 1.0f) * 255),
			int(std::clamp(color.y(), 0.0f, 1.0f) * 255),
			int(std::clamp(color.z(), 0.0f, 1.0f) * 255)
		);
		// Calculate luminance to determine text color contrast
		// Using standard relative luminance formula for sRGB
		float luminance = 0.299f * color.x() + 0.587f * color.y() + 0.114f * color.z();
		QString textColor = (luminance > 0.5f) ? "black" : "white";
		btn->setStyleSheet(QString("background-color: %1; color: %2;").arg(qcolor.name(), textColor));
		};

	setColorButton(_ui->btnAlbedoColor, _material->albedoColor());
	setColorButton(_ui->btnEmissiveColor, _material->emissive());
	setColorButton(_ui->btnSheenColor, _material->sheenColor());
	setColorButton(_ui->btnDiffTransColor, _material->diffuseTransmissionColorFactor());
	setColorButton(_ui->btnSpecularColor, _material->specularColor());
	setColorButton(_ui->btnAttenuationColor, _material->attenuationColor());

	// Combo boxes
	if (_ui->shadingCombo) _ui->shadingCombo->setCurrentIndex(static_cast<int>(_material->shadingModel()));
	if (_ui->blendCombo) _ui->blendCombo->setCurrentIndex(static_cast<int>(_material->blendMode()));

	// Checkboxes
	if (_ui->twoSidedCheck) _ui->twoSidedCheck->setChecked(_material->twoSided());
	if (_ui->wireframeCheck) _ui->wireframeCheck->setChecked(_material->wireframe());
	if (_ui->unlitCheck) _ui->unlitCheck->setChecked(_material->isUnlit());

	_updateInProgress = false;
}
void MaterialPropertiesPanel::updateScalarUI()
{
	// This function is called after color changes to update the color button UI
	// Simply call loadScalarValuesFromMaterial to refresh all scalar controls
	loadScalarValuesFromMaterial();
}
void MaterialPropertiesPanel::updateTexturePreview(Material::TextureType type)
{
	if (!_material || !_textureSlots.contains(type))
		return;

	auto& slot = _textureSlots[type];
	if (!slot.button)
		return;

	const QString path = textureMapPath(type);
	if (path.isEmpty())
		applyButtonEmptyIcon(slot);
	else
		applyButtonImageIcon(slot, path);
}

void MaterialPropertiesPanel::updateTextureMetadata(Material::TextureType type)
{
	if (!_ui || !_textureSlots.contains(type))
		return;

	auto& slot = _textureSlots[type];
	if (!slot.metaUv || !slot.metaColorSpace || !slot.metaPacking)
		return;

	auto setStaticMeta = [&slot](const QString& uv, const QString& colorSpace, const QString& packing) {
		slot.metaUv->setText(uv);
		slot.metaColorSpace->setText(colorSpace);
		slot.metaPacking->setText(packing);
	};

	auto setMetaEnabled = [&slot](bool enabled) {
		slot.metaUv->setEnabled(enabled);
		slot.metaColorSpace->setEnabled(enabled);
		slot.metaPacking->setEnabled(enabled);
	};

	auto defaultColorSpace = [](Material::TextureType textureType) -> QString {
		switch (textureType)
		{
		case Material::TextureType::Albedo:
		case Material::TextureType::Emissive:
		case Material::TextureType::SheenColor:
		case Material::TextureType::ClearcoatColor:
		case Material::TextureType::SpecularColor:
		case Material::TextureType::DiffuseTransmissionColor:
			return QStringLiteral("sRGB");
		default:
			return QStringLiteral("Linear");
		}
	};

	auto defaultPacking = [](Material::TextureType textureType) -> QString {
		switch (textureType)
		{
		case Material::TextureType::Albedo:
		case Material::TextureType::Normal:
		case Material::TextureType::Emissive:
		case Material::TextureType::SheenColor:
		case Material::TextureType::ClearcoatNormal:
		case Material::TextureType::SpecularColor:
		case Material::TextureType::Anisotropy:
		case Material::TextureType::DiffuseTransmissionColor:
			return QStringLiteral("RGB");
		case Material::TextureType::Metallic:
		case Material::TextureType::Roughness:
		case Material::TextureType::AmbientOcclusion:
		case Material::TextureType::Opacity:
			return QStringLiteral("-");
		case Material::TextureType::Height:
		case Material::TextureType::Transmission:
		case Material::TextureType::IOR:
		case Material::TextureType::ClearcoatColor:
		case Material::TextureType::Iridescence:
			return QStringLiteral("R");
		case Material::TextureType::ClearcoatRoughness:
		case Material::TextureType::IridescenceThickness:
		case Material::TextureType::Thickness:
			return QStringLiteral("G");
		case Material::TextureType::SheenRoughness:
		case Material::TextureType::SpecularFactor:
		case Material::TextureType::DiffuseTransmission:
			return QStringLiteral("A");
		default:
			return QStringLiteral("-");
		}
	};

	if (!_material)
	{
		setStaticMeta(tr("UV-"), defaultColorSpace(type), defaultPacking(type));
		setMetaEnabled(false);
		return;
	}

	const Material::Texture tex = _material->texture(type);
	slot.metaUv->setText(tr("UV%1").arg(tex.texCoordIndex));
	slot.metaColorSpace->setText(defaultColorSpace(type));

	switch (type)
	{
	case Material::TextureType::Metallic:
		slot.metaPacking->setText(channelPackingSummary(_material->packingFor(QStringLiteral("metallic"))));
		break;
	case Material::TextureType::Roughness:
		slot.metaPacking->setText(channelPackingSummary(_material->packingFor(QStringLiteral("roughness"))));
		break;
	case Material::TextureType::AmbientOcclusion:
		slot.metaPacking->setText(channelPackingSummary(_material->packingFor(QStringLiteral("ao"))));
		break;
	case Material::TextureType::Opacity:
		slot.metaPacking->setText(channelPackingSummary(_material->packingFor(QStringLiteral("opacity"))));
		break;
	default:
		slot.metaPacking->setText(defaultPacking(type));
		break;
	}

	setMetaEnabled(!textureMapPath(type).isEmpty());
}

void MaterialPropertiesPanel::openPackingDialogFor(Material::TextureType type)
{
	if (!_material) return;

	// Convert TextureType to string key used by material packing
	auto keyFromType = [](Material::TextureType t)->QString {
		if (t == Material::TextureType::Metallic) return "metallic";
		if (t == Material::TextureType::Roughness) return "roughness";
		if (t == Material::TextureType::AmbientOcclusion) return "ao";
		if (t == Material::TextureType::Opacity) return "opacity";
		return QString();
		};

	// Pretty name for the window title
	auto pretty = [](Material::TextureType t)->QString {
		if (t == Material::TextureType::Metallic) return tr("Metallic");
		if (t == Material::TextureType::Roughness) return tr("Roughness");
		if (t == Material::TextureType::AmbientOcclusion) return tr("Ambient Occlusion");
		if (t == Material::TextureType::Opacity) return tr("Opacity");
		return tr("Texture");
		};

	QString key = keyFromType(type);
	if (key.isEmpty())
		return;  // Unsupported texture type for packing

	ChannelPackingEditorDialog dlg(this);

	// Show dialog with current (or default) packing
	Material::ChannelPacking cur{};
	if (_material) cur = _material->packingFor(key);
	dlg.setCurrentPacking(cur, pretty(type));

	if (dlg.exec() == QDialog::Accepted)
	{
		if (_material)
		{
			_material->setPackingFor(key, dlg.packing());

			// CRITICAL: Update unsaved material in shared map when packing changes
			updateUnsavedMaterialInMap();
			updateTextureMetadata(type);

			updatePreview();
			emit materialChanged(_material);
		}
	}
}
void MaterialPropertiesPanel::onTransformButtonClicked(Material::TextureType type)
{
	if (!_material) return;

	// Get current texture data
	auto tex = _material->texture(type);

	// Check if texture has been loaded
	if (tex.path == "")
	{
		QMessageBox::warning(this, "No Texture",
			"Please load a texture first before editing its transform and sampler parameters.");
		return;
	}

	// Create and show dialog
	TextureParametersDialog dialog(this);

	// Load current values into dialog
	TextureParametersDialog::SamplerSettings samplers{
		tex.wrapS,
		tex.wrapT,
		tex.magFilter,
		tex.minFilter
	};
	dialog.setSamplerSettings(samplers);

	TextureParametersDialog::TextureTransform texTransform{
		tex.texCoordIndex,
		QVector2D(tex.scale.x, tex.scale.y),
		QVector2D(tex.offset.x, tex.offset.y),
		tex.rotation
	};
	dialog.setTransform(texTransform);

	// Show dialog and wait for result
	if (dialog.exec() == QDialog::Accepted)
	{
		// Get new values from dialog
		auto [newTexCoord, newScale, newOffset, newRotation] = dialog.getTransform();
		auto [newWrapS, newWrapT, newMagF, newMinF] = dialog.getSamplerSettings();

		// Update texture struct with new values
		tex.wrapS = newWrapS;
		tex.wrapT = newWrapT;
		tex.magFilter = newMagF;
		tex.minFilter = newMinF;
		tex.scale = glm::vec2(newScale.x(), newScale.y());
		tex.offset = glm::vec2(newOffset.x(), newOffset.y());
		tex.rotation = newRotation;
		tex.texCoordIndex = newTexCoord;

		// CRITICAL: Store in material (cascades to unified storage via setTexture)
		_material->setTexture(type, tex);

		// CRITICAL: Update unsaved material in shared map when texture transforms change
		updateUnsavedMaterialInMap();

		// Refresh the swatch so UV-slot badge changes are visible immediately.
		updateTexturePreview(type);

		// Update preview to show transform changes (GL context issues are now fixed)
		updatePreview();

		// Emit signals for material tracking
		emit textureSamplerChanged(_material, type);
		emit materialChanged(_material);
	}
}

void MaterialPropertiesPanel::saveCurrentMaterialTexturesBeforeSwitch()
{
	// Save textures of current material before switching to another
	if (!_material || _currentMaterialKey.isEmpty())
		return;

	// Only save if this is an unsaved or user material
	if (!_unsavedMaterialKeys.contains(_currentMaterialKey) &&
		!MaterialLibraryWidget::s_userMaterialKeys.contains(_currentMaterialKey))
		return;

	// Update shared map with current material state (includes textures and any changes)
	updateUnsavedMaterialInMap();

	qDebug() << "saveCurrentMaterialTexturesBeforeSwitch: Saved material" << _currentMaterialKey;
	_texturesDirty = false;
}

void MaterialPropertiesPanel::updatePreview()
{
	if (!_preview)
	{
		qWarning() << "updatePreview: _preview is null!";
		return;
	}
	if (!_material)
	{
		qWarning() << "updatePreview: _material is null!";
		return;
	}

	// Update preview with material
	_preview->setMaterial(*_material);

	// Update preview shape, environment, and exposure from UI controls
	if (_ui->comboShape)
	{
		int shapeIdx = _ui->comboShape->currentIndex();
		_preview->setPreviewShape(static_cast<PreviewShape>(shapeIdx));
	}

	if (_ui->comboEnv)
	{
		int envIdx = _ui->comboEnv->currentIndex();
		if (_ui->comboEnv->count() == 3)
			envIdx += 1;
		_preview->setEnvironment(static_cast<EnvMode>(envIdx));
	}

	if (_ui->comboBoxTexMode)
	{
		const QVariant texModeData = _ui->comboBoxTexMode->currentData();
		const int texMode = texModeData.isValid()
			? texModeData.toInt()
			: static_cast<int>(TexViewMode::All);
		_preview->setTextureViewMode(static_cast<TexViewMode>(texMode));
	}

	// Trigger re-render
	_preview->update();
}

void MaterialPropertiesPanel::loadTextureImageFiles()
{
	// Load actual texture image files from disk for the currently bound material
	if (!_material)
	{
		qWarning() << "loadTextureImageFiles: material is null!";
		return;
	}

	int texturesLoaded = 0;

	// Iterate through all texture types and load them
	for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
	{
		Material::TextureType type = static_cast<Material::TextureType>(i);
		QString path = QString::fromStdString(_material->texture(type).path);

		if (path.isEmpty())
			continue;

		// Check if file exists
		if (!QFile::exists(path))
		{
			qDebug() << "Texture file not found:" << path;
			continue;
		}

		// Call the appropriate setter function based on texture type
		// This mirrors the OLD API setters used by loadMaterialTexturesFromKey()
		bool loaded = false;
		switch (type)
		{
		case Material::TextureType::Albedo:
			_material->setAlbedoMap(path);
			loaded = true;
			break;
		case Material::TextureType::Normal:
			_material->setNormalMap(path);
			loaded = true;
			break;
		case Material::TextureType::Metallic:
			_material->setMetallicMap(path);
			loaded = true;
			break;
		case Material::TextureType::Roughness:
			_material->setRoughnessMap(path);
			loaded = true;
			break;
		case Material::TextureType::AmbientOcclusion:
			_material->setAOMap(path);
			loaded = true;
			break;
		case Material::TextureType::Opacity:
			_material->setOpacityMap(path);
			loaded = true;
			break;
		case Material::TextureType::Emissive:
			_material->setEmissiveMap(path);
			loaded = true;
			break;
		case Material::TextureType::Height:
			_material->setHeightMap(path);
			loaded = true;
			break;
		case Material::TextureType::Transmission:
			_material->setTransmissionMap(path);
			loaded = true;
			break;
		case Material::TextureType::IOR:
			_material->setIORMap(path);
			loaded = true;
			break;
		case Material::TextureType::SheenColor:
			_material->setSheenColorMap(path);
			loaded = true;
			break;
		case Material::TextureType::SheenRoughness:
			_material->setSheenRoughnessMap(path);
			loaded = true;
			break;
		case Material::TextureType::ClearcoatColor:
			_material->setClearcoatColorMap(path);
			loaded = true;
			break;
		case Material::TextureType::ClearcoatRoughness:
			_material->setClearcoatRoughnessMap(path);
			loaded = true;
			break;
		case Material::TextureType::ClearcoatNormal:
			_material->setClearcoatNormalMap(path);
			loaded = true;
			break;
		case Material::TextureType::Iridescence:
			_material->setIridescenceMap(path);
			loaded = true;
			break;
		case Material::TextureType::IridescenceThickness:
			_material->setIridescenceThicknessMap(path);
			loaded = true;
			break;
		case Material::TextureType::SpecularFactor:
			_material->setSpecularFactorMap(path);
			loaded = true;
			break;
		case Material::TextureType::SpecularColor:
			_material->setSpecularColorMap(path);
			loaded = true;
			break;
		case Material::TextureType::Anisotropy:
			_material->setAnisotropyMap(path);
			loaded = true;
			break;
		case Material::TextureType::DiffuseTransmission:
			_material->setDiffuseTransmissionMap(path);
			loaded = true;
			break;
		case Material::TextureType::DiffuseTransmissionColor:
			_material->setDiffuseTransmissionColorMap(path);
			loaded = true;
			break;
		case Material::TextureType::Thickness:
			_material->setThicknessMap(path);
			loaded = true;
			break;
		case Material::TextureType::Diffuse:
			_material->setDiffuseMap(path);
			loaded = true;
			break;
		case Material::TextureType::SpecularGlossiness:
			_material->setSpecularGlossinessMap(path);
			loaded = true;
			break;
		default:
			qDebug() << "    WARNING: Unknown texture type:" << i;
			break;
		}

		if (loaded)
			texturesLoaded++;
	}

	if (texturesLoaded > 0)
	{
		qDebug() << "loadTextureImageFiles: Loaded" << texturesLoaded << "texture(s)";
		_texturesDirty = true;  // Mark material as having unsaved texture changes
		updateUnsavedMaterialInMap();  // Save to shared map immediately
	}
}

void MaterialPropertiesPanel::loadMaterialTexturesFromKey(const QString& materialKey)
{
	if (!_material)
	{
		qWarning() << "loadMaterialTexturesFromKey: material is null!";
		return;
	}

	// Load unified materials.json
	QString jsonPath = PathUtils::getDataDirectory() + "/data/catalogs/materials.json";
	QFile file(jsonPath);

	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "loadMaterialTexturesFromKey: Cannot open materials.json at" << jsonPath;
		return;
	}

	QJsonObject catalog = QJsonDocument::fromJson(file.readAll()).object();
	file.close();

	// Find material in groups
	QJsonObject materialObj;
	for (const QJsonValue& groupVal : catalog["groups"].toArray())
	{
		for (const QJsonValue& matVal : groupVal.toObject()["items"].toArray())
		{
			if (matVal.toObject()["key"].toString() == materialKey)
			{
				materialObj = matVal.toObject();
				break;
			}
		}
		if (!materialObj.isEmpty())
			break;
	}

	if (materialObj.isEmpty())
	{
		qWarning() << "loadMaterialTexturesFromKey: Material not found in JSON for key:" << materialKey;
		return;
	}

	QString baseDir = PathUtils::getDataDirectory() + "/";

	// Texture type mapping: JSON key -> texture path key -> Material::TextureType
	static const QMap<QString, QPair<QString, Material::TextureType>> textureMap = {
		{"Albedo", {"albedoMapPath", Material::TextureType::Albedo}},
		{"Normal", {"normalMapPath", Material::TextureType::Normal}},
		{"Metallic", {"metallicMapPath", Material::TextureType::Metallic}},
		{"Roughness", {"roughnessMapPath", Material::TextureType::Roughness}},
		{"Height", {"heightMapPath", Material::TextureType::Height}},
		{"AmbientOcclusion", {"aoMapPath", Material::TextureType::AmbientOcclusion}},
		{"Emissive", {"emissiveMapPath", Material::TextureType::Emissive}},
		{"Opacity", {"opacityMapPath", Material::TextureType::Opacity}},
		{"Transmission", {"transmissionMapPath", Material::TextureType::Transmission}},
		{"IOR", {"iorMapPath", Material::TextureType::IOR}},
		{"SheenColor", {"sheenColorMapPath", Material::TextureType::SheenColor}},
		{"SheenRoughness", {"sheenRoughnessMapPath", Material::TextureType::SheenRoughness}},
		{"ClearcoatColor", {"clearcoatColorMapPath", Material::TextureType::ClearcoatColor}},
		{"ClearcoatRoughness", {"clearcoatRoughnessMapPath", Material::TextureType::ClearcoatRoughness}},
		{"ClearcoatNormal", {"clearcoatNormalMapPath", Material::TextureType::ClearcoatNormal}},
		{"SpecularFactor", {"specularFactorMapPath", Material::TextureType::SpecularFactor}},
		{"SpecularColor", {"specularColorMapPath", Material::TextureType::SpecularColor}},
		{"Anisotropy", {"anisotropyMapPath", Material::TextureType::Anisotropy}},
		{"Iridescence", {"iridescenceMapPath", Material::TextureType::Iridescence}},
		{"IridescenceThickness", {"iridescenceThicknessMapPath", Material::TextureType::IridescenceThickness}},
		{"DiffuseTransmission", {"diffuseTransmissionMapPath", Material::TextureType::DiffuseTransmission}},
		{"DiffuseTransmissionColor", {"diffuseTransmissionColorMapPath", Material::TextureType::DiffuseTransmissionColor}},
		{"Thickness", {"thicknessMapPath", Material::TextureType::Thickness}},
		{"Diffuse", {"diffuseMapPath", Material::TextureType::Diffuse}},
		{"SpecularGlossiness", {"specularGlossinessMapPath", Material::TextureType::SpecularGlossiness}}
	};

	// Load each texture from JSON using OLD API setters (for compatibility with onCustomMaterialApplied)
	for (auto it = textureMap.constBegin(); it != textureMap.constEnd(); ++it)
	{
		const QString& jsonKey = it.value().first;
		const Material::TextureType type = it.value().second;

		if (materialObj.contains(jsonKey))
		{
			QString relativePath = materialObj[jsonKey].toString();
			if (!relativePath.isEmpty())
			{
				QString fullPath = baseDir + relativePath;
				if (QFile::exists(fullPath))
				{
					// Set BOTH old and new API to ensure compatibility with all texture-reading code paths:
					// - Old API (setAlbedoMap, etc.): Used by onCustomMaterialApplied() handler
					// - New API (setTexture with TextureType): Used by ViewportWidget::setTexturesToObjects() for main viewer

					// First, set the OLD API (for onCustomMaterialApplied handler)
					switch (type)
					{
					case Material::TextureType::Albedo:
						_material->setAlbedoMap(fullPath);
						break;
					case Material::TextureType::Metallic:
						_material->setMetallicMap(fullPath);
						break;
					case Material::TextureType::Roughness:
						_material->setRoughnessMap(fullPath);
						break;
					case Material::TextureType::Normal:
						_material->setNormalMap(fullPath);
						break;
					case Material::TextureType::AmbientOcclusion:
						_material->setAOMap(fullPath);
						break;
					case Material::TextureType::Opacity:
						_material->setOpacityMap(fullPath);
						break;
					case Material::TextureType::Emissive:
						_material->setEmissiveMap(fullPath);
						break;
					case Material::TextureType::Height:
						_material->setHeightMap(fullPath);
						break;
					case Material::TextureType::Transmission:
						_material->setTransmissionMap(fullPath);
						break;
					case Material::TextureType::IOR:
						_material->setIORMap(fullPath);
						break;
					case Material::TextureType::SheenColor:
						_material->setSheenColorMap(fullPath);
						break;
					case Material::TextureType::SheenRoughness:
						_material->setSheenRoughnessMap(fullPath);
						break;
					case Material::TextureType::ClearcoatColor:
						_material->setClearcoatColorMap(fullPath);
						break;
					case Material::TextureType::ClearcoatRoughness:
						_material->setClearcoatRoughnessMap(fullPath);
						break;
					case Material::TextureType::ClearcoatNormal:
						_material->setClearcoatNormalMap(fullPath);
						break;
					case Material::TextureType::Iridescence:
						_material->setIridescenceMap(fullPath);
						break;
					case Material::TextureType::IridescenceThickness:
						_material->setIridescenceThicknessMap(fullPath);
						break;
					case Material::TextureType::SpecularFactor:
						_material->setSpecularFactorMap(fullPath);
						break;
					case Material::TextureType::SpecularColor:
						_material->setSpecularColorMap(fullPath);
						break;
					case Material::TextureType::Anisotropy:
						_material->setAnisotropyMap(fullPath);
						break;
					case Material::TextureType::DiffuseTransmission:
						_material->setDiffuseTransmissionMap(fullPath);
						break;
					case Material::TextureType::DiffuseTransmissionColor:
						_material->setDiffuseTransmissionColorMap(fullPath);
						break;
					case Material::TextureType::Thickness:
						_material->setThicknessMap(fullPath);
						break;
					case Material::TextureType::Diffuse:
						_material->setDiffuseMap(fullPath);
						break;
					case Material::TextureType::SpecularGlossiness:
						_material->setSpecularGlossinessMap(fullPath);
						break;
					default:
						qDebug() << "    WARNING: Unknown texture type:" << static_cast<int>(type);
						continue;
					}

					// Also set the NEW API (for ViewportWidget::setTexturesToObjects used in main viewer)
					auto tex = _material->texture(type);
					tex.path = fullPath.toStdString();
					_material->setTexture(type, tex);
				}
				else
				{
					qDebug() << "Texture file not found:" << fullPath;
				}
			}
		}
	}
}

void MaterialPropertiesPanel::populatePresetTree() {}
void MaterialPropertiesPanel::selectPresetInTree(const QString& presetName, bool userPreset) {}
void MaterialPropertiesPanel::applyMaterialPreset(const QString& presetName, const QString& presetFolder, bool userPreset) {}
void MaterialPropertiesPanel::loadMaterialPresetMetadata(const QString& presetName) {}
bool MaterialPropertiesPanel::saveCurrentPresetMetadata()
{
	// This is called internally after UI prompts have been handled
	// It delegates to MaterialLibraryWidget's backend save method
	// NOTE: Texture file copying is handled by saveUserMaterialToUserLocation()
	//       via the material's toVariantMap() which now includes all metadata
	return true; // Actual saving handled by onSaveToLibrary()
}

QString MaterialPropertiesPanel::materialLibraryRoot() const
{
	return PathUtils::getDataDirectory() + "/textures/materials";
}

QString MaterialPropertiesPanel::userMaterialLibraryRoot() const
{
	QString appData = PathUtils::getDataDirectory();
	return appData.left(appData.lastIndexOf('/')) + "/user_materials";
}

QString MaterialPropertiesPanel::currentPresetMetadataPath() const
{
	if (_currentPresetName.isEmpty())
		return QString();
	QString root = _currentPresetIsUser ? userMaterialLibraryRoot() : materialLibraryRoot();
	return root + "/" + _currentPresetFolder + "/material.json";
}

void MaterialPropertiesPanel::onTextureButtonClicked(Material::TextureType type)
{
	QString texFolder = _lastUsedTextureFolder;
	if (texFolder.isEmpty())
	{
		QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
		texFolder = settings.value("lineEditTextureDir", QString()).toString();
	}
	if (texFolder.isEmpty())
		texFolder = PathUtils::getDataDirectory() + "/textures/materials";

	QString file = QFileDialog::getOpenFileName(this, tr("Select Texture"), texFolder,
		tr("Image Files (*.png *.jpg *.jpeg *.bmp *.tga);;All Files (*)"));

	if (!file.isEmpty())
		setTextureMapPath(type, file);
}

void MaterialPropertiesPanel::onClearAllTextures()
{
	if (QMessageBox::question(this, tr("Clear All Textures"),
		tr("Are you sure you want to clear all textures?")) == QMessageBox::Yes)
		clearAllTexturesMaps();
}

void MaterialPropertiesPanel::onMaterialPresetSelected(const Material& mat)
{
	// IMPORTANT: Save any unsaved texture changes from current material before switching
	saveCurrentMaterialTexturesBeforeSwitch();
	// IMPORTANT: Save scalar property changes to MDI-scoped cache before switching
	updateUnsavedMaterialInMap();

	// Material is already populated with scalar properties from tree widget
	// Now load textures and bind the material
	if (!_ui)
	{
		qWarning() << "onMaterialPresetSelected: _ui is null!";
		return;
	}

	// Create a copy of the material to work with
	if (!_material) _material = new Material();
	*_material = mat;
	// Ensure ADS values are recalculated after copy assignment
	// (copy assignment operator doesn't call updateConsistency)
	_material->updateConsistency();

	// Get the material key from the selected tree item to load textures
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
		if (!selected.isEmpty())
		{
			QString materialKey = selected.first()->data(0, Qt::UserRole).toString();
			if (!materialKey.isEmpty())
			{
				_currentMaterialKey = materialKey;

				// Extract group/category from parent tree item
				QTreeWidgetItem* parentItem = selected.first()->parent();
				if (parentItem)
				{
					_currentMaterialGroup = parentItem->text(0);
				}
				else
				{
					_currentMaterialGroup.clear();
				}

				// Determine material type early so we can use it in cache restoration logic
				bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(materialKey);
				bool isUnsavedMaterial = _unsavedMaterialKeys.contains(materialKey);

				// Check if this material was previously modified and cached
				// If so, restore all data (both scalars and textures) from cache to preserve user modifications
				if (_materialCacheRef)
				{
					auto cachedIt = _materialCacheRef->find(materialKey);
					if (cachedIt != _materialCacheRef->end())
					{
						// For user-created or unsaved materials, restore everything
						// This preserves both scalar and texture modifications from previous session
						if (isUserMaterial || isUnsavedMaterial)
						{
							qDebug() << "onMaterialPresetSelected: BEFORE restore - _material albedo:" << _material->albedoColor();
							qDebug() << "  Cache entry albedo:" << cachedIt.value().material.albedoColor();
							*_material = cachedIt.value().material;
							// Ensure ADS values are recalculated after copy assignment from cache
							_material->updateConsistency();
							qDebug() << "  AFTER restore - _material albedo:" << _material->albedoColor();
							qDebug() << "Restored cached material (user/unsaved):" << materialKey;
						}
						else
						{
							// For preset materials, restore ONLY textures to preserve fresh scalar defaults
							const Material& cachedMaterial = cachedIt.value().material;
							for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
							{
								Material::TextureType type = static_cast<Material::TextureType>(i);
								const auto& cachedTex = cachedMaterial.texture(type);
								// Only restore texture if it has a valid path in the cache
								if (!cachedTex.path.empty())
								{
									_material->setTexture(type, cachedTex);
									qDebug() << "Restored cached texture for type:" << i << "path:" << QString::fromStdString(cachedTex.path);
								}
							}
						}
					}
				}

				// For user materials or unsaved materials, the texture paths are already in the material object
				// We just need to load the texture image files from disk
				if (isUserMaterial || isUnsavedMaterial)
				{
					loadTextureImageFiles();
				}
				else
				{
					// Load textures from shipped materials.json by material key
					loadMaterialTexturesFromKey(materialKey);
				}
			}
		}
	}

	// Bind material and update UI
	bindMaterial(_material);

	// Update refresh button state based on material type and modification status
	updateRefreshButtonState();

	// NOTE: Do NOT emit materialApplied here!
	// materialApplied should only be emitted when user clicks Apply button
	// Selecting from tree just loads the material for editing/preview
}

void MaterialPropertiesPanel::onMaterialDoubleClicked(const Material& mat)
{
	// IMPORTANT: Save any unsaved texture changes from current material before switching
	saveCurrentMaterialTexturesBeforeSwitch();
	// IMPORTANT: Save scalar property changes to MDI-scoped cache before switching
	updateUnsavedMaterialInMap();

	// Load and bind material for preview (same as single-click)
	if (!_material) _material = new Material();
	*_material = mat;
	// Ensure ADS values are recalculated after copy assignment
	_material->updateConsistency();

	// Get material key from selected tree item to load textures
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
		if (!selected.isEmpty())
		{
			QString materialKey = selected.first()->data(0, Qt::UserRole).toString();
			if (!materialKey.isEmpty())
			{
				_currentMaterialKey = materialKey;

				// Extract group/category from parent tree item
				QTreeWidgetItem* parentItem = selected.first()->parent();
				if (parentItem)
				{
					_currentMaterialGroup = parentItem->text(0);
				}
				else
				{
					_currentMaterialGroup.clear();
				}

				// Determine material type early so we can use it in cache restoration logic
				bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(materialKey);
				bool isUnsavedMaterial = _unsavedMaterialKeys.contains(materialKey);

				// Check if this material was previously modified and cached
				// If so, restore all data (both scalars and textures) from cache to preserve user modifications
				if (_materialCacheRef)
				{
					auto cachedIt = _materialCacheRef->find(materialKey);
					if (cachedIt != _materialCacheRef->end())
					{
						// For user-created or unsaved materials, restore everything
						// This preserves both scalar and texture modifications from previous session
						if (isUserMaterial || isUnsavedMaterial)
						{
							*_material = cachedIt.value().material;
							// Ensure ADS values are recalculated after copy assignment from cache
							_material->updateConsistency();
							qDebug() << "Restored cached material (user/unsaved):" << materialKey;
						}
						else
						{
							// For preset materials, restore ONLY textures to preserve fresh scalar defaults
							const Material& cachedMaterial = cachedIt.value().material;
							for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
							{
								Material::TextureType type = static_cast<Material::TextureType>(i);
								const auto& cachedTex = cachedMaterial.texture(type);
								// Only restore texture if it has a valid path in the cache
								if (!cachedTex.path.empty())
								{
									_material->setTexture(type, cachedTex);
									qDebug() << "Restored cached texture for type:" << i << "path:" << QString::fromStdString(cachedTex.path);
								}
							}
						}
					}

				}
				// For user materials or unsaved materials, the texture paths are already in the material object
				if (isUserMaterial || isUnsavedMaterial)
				{
					loadTextureImageFiles();
				}
				else
				{
					// Load textures from shipped materials.json by material key
					loadMaterialTexturesFromKey(materialKey);
				}
			}
		}
	}

	// Bind material and update UI
	bindMaterial(_material);

	// Update refresh button state based on material type and modification status
	updateRefreshButtonState();

	// EMIT SIGNAL TO APPLY TO MESH (this is the key difference from single-click)
	emit materialApplied(*_material);
}

void MaterialPropertiesPanel::onSaveToLibrary()
{
	if (!_material)
	{
		QMessageBox::warning(this, tr("No Material"), tr("No material is currently bound to save."));
		return;
	}

	// For mesh materials, prompt for name and category, then save with cleanup only on success
	if (_currentMaterialKey.startsWith("__mesh_"))
	{
		QString originalMeshKey = _currentMaterialKey;

		// Get current material name for suggestion
		QString suggestedName;
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
			if (!selected.isEmpty())
			{
				suggestedName = selected.first()->text(0);
				// Remove " *" suffix if present
				if (suggestedName.endsWith(" *"))
					suggestedName = suggestedName.mid(0, suggestedName.length() - 2);
			}
		}
		if (suggestedName.isEmpty())
			suggestedName = "New Material";

		// Ask for name
		bool okName = false;
		QString enteredName = QInputDialog::getText(this,
			tr("Material Name"),
			tr("Enter name for material:"),
			QLineEdit::Normal,
			suggestedName,
			&okName);
		if (!okName || enteredName.trimmed().isEmpty()) return;  // User cancelled - gracefully exit
		QString materialName = enteredName.trimmed();

		// Ask for category
		QString selectedGroup;
		QStringList groups;
		const auto& sharedGroups = MaterialLibraryWidget::sharedGroups();
		for (const auto& g : sharedGroups)
		{
			if (g.first != "Mesh Materials")
			{  // Exclude temporary mesh materials group
				groups << g.first;
			}
		}
		if (groups.isEmpty()) groups << QStringLiteral("User Materials");

		bool okGroup = false;
		QString picked = QInputDialog::getItem(this,
			tr("Choose Category"),
			tr("Select a category to save into:"),
			groups,
			0,
			true,
			&okGroup);
		if (!okGroup) return;  // User cancelled - gracefully exit
		selectedGroup = picked.trimmed().isEmpty() ? QStringLiteral("User Materials") : picked.trimmed();

		// User confirmed both name and category - now proceed with save and cleanup
		// Generate a unique UUID key for this new material
		QString newKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
		Material mat = *_material;

		// Create user material folder and copy texture files
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString materialFolder = QDir(userRoot).filePath(newKey);
		QDir materialDir(materialFolder);

		if (!materialDir.exists())
		{
			if (!materialDir.mkpath("."))
			{
				QMessageBox::warning(this, tr("Folder Creation Failed"),
					tr("Could not create material folder: %1").arg(materialFolder));
				return;
			}
		}

		// Copy texture files
		QString materialFolderCanonical = QFileInfo(materialFolder).canonicalFilePath();

		for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
		{
			Material::TextureType type = static_cast<Material::TextureType>(i);
			Material::Texture tex = mat.texture(type);

			if (tex.path.empty()) continue;

			QString sourcePath = QString::fromStdString(tex.path);
			QFileInfo fileInfo(sourcePath);
			QString fileName = fileInfo.fileName();
			QString destPath = materialDir.filePath(fileName);

			QString sourceCanonical = QFileInfo(sourcePath).canonicalFilePath();

			// Skip if already in our folder
			if (sourceCanonical == QFileInfo(destPath).canonicalFilePath())
				continue;

			// Copy texture file
			if (QFile::exists(sourcePath))
			{
				if (!QFile::copy(sourcePath, destPath))
				{
					qWarning() << "Failed to copy texture:" << sourcePath << "to" << destPath;
				}
				else
				{
					// Update material to use relative path
					tex.path = fileName.toStdString();
					mat.setTexture(type, tex);
				}
			}
		}

		// Save the material to user library
		if (libraryWidget) libraryWidget->blockSignals(true);

		QString err;
		bool saved = MaterialLibraryWidget::saveUserMaterialToUserLocation(selectedGroup, newKey, materialName, mat, this, &err);

		if (libraryWidget) libraryWidget->blockSignals(false);

		if (!saved)
		{
			if (!err.isEmpty() && err != QStringLiteral("User cancelled overwrite"))
			{
				QMessageBox::warning(this, tr("Save Material Failed"), err);
			}
			return;
		}

		// Mark key as user key
		MaterialLibraryWidget::s_userMaterialKeys.insert(newKey);

		// Update shared map with the saved material
		{
			Material matWithAbsolutePaths = mat;
			QString materialFolder = QDir(userRoot).filePath(newKey);

			// Restore absolute paths from relative paths
			for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
			{
				Material::TextureType type = static_cast<Material::TextureType>(i);
				Material::Texture tex = matWithAbsolutePaths.texture(type);

				if (tex.path.empty()) continue;

				QString texPath = QString::fromStdString(tex.path);
				if (!texPath.contains('/') && !texPath.contains('\\'))
				{
					QString absolutePath = QDir(materialFolder).filePath(texPath);
					tex.path = absolutePath.toStdString();
					matWithAbsolutePaths.setTexture(type, tex);
				}
			}

			auto& mutableSharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
				MaterialLibraryWidget::sharedMaterialMap());
			mutableSharedMap[newKey] = [material = matWithAbsolutePaths]() { return material; };
		}

		// Refresh the tree and select the newly saved material
		if (libraryWidget)
		{
			libraryWidget->blockSignals(true);
			libraryWidget->refreshMaterialTree();
			restoreAsterisksForUnsavedMaterials();
			libraryWidget->selectMaterialByKey(newKey);
			libraryWidget->blockSignals(false);

			// Load the newly saved material into preview
			if (libraryWidget->sharedMaterialMap().contains(newKey))
			{
				Material savedMat = libraryWidget->sharedMaterialMap()[newKey]();
				_material = new Material(savedMat);
				loadTextureImageFiles();
				bindMaterial(_material);
			}
		}

		// Update current material key
		_currentMaterialKey = newKey;
		_currentMaterialGroup = selectedGroup;

		// Clean up the original mesh material from the tree
		if (originalMeshKey.startsWith("__mesh_"))
		{
			_unsavedMaterialKeys.remove(originalMeshKey);

			auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
				MaterialLibraryWidget::sharedMaterialMap());
			sharedMap.remove(originalMeshKey);

			auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
				MaterialLibraryWidget::sharedGroups());
			for (auto& groupPair : mutableGroups)
			{
				if (groupPair.first == "Mesh Materials")
				{
					auto& materials = groupPair.second;
					for (int i = 0; i < materials.size(); ++i)
					{
						if (materials[i].second == originalMeshKey)
						{
							materials.removeAt(i);
							break;
						}
					}
					break;
				}
			}

			if (_materialCacheRef)
			{
				_materialCacheRef->remove(originalMeshKey);
			}

			qDebug() << "Cleaned up mesh material from tree:" << originalMeshKey;

			// Refresh tree again to reflect the removal of the mesh material
			if (libraryWidget)
			{
				libraryWidget->blockSignals(true);
				libraryWidget->refreshMaterialTree();
				restoreAsterisksForUnsavedMaterials();
				libraryWidget->selectMaterialByKey(newKey);
				libraryWidget->blockSignals(false);
			}

			// Remove the "Mesh Materials" category if it's now empty
			removeEmptyMeshMaterialsCategory();

			// Refresh tree one more time to remove the empty category
			if (libraryWidget)
			{
				libraryWidget->blockSignals(true);
				libraryWidget->refreshMaterialTree();
				restoreAsterisksForUnsavedMaterials();
				libraryWidget->selectMaterialByKey(newKey);
				libraryWidget->blockSignals(false);
			}
		}

		_editingMeshUuid = QUuid();  // Clear editing state

		QMessageBox::information(this, tr("Material Saved"),
			tr("New user material '%1' has been created in category '%2'.").arg(materialName, selectedGroup));
		return;
	}

	// Get current material copy
	Material mat = *_material;

	// Derive defaults from selected tree item (if any)
	QString key = _currentMaterialKey;
	QString name;
	QString groupLabel;

	// Check if current material is an existing user material (not factory, not unsaved)
	const auto& sharedMap = MaterialLibraryWidget::sharedMaterialMap();
	bool isExistingUserMaterial = !key.isEmpty() &&
		!_unsavedMaterialKeys.contains(key) &&
		sharedMap.contains(key) &&
		MaterialLibraryWidget::s_userMaterialKeys.contains(key);

	qDebug() << "onSaveToLibrary: key=" << key
		<< "inUnsaved=" << _unsavedMaterialKeys.contains(key)
		<< "inSharedMap=" << sharedMap.contains(key)
		<< "inUserKeys=" << MaterialLibraryWidget::s_userMaterialKeys.contains(key)
		<< "isExistingUserMaterial=" << isExistingUserMaterial;

	// If existing user material, overwrite directly without prompting
	if (isExistingUserMaterial)
	{
		saveCurrentMaterialTexturesBeforeSwitch();

		// Get material name and group from current state
		QString name;
		QString groupLabel;
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
			if (!selected.isEmpty())
			{
				name = selected.first()->text(0).trimmed();
				if (name.endsWith(" *")) name = name.mid(0, name.length() - 2);

				QTreeWidgetItem* parentItem = selected.first()->parent();
				if (parentItem) groupLabel = parentItem->text(0);
			}
		}

		if (name.isEmpty()) name = "User Material";
		if (groupLabel.isEmpty()) groupLabel = _currentMaterialGroup.isEmpty() ? "User Materials" : _currentMaterialGroup;

		// IMPORTANT: Copy any new/modified textures to the material folder
		Material mat = *_material;
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString materialFolder = QDir(userRoot).filePath(key);
		QDir materialDir(materialFolder);

		// Ensure material folder exists
		if (!materialDir.exists())
		{
			materialDir.mkpath(".");
		}

		// Copy texture files for all textures in the material
		// CRITICAL: Convert ALL absolute paths to relative, and only write successfully copied files
		QString materialFolderCanonical = QFileInfo(materialFolder).canonicalFilePath();

		for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
		{
			Material::TextureType type = static_cast<Material::TextureType>(i);
			Material::Texture tex = mat.texture(type);

			if (tex.path.empty()) continue;

			QString sourcePath = QString::fromStdString(tex.path);
			QFileInfo fileInfo(sourcePath);
			QString fileName = fileInfo.fileName();
			QString destPath = materialDir.filePath(fileName);

			// Check if source is already in our material folder
			// This happens when overwriting - paths are resolved to absolute but point to same folder
			QString sourceCanonical = QFileInfo(sourcePath).canonicalFilePath();
			QString destCanonical = QFileInfo(destPath).canonicalFilePath();

			if (sourceCanonical == destCanonical ||
				sourceCanonical.startsWith(materialFolderCanonical + QDir::separator()))
			{
				// File is already in our material folder, just update to relative path
				tex.path = fileName.toStdString();
				mat.setTexture(type, tex);
				qDebug() << "Overwrite: Texture already in folder, using relative:" << fileName;
				continue;
			}

			// Check if source file actually exists
			if (!QFile::exists(sourcePath))
			{
				// Texture file not found - skip it (don't write invalid path to JSON)
				qWarning() << "Texture file not found, skipping:" << sourcePath;
				tex.path = "";  // Clear path so it's not written to JSON
				mat.setTexture(type, tex);
				continue;
			}

			// Copy the texture file
			bool copySuccess = QFile::copy(sourcePath, destPath);
			if (copySuccess)
			{
				qDebug() << "Copied texture:" << fileName << "to" << destPath;
				// Update material's texture path to be relative
				tex.path = fileName.toStdString();
				mat.setTexture(type, tex);
			}
			else
			{
				// Copy failed - skip this texture (don't write invalid path to JSON)
				qWarning() << "Failed to copy texture file:" << fileName;
				tex.path = "";  // Clear path so it's not written to JSON
				mat.setTexture(type, tex);
			}
		}

		// Block signals during save
		if (libraryWidget) libraryWidget->blockSignals(true);

		// Save the material with textures to JSON using backend
		QString err;
		bool saved = MaterialLibraryWidget::saveUserMaterialToUserLocation(groupLabel, key, name, mat, this, &err);

		if (libraryWidget) libraryWidget->blockSignals(false);

		// Update shared map with the saved material (SAME AS SAVE AS)
		// The JSON has relative paths, but shared map needs absolute paths for runtime
		if (saved)
		{
			Material matWithAbsolutePaths = mat;
			QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
			QString materialFolder = QDir(userRoot).filePath(key);

			// Restore absolute paths from relative paths for shared map
			for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
			{
				Material::TextureType type = static_cast<Material::TextureType>(i);
				Material::Texture tex = matWithAbsolutePaths.texture(type);

				if (tex.path.empty()) continue;

				QString texPath = QString::fromStdString(tex.path);
				// If it's relative (just filename, no path separators), convert to absolute
				if (!texPath.contains('/') && !texPath.contains('\\'))
				{
					QString absolutePath = QDir(materialFolder).filePath(texPath);
					tex.path = absolutePath.toStdString();
					matWithAbsolutePaths.setTexture(type, tex);
					qDebug() << "Restored absolute path for save overwrite:" << absolutePath;
				}
			}

			auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
				MaterialLibraryWidget::sharedMaterialMap());
			sharedMap[key] = [material = matWithAbsolutePaths]() { return material; };
			qDebug() << "Updated shared map for overwritten material:" << key;
		}

		if (saved)
		{
			_unsavedMaterialKeys.remove(key);
			qDebug() << "Overwrote user material with textures:" << key;

			// Re-select the material after save (saveUserMaterialToUserLocation emits a global signal
			// that triggers tree refresh which auto-selects first item)
			if (libraryWidget)
			{
				libraryWidget->blockSignals(true);
				libraryWidget->selectMaterialByKey(key);
				libraryWidget->blockSignals(false);
			}

			QMessageBox::information(this, tr("Material Saved"), tr("User material '%1' has been updated.").arg(name));

			// Update refresh button state (button should now disable since material is no longer unsaved)
			updateRefreshButtonState();
		}
		else
		{
			qWarning() << "Failed to save material:" << err;
			QMessageBox::warning(this, tr("Save Failed"), tr("Failed to save material: %1").arg(err));
		}
		return;
	}

	// Check if this is an unsaved material (newly created OR modified existing)
	if (_unsavedMaterialKeys.contains(_currentMaterialKey))
	{
		// CRITICAL FIX: Distinguish between:
		// 1. NEW material created via "New" button - should generate NEW UUID
		// 2. EXISTING material that was modified - should KEEP original UUID
		//
		// Check if material is in s_userMaterialKeys:
		// - If YES: existing saved material that was modified → keep UUID
		// - If NO: newly created material → keep UUID (was assigned in onCreateNewMaterial)
		bool isExistingLibraryMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(_currentMaterialKey);

		// Extract the name from the tree (it has " *" suffix)
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			QList<QTreeWidgetItem*> items = libraryWidget->findItems("*", Qt::MatchRecursive | Qt::MatchWildcard);
			for (QTreeWidgetItem* item : items)
			{
				if (item->data(0, Qt::UserRole).toString() == _currentMaterialKey)
				{
					QString displayName = item->text(0);
					// Remove the " *" suffix
					if (displayName.endsWith(" *"))
					{
						name = displayName.mid(0, displayName.length() - 2);
					}
					else
					{
						name = displayName;
					}
					break;
				}
			}
		}

		if (name.isEmpty())
		{
			name = QStringLiteral("New Material");
		}

		// Keep the UUID that was assigned (either from onCreateNewMaterial or from library)
		// This prevents duplicate UUID generation
		key = _currentMaterialKey;
		qDebug() << "Save unsaved material, keeping UUID:" << key
			<< "(existing library material:" << isExistingLibraryMaterial << ")";

		// Use the stored group
		groupLabel = _currentMaterialGroup;
		if (groupLabel.isEmpty())
		{
			groupLabel = QStringLiteral("User Materials");
		}

		// NOTE: Do NOT remove from _unsavedMaterialKeys here!
		// We need to keep it in the set so the cleanup code below can find it and remove
		// it from the shared maps. The removal happens after cleanup at line 2403.
	}

	// If group label not already set, try to derive it from tree selection
	if (groupLabel.isEmpty())
	{
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			QList<QTreeWidgetItem*> selectedItems = libraryWidget->selectedItems();
			if (!selectedItems.isEmpty())
			{
				QTreeWidgetItem* selectedItem = selectedItems.first();

				// Check if selected item is a category (has children) or material (no children)
				if (selectedItem->childCount() > 0)
				{
					// It's a category node - use it directly
					groupLabel = selectedItem->text(0);
					qDebug() << "Auto-detected group from selection:" << groupLabel;
				}
				else
				{
					// It's a material node - get its parent category
					QTreeWidgetItem* parentItem = selectedItem->parent();
					if (parentItem)
					{
						groupLabel = parentItem->text(0);
						qDebug() << "Auto-detected group from material parent:" << groupLabel;
					}
				}
			}
		}
	}

	// If group still not set, ask user
	if (groupLabel.isEmpty())
	{
		QStringList groups;
		const auto& sharedGroups = MaterialLibraryWidget::sharedGroups();
		for (const auto& g : sharedGroups) groups << g.first;
		if (groups.isEmpty()) groups << QStringLiteral("User Materials");

		bool ok = false;
		QString picked = QInputDialog::getItem(this,
			tr("Choose Group"),
			tr("Select a group to save into:"),
			groups,
			0,
			true,
			&ok);
		if (!ok) return;
		groupLabel = picked.trimmed();
		if (groupLabel.isEmpty()) groupLabel = QStringLiteral("User Materials");
	}

	// Determine whether the current key exists and whether it is user or factory
	bool keyExists = !key.isEmpty() && sharedMap.contains(key);
	bool isUserKey = MaterialLibraryWidget::s_userMaterialKeys.contains(key);
	bool isUnsavedMaterial = _unsavedMaterialKeys.contains(key);

	// If a factory material is referenced (exists && not a user key && not unsaved), force "Save As"
	// Unsaved materials (newly created or modified) should NOT be treated as factory materials
	// They should be saved normally with their existing UUID
	if (keyExists && !isUserKey && !isUnsavedMaterial)
	{
		QMessageBox::information(this,
			tr("Save As New User Material"),
			tr("You are saving changes to a factory material. "
				"A new user material will be created instead of modifying the factory material."));

		// Suggest a name for the new user material
		QString suggestedName = name.isEmpty() ? QStringLiteral("User Material") : QStringLiteral("User %1").arg(name);

		// Ask user for display name
		bool okName = false;
		QString enteredName = QInputDialog::getText(this,
			tr("Material Name"),
			tr("Display name for material:"),
			QLineEdit::Normal,
			suggestedName,
			&okName);
		if (!okName || enteredName.trimmed().isEmpty()) return;
		name = enteredName.trimmed();

		// Generate a unique UUID key for this material
		key = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	else
	{
		// Not saving over a factory material
		if (key.isEmpty())
		{
			if (name.isEmpty())
			{
				bool ok = false;
				QString suggestedName = QStringLiteral("New Material");
				QString enteredName = QInputDialog::getText(this,
					tr("Material Name"),
					tr("Display name for material:"),
					QLineEdit::Normal,
					suggestedName,
					&ok);
				if (!ok || enteredName.trimmed().isEmpty()) return;
				name = enteredName.trimmed();
			}

			// Generate a unique UUID key for this material
			key = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
	}

	// Final sanity check
	if (key.isEmpty() || name.isEmpty() || groupLabel.isEmpty())
	{
		QMessageBox::warning(this, tr("Save Failed"), tr("Invalid material name/key/group."));
		return;
	}

	// Create user material folder and copy texture files
	// Get user materials root path
	QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
	QString materialFolder = QDir(userRoot).filePath(key);
	QDir materialDir(materialFolder);

	if (!materialDir.exists())
	{
		if (!materialDir.mkpath("."))
		{
			QMessageBox::warning(this, tr("Folder Creation Failed"),
				tr("Could not create material folder: %1").arg(materialFolder));
			return;
		}
	}

	// Copy texture files for all loaded textures in the material
	// CRITICAL: All user material textures must be self-contained in the material folder
	// with relative paths. Convert absolute paths to relative if they're in our folder.
	// If a texture cannot be copied, skip it (don't write invalid absolute paths to JSON)
	QString materialFolderCanonical = QFileInfo(materialFolder).canonicalFilePath();

	for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
	{
		Material::TextureType type = static_cast<Material::TextureType>(i);
		Material::Texture tex = mat.texture(type);

		if (tex.path.empty()) continue; // Skip unloaded textures

		QString sourcePath = QString::fromStdString(tex.path);
		QFileInfo fileInfo(sourcePath);
		QString fileName = fileInfo.fileName();
		QString destPath = materialDir.filePath(fileName);

		// Check if source is already in our material folder
		// This happens when overwriting a previously saved material
		QString sourceCanonical = QFileInfo(sourcePath).canonicalFilePath();
		QString destCanonical = QFileInfo(destPath).canonicalFilePath();

		if (sourceCanonical == destCanonical ||
			sourceCanonical.startsWith(materialFolderCanonical + QDir::separator()))
		{
			// File is already in our material folder, just update to relative path
			tex.path = fileName.toStdString();
			mat.setTexture(type, tex);
			qDebug() << "Overwriting material: Texture already in folder, using relative:" << fileName;
			continue;
		}

		// Check if source file actually exists
		if (!QFile::exists(sourcePath))
		{
			// Texture file not found - skip it (don't write invalid path to JSON)
			qWarning() << "Texture file not found, skipping:" << sourcePath;
			tex.path = "";  // Clear path so it's not written to JSON
			mat.setTexture(type, tex);
			continue;
		}

		// Copy the texture file to material folder
		bool copySuccess = QFile::copy(sourcePath, destPath);
		if (!copySuccess)
		{
			// If destination already exists, it means another texture type already copied this file
			// (ORM case: same file assigned to O, R, M). Still set the path.
			if (QFile::exists(destPath))
			{
				tex.path = fileName.toStdString();
				mat.setTexture(type, tex);
				qDebug() << "Texture already in folder (ORM dedup), using existing:" << fileName;
				continue;
			}

			// Actual copy failure - file doesn't exist or permission error
			qWarning() << "Failed to copy texture file:" << fileName << "from" << sourcePath;
			tex.path = "";  // Clear path so it's not written to JSON
			mat.setTexture(type, tex);
			continue;
		}

		// Success - update to relative path (just filename)
		tex.path = fileName.toStdString();
		mat.setTexture(type, tex);
		qDebug() << "Copied and saved texture as relative:" << fileName;
	}

	// Save via backend (will use updated paths with texture metadata)
	// NOTE: saveUserMaterialToUserLocation() emits MaterialRegistry::instance().materialsChanged()
	// which would trigger populateMaterials() and unwanted signal handlers.
	// Block signals on tree widget so the materialsChanged signal won't cause populateMaterials
	// to run (and thus won't emit materialPreview/materialSelected that trigger the dialog).
	// We'll do the tree refresh ourselves afterwards on our own schedule.
	if (auto* libWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget))
	{
		libWidget->blockSignals(true);
	}

	QString err;
	bool saved = MaterialLibraryWidget::saveUserMaterialToUserLocation(groupLabel, key, name, mat, this, &err);

	if (auto* libWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget))
	{
		libWidget->blockSignals(false);
	}

	if (!saved)
	{
		if (!err.isEmpty() && err != QStringLiteral("User cancelled overwrite"))
		{
			QMessageBox::warning(this, tr("Save Material Failed"), err);
		}
		return;
	}

	// If this was a NEWLY CREATED unsaved material, clean up the old unsaved entry BEFORE refreshing
	// Do NOT cleanup if it's a modified existing material - those were already saved before
	bool isNewlyCreatedMaterial = !MaterialLibraryWidget::s_userMaterialKeys.contains(_currentMaterialKey);

	if (!_currentMaterialKey.isEmpty() && _unsavedMaterialKeys.contains(_currentMaterialKey) && isNewlyCreatedMaterial)
	{
		// Remove from s_materialMap
		auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
			MaterialLibraryWidget::sharedMaterialMap());
		sharedMap.remove(_currentMaterialKey);

		// Remove from s_groups
		auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
			MaterialLibraryWidget::sharedGroups());

		for (auto& groupPair : mutableGroups)
		{
			// Search in ALL groups for the old unsaved entry
			for (int i = 0; i < groupPair.second.size(); ++i)
			{
				if (groupPair.second[i].second == _currentMaterialKey)
				{
					groupPair.second.removeAt(i);
					qDebug() << "Removed old unsaved material from group:" << groupPair.first;
					break;
				}
			}
		}

		// Remove from unsaved set
		_unsavedMaterialKeys.remove(_currentMaterialKey);
		qDebug() << "Cleaned up old unsaved material:" << _currentMaterialKey << "New key:" << key;
	}
	else if (!_currentMaterialKey.isEmpty() && _unsavedMaterialKeys.contains(_currentMaterialKey))
	{
		// This is a modified existing material - just remove from unsaved set
		// The material is already in the library with the same UUID
		_unsavedMaterialKeys.remove(_currentMaterialKey);
		qDebug() << "Marked modified existing material as saved:" << _currentMaterialKey;
	}

	// Mark key as user key (after cleanup)
	MaterialLibraryWidget::s_userMaterialKeys.insert(key);

	// CRITICAL: Update the shared map lambda with the just-saved material
	// BUT: Convert relative texture paths back to absolute paths for the shared map
	// The JSON file has relative paths, but the runtime needs absolute paths
	{
		Material matWithAbsolutePaths = mat;
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString materialFolder = QDir(userRoot).filePath(key);

		// Restore absolute paths from relative paths
		for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
		{
			Material::TextureType type = static_cast<Material::TextureType>(i);
			Material::Texture tex = matWithAbsolutePaths.texture(type);

			if (tex.path.empty()) continue;

			QString texPath = QString::fromStdString(tex.path);
			// If it's relative (just filename, no path separators), convert to absolute
			if (!texPath.contains('/') && !texPath.contains('\\'))
			{
				QString absolutePath = QDir(materialFolder).filePath(texPath);
				tex.path = absolutePath.toStdString();
				matWithAbsolutePaths.setTexture(type, tex);
				qDebug() << "Restored absolute path for" << QString::fromStdString(tex.type) << ":" << absolutePath;
			}
		}

		auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
			MaterialLibraryWidget::sharedMaterialMap());
		sharedMap[key] = [material = matWithAbsolutePaths]() { return material; };
		qDebug() << "Updated shared map lambda for saved material:" << key;
	}

	// Refresh the tree and select the newly saved material
	if (auto* libWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget))
	{
		// Block ALL signals during tree refresh to prevent any signal-driven side effects
		// This prevents materialSelected/materialPreview from triggering handlers
		libWidget->blockSignals(true);

		// CRITICAL: Clear the in-memory maps to force reload from JSON
		// The newly saved material is in JSON but NOT in s_groups/s_materialMap yet
		// because saveUserMaterialToUserLocation() only writes JSON, doesn't update maps
		auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
			MaterialLibraryWidget::sharedGroups());
		auto& mutableMap = const_cast<QMap<QString, std::function<Material()>>&>(
			MaterialLibraryWidget::sharedMaterialMap());
		mutableGroups.clear();
		mutableMap.clear();
		qDebug() << "Cleared material maps to force JSON reload";

		// Refresh the tree and select the newly saved material by key
		libWidget->refreshMaterialTree();

		// Re-add asterisks to all materials still in _unsavedMaterialKeys
		// (refreshMaterialTree loads from JSON which doesn't have asterisks)
		restoreAsterisksForUnsavedMaterials();

		libWidget->selectMaterialByKey(key);

		// Re-enable signals
		libWidget->blockSignals(false);

		// Now manually load the newly saved material into preview WITHOUT triggering handlers
		// This bypasses all signal-based slot calls
		if (libWidget->sharedMaterialMap().contains(key))
		{
			// Get the material from the cache (lambda already resolved relative paths to absolute)
			Material savedMat = libWidget->sharedMaterialMap()[key]();

			// Load material directly into panel without going through signal handlers
			// This prevents materialApplied from being emitted
			_material = new Material(savedMat);

			// IMPORTANT: Load texture image files BEFORE bindMaterial()
			// bindMaterial() calls updatePreview(), so textures must be loaded first
			// otherwise the preview shows white until selection changes
			loadTextureImageFiles();

			bindMaterial(_material);
		}
	}

	// Update current material key to the saved key
	_currentMaterialKey = key;
	_currentMaterialGroup = groupLabel;

	QMessageBox::information(this, tr("Material Saved"),
		tr("Material '%1' successfully saved to your library under category '%2'.").arg(name, groupLabel));

	// Update refresh button state
	updateRefreshButtonState();
}

void MaterialPropertiesPanel::onSaveAsToLibrary()
{
	if (!_material)
	{
		QMessageBox::warning(this, tr("No Material"), tr("No material is currently bound to save."));
		return;
	}

	// Save As always creates a new user material (copy of current material)
	Material mat = *_material;

	QString name;
	QString groupLabel;

	// Get current material name for suggestion
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
		if (!selected.isEmpty())
		{
			name = selected.first()->text(0);
			// Remove " *" suffix if present
			if (name.endsWith(" *"))
				name = name.mid(0, name.length() - 2);
		}
	}

	if (name.isEmpty())
		name = _currentMaterialKey.isEmpty() ? "New Material" : _currentMaterialKey;

	// Ask for new name
	QString suggestedName = QString("Copy of %1").arg(name);
	bool okName = false;
	QString enteredName = QInputDialog::getText(this,
		tr("Material Name"),
		tr("Enter name for new material:"),
		QLineEdit::Normal,
		suggestedName,
		&okName);
	if (!okName || enteredName.trimmed().isEmpty()) return;
	name = enteredName.trimmed();

	// Generate a unique UUID key for this material
	QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);

	// Try to derive group from currently selected tree item
	QList<QTreeWidgetItem*> selectedItems = libraryWidget ? libraryWidget->selectedItems() : QList<QTreeWidgetItem*>();
	if (!selectedItems.isEmpty())
	{
		QTreeWidgetItem* selectedItem = selectedItems.first();

		// Check if selected item is a category (has children) or material (no children)
		if (selectedItem->childCount() > 0)
		{
			// It's a category node - use it directly
			groupLabel = selectedItem->text(0);
			qDebug() << "Auto-detected group from selection:" << groupLabel;
		}
		else
		{
			// It's a material node - get its parent category
			QTreeWidgetItem* parentItem = selectedItem->parent();
			if (parentItem)
			{
				groupLabel = parentItem->text(0);
				qDebug() << "Auto-detected group from material parent:" << groupLabel;
			}
		}
	}

	// If group not detected, ask user
	if (groupLabel.isEmpty())
	{
		QStringList groups;
		const auto& sharedGroups = MaterialLibraryWidget::sharedGroups();
		for (const auto& g : sharedGroups) groups << g.first;
		if (groups.isEmpty()) groups << QStringLiteral("User Materials");

		bool ok = false;
		QString picked = QInputDialog::getItem(this,
			tr("Choose Group"),
			tr("Select a group to save into:"),
			groups,
			0,
			true,
			&ok);
		if (!ok) return;
		groupLabel = picked.trimmed().isEmpty() ? QStringLiteral("User Materials") : picked.trimmed();
	}

	// Create user material folder and copy texture files
	// (same logic as Save To Library to ensure consistency)
	QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
	QString materialFolder = QDir(userRoot).filePath(key);
	QDir materialDir(materialFolder);

	if (!materialDir.exists())
	{
		if (!materialDir.mkpath("."))
		{
			QMessageBox::warning(this, tr("Folder Creation Failed"),
				tr("Could not create material folder: %1").arg(materialFolder));
			return;
		}
	}

	// Copy texture files for all loaded textures in the material
	// CRITICAL: All user material textures must be self-contained in the material folder
	// with relative paths. Convert absolute paths to relative if they're in our folder.
	// If a texture cannot be copied, skip it (don't write invalid absolute paths to JSON)
	QString materialFolderCanonical = QFileInfo(materialFolder).canonicalFilePath();

	for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
	{
		Material::TextureType type = static_cast<Material::TextureType>(i);
		Material::Texture tex = mat.texture(type);

		if (tex.path.empty()) continue;

		QString sourcePath = QString::fromStdString(tex.path);
		QFileInfo fileInfo(sourcePath);
		QString fileName = fileInfo.fileName();
		QString destPath = materialDir.filePath(fileName);

		qDebug() << "Save As: Checking texture" << fileName << "source path:" << sourcePath;

		// Check if source is already in our material folder
		// This happens when overwriting a previously saved material
		QString sourceCanonical = QFileInfo(sourcePath).canonicalFilePath();
		QString destCanonical = QFileInfo(destPath).canonicalFilePath();

		if (sourceCanonical == destCanonical ||
			sourceCanonical.startsWith(materialFolderCanonical + QDir::separator()))
		{
			// File is already in our material folder, just update to relative path
			tex.path = fileName.toStdString();
			mat.setTexture(type, tex);
			qDebug() << "Save As: Texture already in folder, using relative:" << fileName;
			continue;
		}

		// Check if source file actually exists
		if (!QFile::exists(sourcePath))
		{
			// Texture file not found - skip it (don't write invalid path to JSON)
			qWarning() << "Texture file not found, skipping:" << sourcePath;
			tex.path = "";  // Clear path so it's not written to JSON
			mat.setTexture(type, tex);
			continue;
		}

		// Copy the texture file to material folder
		bool copySuccess = QFile::copy(sourcePath, destPath);
		if (!copySuccess)
		{
			// If destination already exists, it means another texture type already copied this file
			// (ORM case: same file assigned to O, R, M). Still set the path.
			if (QFile::exists(destPath))
			{
				tex.path = fileName.toStdString();
				mat.setTexture(type, tex);
				qDebug() << "SaveAs: Texture already in folder (ORM dedup), using existing:" << fileName;
				continue;
			}

			// Actual copy failure - file doesn't exist or permission error
			qWarning() << "Failed to copy texture file:" << fileName << "from" << sourcePath << "to" << destPath;
			tex.path = "";  // Clear path so it's not written to JSON
			mat.setTexture(type, tex);
			continue;
		}

		qDebug() << "Copied texture:" << fileName << "to" << destPath;

		// Success - update to relative path (just filename)
		tex.path = fileName.toStdString();
		mat.setTexture(type, tex);
	}

	// Block signals during save
	if (libraryWidget)
	{
		libraryWidget->blockSignals(true);
	}

	// Actually save the material to user library
	QString err;
	bool saved = MaterialLibraryWidget::saveUserMaterialToUserLocation(groupLabel, key, name, mat, this, &err);

	if (libraryWidget)
	{
		libraryWidget->blockSignals(false);
	}

	if (!saved)
	{
		if (!err.isEmpty() && err != QStringLiteral("User cancelled overwrite"))
		{
			QMessageBox::warning(this, tr("Save Material Failed"), err);
		}
		return;
	}

	// Mark key as user key
	MaterialLibraryWidget::s_userMaterialKeys.insert(key);

	// Update shared map with the saved material
	{
		Material matWithAbsolutePaths = mat;
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString materialFolder = QDir(userRoot).filePath(key);

		// Restore absolute paths from relative paths
		for (int i = 0; i < static_cast<int>(Material::TextureType::Count); ++i)
		{
			Material::TextureType type = static_cast<Material::TextureType>(i);
			Material::Texture tex = matWithAbsolutePaths.texture(type);

			if (tex.path.empty()) continue;

			QString texPath = QString::fromStdString(tex.path);
			if (!texPath.contains('/') && !texPath.contains('\\'))
			{
				QString absolutePath = QDir(materialFolder).filePath(texPath);
				tex.path = absolutePath.toStdString();
				matWithAbsolutePaths.setTexture(type, tex);
			}
		}

		auto& mutableSharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
			MaterialLibraryWidget::sharedMaterialMap());
		mutableSharedMap[key] = [material = matWithAbsolutePaths]() { return material; };
	}

	// Refresh the tree and select the newly saved material
	if (libraryWidget)
	{
		libraryWidget->blockSignals(true);
		// Refresh tree and select the newly saved material by key
		libraryWidget->refreshMaterialTree();

		// Re-add asterisks to all materials still in _unsavedMaterialKeys
		// (refreshMaterialTree loads from JSON which doesn't have asterisks)
		restoreAsterisksForUnsavedMaterials();

		libraryWidget->selectMaterialByKey(key);
		libraryWidget->blockSignals(false);

		// Load the newly saved material into preview
		if (libraryWidget->sharedMaterialMap().contains(key))
		{
			Material savedMat = libraryWidget->sharedMaterialMap()[key]();
			_material = new Material(savedMat);
			loadTextureImageFiles();
			bindMaterial(_material);
		}
	}

	// Update current material key
	_currentMaterialKey = key;
	_currentMaterialGroup = groupLabel;

	// Clear mesh editing state and remove old mesh material from tree if this was a mesh material
	if (!_editingMeshUuid.isNull())
	{
		// Remove the original mesh material from the shared groups and caches
		QString oldMeshKey = _currentMaterialKey;  // The old mesh material key
		if (oldMeshKey.startsWith("__mesh_"))
		{
			// Remove from unsaved keys
			_unsavedMaterialKeys.remove(oldMeshKey);

			// Remove from shared map
			auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
				MaterialLibraryWidget::sharedMaterialMap());
			sharedMap.remove(oldMeshKey);

			// Remove from shared groups (Mesh Materials category)
			auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
				MaterialLibraryWidget::sharedGroups());
			for (auto& groupPair : mutableGroups)
			{
				if (groupPair.first == "Mesh Materials")
				{
					// Find and remove the material with this key
					auto& materials = groupPair.second;
					for (int i = 0; i < materials.size(); ++i)
					{
						if (materials[i].second == oldMeshKey)
						{
							materials.removeAt(i);
							break;
						}
					}
					break;
				}
			}

			// Remove from local cache
			if (_materialCacheRef)
			{
				_materialCacheRef->remove(oldMeshKey);
			}

			qDebug() << "Cleaned up original mesh material:" << oldMeshKey;
		}
		_editingMeshUuid = QUuid();
	}

	QMessageBox::information(this, tr("Material Saved"),
		tr("New user material '%1' has been created.").arg(name));

	// Update refresh button state
	updateRefreshButtonState();
}

void MaterialPropertiesPanel::onRefreshSelectedMaterialFromLibrary()
{
	QString key = _currentMaterialKey;

	// Load fresh material from library
	const auto& sharedMap = MaterialLibraryWidget::sharedMaterialMap();
	if (sharedMap.contains(key))
	{
		Material freshMaterial = sharedMap[key]();
		*_material = freshMaterial;
		loadTextureImageFiles();
		bindMaterial(_material);

		qDebug() << "Refreshed material from library:" << key;
	}

	// Remove from unsaved set (marks as "clean")
	_unsavedMaterialKeys.remove(key);

	// Update tree to remove asterisk from material name
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
		if (!selected.isEmpty())
		{
			QTreeWidgetItem* item = selected.first();
			QString displayName = item->text(0);
			// Remove asterisk if present
			if (displayName.endsWith(" *"))
			{
				displayName = displayName.mid(0, displayName.length() - 2);
				item->setText(0, displayName);
				qDebug() << "Removed asterisk from material:" << displayName;
			}
		}
	}

	// Update button state
	updateRefreshButtonState();
}

void MaterialPropertiesPanel::updateRefreshButtonState()
{
	// Enable refresh button only if:
	// 1. Current material is a saved user material
	// 2. Current material has unsaved modifications
	bool canRefresh = !_currentMaterialKey.isEmpty() &&
		MaterialLibraryWidget::s_userMaterialKeys.contains(_currentMaterialKey) &&
		_unsavedMaterialKeys.contains(_currentMaterialKey);

	if (_ui->refreshTreeButton)
	{
		_ui->refreshTreeButton->setEnabled(canRefresh);
		qDebug() << "Refresh button state:" << (canRefresh ? "enabled" : "disabled") << "for material:" << _currentMaterialKey;
	}
}

void MaterialPropertiesPanel::onDeleteMaterial()
{
	// Get selected material from tree
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (!libraryWidget)
	{
		QMessageBox::warning(this, tr("No Library"), tr("Material library not available."));
		return;
	}

	QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
	if (selected.isEmpty())
	{
		QMessageBox::information(this, tr("No Selection"), tr("Please select a material to delete."));
		return;
	}

	// Get material key and group from selected item
	QString materialKey = selected.first()->data(0, Qt::UserRole).toString();
	QString materialName = selected.first()->text(0);

	if (materialKey.isEmpty())
	{
		QMessageBox::warning(this, tr("Invalid Selection"), tr("Could not determine material key."));
		return;
	}

	// Check if it's an unsaved, user material, or shipped material
	bool isUnsavedMaterial = _unsavedMaterialKeys.contains(materialKey);
	bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(materialKey);

	if (!isUserMaterial && !isUnsavedMaterial)
	{
		QMessageBox::information(this, tr("Cannot Delete Factory Material"),
			tr("Factory materials cannot be deleted. Only user-created or unsaved materials can be removed."));
		return;
	}

	// Show confirmation dialog
	QMessageBox::StandardButton reply =
		QMessageBox::question(this,
			tr("Delete Material?"),
			tr("Are you sure you want to remove '%1' from your library?\n\nThis cannot be undone.").arg(materialName),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);

	if (reply != QMessageBox::Yes)
	{
		return;
	}

	// Get the group from the selected item's parent
	QTreeWidgetItem* parentItem = selected.first()->parent();
	if (!parentItem)
	{
		QMessageBox::warning(this, tr("Invalid Material"), tr("Could not determine material group."));
		return;
	}
	QString groupLabel = parentItem->text(0);

	// FEATURE 1: Determine which material to select after deletion
	// Strategy: Select the material immediately above (previous sibling), or if it's first, select next
	QString nextMaterialKey;
	{
		int deletedRow = parentItem->indexOfChild(selected.first());
		int targetRow = (deletedRow > 0) ? deletedRow - 1 : deletedRow;

		// Check if there will be a sibling after deletion at the target row
		// After deletion, the item at targetRow will either be:
		// - The previous item (if we selected the previous row)
		// - The next item promoted upward (if we selected current row for a first item)
		if (targetRow >= 0 && targetRow < parentItem->childCount())
		{
			QTreeWidgetItem* targetItem = parentItem->child(targetRow);
			if (targetItem)
			{
				nextMaterialKey = targetItem->data(0, Qt::UserRole).toString();
				qDebug() << "Smart selection after deletion: will select" << nextMaterialKey << "at row" << targetRow;
			}
		}
	}

	// Block signals on tree widget to prevent unwanted signal handlers during deletion/refresh
	if (libraryWidget)
	{
		libraryWidget->blockSignals(true);
	}

	bool removed = false;

	// Handle unsaved materials differently - just remove from memory, not from JSON
	if (isUnsavedMaterial)
	{
		// Remove from s_materialMap
		auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
			MaterialLibraryWidget::sharedMaterialMap());
		sharedMap.remove(materialKey);

		// Remove from s_groups
		auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
			MaterialLibraryWidget::sharedGroups());
		for (auto& groupPair : mutableGroups)
		{
			if (groupPair.first == groupLabel)
			{
				for (int i = 0; i < groupPair.second.size(); ++i)
				{
					if (groupPair.second[i].second == materialKey)
					{
						groupPair.second.removeAt(i);
						break;
					}
				}
				break;
			}
		}

		// Remove from unsaved set
		_unsavedMaterialKeys.remove(materialKey);

		removed = true;
	}
	else
	{
		// Delete from JSON via MaterialLibraryWidget (for saved user materials)
		QString err;
		removed = MaterialLibraryWidget::removeUserMaterialFromUserLocation(groupLabel, materialKey, nullptr, &err);

		if (!removed)
		{
			QMessageBox::warning(this, tr("Delete Failed"),
				tr("Failed to delete material from library:\n%1").arg(err));
			if (libraryWidget)
			{
				libraryWidget->blockSignals(false);
			}
			return;
		}

		// Delete the material folder (texture files)
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString materialFolder = QDir(userRoot).filePath(materialKey);
		QDir materialDir(materialFolder);

		if (materialDir.exists())
		{
			if (!materialDir.removeRecursively())
			{
				QMessageBox::warning(this, tr("Folder Deletion Failed"),
					tr("Could not delete material folder. The material was removed from the library, but texture files remain at:\n%1").arg(materialFolder));
			}
		}
	}

	// Refresh tree while signals are blocked
	if (libraryWidget && removed)
	{
		libraryWidget->blockSignals(true);
		libraryWidget->refreshMaterialTree();

		// FEATURE 1: Smart selection - select the material we identified before deletion
		// This prevents jarring jumps to first item and keeps focus near deleted material
		if (!nextMaterialKey.isEmpty() && libraryWidget->sharedMaterialMap().contains(nextMaterialKey))
		{
			libraryWidget->selectMaterialByKey(nextMaterialKey);
			qDebug() << "Smart selection executed:" << nextMaterialKey;
		}
		else
		{
			// Fallback: if no next material found, deselect all
			for (auto item : libraryWidget->selectedItems())
				item->setSelected(false);
			_material = nullptr;
		}
		libraryWidget->blockSignals(false);
	}

	// Re-enable signals
	if (libraryWidget)
	{
		libraryWidget->blockSignals(false);
	}

	// Show success message
	QMessageBox::information(this, tr("Material Deleted"),
		tr("Material '%1' has been removed from your library.").arg(materialName));
}

void MaterialPropertiesPanel::onRenameMaterial()
{
	// Get selected material from tree
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (!libraryWidget)
	{
		QMessageBox::warning(this, tr("No Library"), tr("Material library not available."));
		return;
	}

	QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
	if (selected.isEmpty())
	{
		QMessageBox::information(this, tr("No Selection"), tr("Please select a material to rename."));
		return;
	}

	QString materialKey = selected.first()->data(0, Qt::UserRole).toString();
	if (materialKey.isEmpty())
	{
		QMessageBox::warning(this, tr("Invalid Selection"), tr("Could not determine material key."));
		return;
	}

	// Check material type
	bool isUnsavedMaterial = _unsavedMaterialKeys.contains(materialKey);
	bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(materialKey);

	// Verify it's either an unsaved or user material (not factory)
	if (!isUnsavedMaterial && !isUserMaterial)
	{
		QMessageBox::information(this, tr("Cannot Rename"),
			tr("Only user-created and unsaved materials can be renamed. Factory materials cannot be renamed."));
		return;
	}

	// Get current name and group
	QString displayName = selected.first()->text(0).trimmed();

	// Strip the " *" suffix for unsaved materials before showing in dialog
	QString currentName = displayName;
	if (currentName.endsWith(" *"))
	{
		currentName = currentName.mid(0, currentName.length() - 2);
	}

	QTreeWidgetItem* parentItem = selected.first()->parent();
	if (!parentItem)
	{
		QMessageBox::warning(this, tr("Invalid Material"), tr("Could not determine material group."));
		return;
	}
	QString groupLabel = parentItem->text(0);

	// Prompt for new name (without the " *" suffix)
	bool ok = false;
	QString newName = QInputDialog::getText(this,
		tr("Rename Material"),
		tr("Enter new name for material:"),
		QLineEdit::Normal,
		currentName,  // Clean name without " *"
		&ok);

	if (!ok || newName.trimmed().isEmpty())
	{
		return;
	}
	newName = newName.trimmed();

	// Prevent rename to same name
	if (newName == currentName)
	{
		return;  // Silent no-op - user cancelled or entered same name
	}

	// Validate no duplicate names in same group
	bool hasDuplicate = false;
	for (int i = 0; i < parentItem->childCount(); ++i)
	{
		QTreeWidgetItem* siblingItem = parentItem->child(i);
		if (siblingItem && siblingItem != selected.first())
		{
			QString siblingName = siblingItem->text(0).trimmed();
			if (siblingName == newName)
			{
				hasDuplicate = true;
				break;
			}
		}
	}

	if (hasDuplicate)
	{
		QMessageBox::warning(this, tr("Duplicate Name"),
			tr("A material with name '%1' already exists in this group.").arg(newName));
		return;
	}

	// Block signals during rename operation
	libraryWidget->blockSignals(true);

	// DIFFERENT PATHS: Unsaved vs Saved materials
	if (isUnsavedMaterial)
	{
		// For unsaved materials: Only update in-memory structures (no JSON persistence)
		// UUID key is never modified, only the display name
		qDebug() << "Renaming unsaved material:" << materialKey << "from" << currentName << "to" << newName;

		// Update the MDI-scoped cache with new name metadata
		if (_materialCacheRef)
		{
			auto it = _materialCacheRef->find(materialKey);
			if (it != _materialCacheRef->end())
			{
				it.value().name = newName;  // Update name only, UUID key unchanged
				qDebug() << "Updated cache entry for unsaved material:" << materialKey << "new name:" << newName;
			}
		}
	}
	else
	{
		// For saved user materials: Update JSON file
		// Construct path to user's personal materials.json (not the shipped catalog)
		QString userRoot = MaterialLibraryWidget::userMaterialsRootPath();
		QString userPath = QDir(userRoot).filePath(QStringLiteral("materials.json"));

		// Check file exists
		if (!QFile::exists(userPath))
		{
			libraryWidget->blockSignals(false);
			QMessageBox::warning(this, tr("File Not Found"),
				tr("User materials file does not exist: %1").arg(userPath));
			return;
		}

		// Load existing file in ReadOnly first
		QFile inFile(userPath);
		if (!inFile.open(QIODevice::ReadOnly))
		{
			libraryWidget->blockSignals(false);
			QMessageBox::warning(this, tr("Read Failed"),
				tr("Failed to open user materials file for reading: %1").arg(inFile.errorString()));
			return;
		}

		QByteArray fileData = inFile.readAll();
		inFile.close();

		QJsonParseError perr;
		QJsonDocument doc = QJsonDocument::fromJson(fileData, &perr);
		if (perr.error != QJsonParseError::NoError || !doc.isObject())
		{
			libraryWidget->blockSignals(false);
			QMessageBox::warning(this, tr("Parse Failed"),
				tr("Failed to parse user materials JSON: %1").arg(perr.errorString()));
			return;
		}

		// Convert to QVariantMap for easier manipulation (following existing pattern)
		QVariantMap rootVar = doc.toVariant().toMap();
		QVariantList groupsList = rootVar.value(QStringLiteral("groups")).toList();

		qDebug() << "Looking for material:" << materialKey << "in group:" << groupLabel;
		qDebug() << "Total groups in JSON:" << groupsList.size();

		bool renameSuccess = false;

		// Find and update the material in the appropriate group
		for (int g = 0; g < groupsList.size(); ++g)
		{
			QVariantMap groupObj = groupsList[g].toMap();
			QString jsonGroupLabel = groupObj.value(QStringLiteral("label")).toString();

			qDebug() << "  Group" << g << "label:" << jsonGroupLabel;

			if (jsonGroupLabel == groupLabel)
			{
				QVariantList itemsList = groupObj.value(QStringLiteral("items")).toList();
				qDebug() << "    Found matching group! Items count:" << itemsList.size();

				for (int i = 0; i < itemsList.size(); ++i)
				{
					QVariantMap itemMap = itemsList[i].toMap();
					QString jsonKey = itemMap.value(QStringLiteral("key")).toString();
					QString jsonName = itemMap.value(QStringLiteral("name")).toString();

					qDebug() << "      Item" << i << "key:" << jsonKey << "name:" << jsonName;

					if (jsonKey == materialKey)
					{
						qDebug() << "      Found matching material! Renaming from" << jsonName << "to" << newName;
						// Update the name field ONLY - key is never modified
						itemMap.insert(QStringLiteral("name"), newName);
						itemsList[i] = itemMap;
						renameSuccess = true;
						break;
					}
				}

				// Write back updated group
				if (renameSuccess)
				{
					groupObj.insert(QStringLiteral("items"), itemsList);
					groupsList[g] = groupObj;
					rootVar.insert(QStringLiteral("groups"), groupsList);
					qDebug() << "Updated group in JSON";
				}
				break;
			}
		}

		if (!renameSuccess)
		{
			libraryWidget->blockSignals(false);
			qDebug() << "ERROR: Could not find material" << materialKey << "in group" << groupLabel;
			qDebug() << "JSON file path:" << userPath;
			QMessageBox::warning(this, tr("Rename Failed"),
				tr("Material with key '%1' not found in group '%2'. Check the debug log for details.").arg(materialKey, groupLabel));
			return;
		}

		// Write updated JSON back to file
		// Use same approach as existing code: convert back to JSON and write
		QFile outFile(userPath);
		if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		{
			libraryWidget->blockSignals(false);
			QMessageBox::warning(this, tr("Write Failed"),
				tr("Failed to open user materials file for writing: %1").arg(outFile.errorString()));
			return;
		}

		QJsonDocument newDoc = QJsonDocument::fromVariant(rootVar);
		outFile.write(newDoc.toJson());
		outFile.close();

		qDebug() << "Successfully wrote updated materials JSON to:" << userPath;
	}

	// Update runtime cache (s_groups) - change the display name
	auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
		MaterialLibraryWidget::sharedGroups());

	for (auto& groupPair : mutableGroups)
	{
		if (groupPair.first == groupLabel)
		{
			for (int i = 0; i < groupPair.second.size(); ++i)
			{
				if (groupPair.second[i].second == materialKey)
				{
					// For unsaved materials, add " *" suffix back
					// For saved materials, use clean name
					QString displayName = newName;
					if (isUnsavedMaterial)
					{
						displayName = newName + " *";
					}
					groupPair.second[i].first = displayName;
					qDebug() << "Updated runtime cache for material:" << materialKey << "new display name:" << displayName;
					break;
				}
			}
			break;
		}
	}

	// Refresh tree to show new name
	libraryWidget->refreshMaterialTree();

	// Re-select the material by key to keep focus on it
	if (libraryWidget->sharedMaterialMap().contains(materialKey))
	{
		libraryWidget->selectMaterialByKey(materialKey);
	}

	// Re-enable signals
	libraryWidget->blockSignals(false);

	// Success message
	QMessageBox::information(this, tr("Material Renamed"),
		tr("Material has been renamed to '%1'.").arg(newName));

	qDebug() << "Material rename completed:" << materialKey << "->" << newName;
}

void MaterialPropertiesPanel::onCreateNewMaterial()
{
	if (!_material)
	{
		QMessageBox::warning(this, tr("No Material"), tr("No material is currently loaded to create from."));
		return;
	}

	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (!libraryWidget)
	{
		QMessageBox::warning(this, tr("No Library"), tr("Material library not available."));
		return;
	}

	// Get available groups from library
	QStringList groups;
	const auto& sharedGroups = MaterialLibraryWidget::sharedGroups();
	for (const auto& groupPair : sharedGroups)
	{
		groups << groupPair.first;
	}

	if (groups.isEmpty())
	{
		groups << QStringLiteral("User Materials");
	}

	// Ask user for material name
	bool okName = false;
	QString suggestedName = QStringLiteral("New Material");
	QString materialName = QInputDialog::getText(this,
		tr("New Material Name"),
		tr("Enter a name for the new material:"),
		QLineEdit::Normal,
		suggestedName,
		&okName);

	if (!okName || materialName.trimmed().isEmpty())
	{
		return;
	}
	materialName = materialName.trimmed();

	// Try to derive group from currently selected tree item
	QString selectedGroup;
	QList<QTreeWidgetItem*> selectedItems = libraryWidget->selectedItems();

	if (!selectedItems.isEmpty())
	{
		QTreeWidgetItem* selectedItem = selectedItems.first();

		// Check if selected item is a category (has children) or material (no children)
		if (selectedItem->childCount() > 0)
		{
			// It's a category node - use it directly
			selectedGroup = selectedItem->text(0);
			qDebug() << "Auto-detected category from selection:" << selectedGroup;
		}
		else
		{
			// It's a material node - get its parent category
			QTreeWidgetItem* parentItem = selectedItem->parent();
			if (parentItem)
			{
				selectedGroup = parentItem->text(0);
				qDebug() << "Auto-detected category from material parent:" << selectedGroup;
			}
		}
	}

	// If category not detected, ask user
	if (selectedGroup.isEmpty())
	{
		bool okGroup = false;
		selectedGroup = QInputDialog::getItem(this,
			tr("Choose Category"),
			tr("Select a category for this material:"),
			groups,
			0,
			false,  // not editable
			&okGroup);

		if (!okGroup || selectedGroup.isEmpty())
		{
			return;
		}
	}

	// Generate a unique UUID key for this material
	QString materialKey = QUuid::createUuid().toString(QUuid::WithoutBraces);

	// Copy current material - this copies all scalars and texture paths
	Material newMaterial = *_material;

	// Ensure ADS values are recalculated from PBR for in-session consistency
	// (copy constructor doesn't call updateConsistency, so ADS may be stale)
	newMaterial.updateConsistency();

	// Make sure we have the full material with all properties
	qDebug() << "Creating new material from:" << materialName
		<< "Albedo:" << newMaterial.albedoMapPath()
		<< "Normal:" << newMaterial.normalMapPath()
		<< "Metallic:" << newMaterial.metallicMapPath();

	// Add to the shared material map in memory
	// For unsaved materials, capture the CREATING MDI instance so other MDIs can query the live cache
	// This enables cross-MDI visibility with real-time synchronization of modifications
	auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
		MaterialLibraryWidget::sharedMaterialMap());

	// Capture the creating MDI (_modelViewer) and material key
	// The lambda queries the live cache, not a static copy
	// This ensures other MDIs see modifications in real-time
	sharedMap[materialKey] = [this, materialKey]() -> Material {
		// Query the live cache from the creating MDI
		if (_modelViewer && _materialCacheRef)
		{
			auto it = _materialCacheRef->find(materialKey);
			if (it != _materialCacheRef->end())
			{
				return it.value().material;  // Return current cached version (with all modifications)
			}
		}
		// Fallback: return a default material if cache lookup fails
		return Material();
		};

	// Add to the appropriate group in s_groups
	auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
		MaterialLibraryWidget::sharedGroups());

	for (auto& groupPair : mutableGroups)
	{
		if (groupPair.first == selectedGroup)
		{
			// Add material to this group: (displayName with asterisk, key)
			// Note: s_groups structure is QPair<displayName, key>
			groupPair.second.append(qMakePair(materialName + " *", materialKey));
			break;
		}
	}

	// Mark as unsaved
	_unsavedMaterialKeys.insert(materialKey);
	_currentMaterialKey = materialKey;

	// Store the group so we can use it when saving
	_currentMaterialGroup = selectedGroup;

	// Add to the MDI-scoped cache with name and group metadata
	if (_materialCacheRef)
	{
		(*_materialCacheRef)[materialKey] = CachedMaterial{
			newMaterial,
			materialName,
			selectedGroup
		};
		qDebug() << "Added to cache - key:" << materialKey << "albedo:" << newMaterial.albedoColor();
	}
	else
	{
		qWarning() << "ERROR: _materialCacheRef is null! Material not added to cache!";
	}


	qDebug() << "Created new unsaved material:" << materialKey << "name:" << materialName << "group:" << selectedGroup;

	// Register this material as owned by the current MDI for proper cleanup
	if (_modelViewer)
	{
		_modelViewer->registerOwnedUnsavedMaterial(materialKey);
	}

	// Refresh tree to show the new material
	libraryWidget->blockSignals(true);
	libraryWidget->refreshMaterialTree();

	// Select the newly created material WITHOUT triggering materialApplied signal
	// (which would try to apply to a mesh that might not exist)
	libraryWidget->selectMaterialByKey(materialKey);
	libraryWidget->blockSignals(false);

	// CRITICAL: Load the new material into _material for editing
	// Since signals are blocked above, onMaterialPresetSelected() is NOT called,
	// so we must manually load the material and update the UI
	if (!_material) _material = new Material();
	*_material = newMaterial;
	_material->updateConsistency();
	qDebug() << "Loaded new material into _material - albedo:" << _material->albedoColor();
	bindMaterial(_material);

	// Update refresh button state (newly created materials should not show refresh button)
	updateRefreshButtonState();

	QMessageBox::information(this, tr("Material Created"),
		tr("New material '%1' created in category '%2'.\n\nModify it and then click 'Save' to persist it to your library.").arg(materialName, selectedGroup));
}

void MaterialPropertiesPanel::connectMaterialLibrarySignals()
{
	// Connect tree widget material selection (SINGLE-CLICK PREVIEW AND DOUBLE-CLICK CONFIRM)
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		// Single-click: update preview immediately (for editing)
		connect(libraryWidget, &MaterialLibraryWidget::materialPreview,
			this, [this](const Material& mat) { onMaterialPresetSelected(mat); });

		// Double-click: apply material to selected mesh in main viewer
		connect(libraryWidget, &MaterialLibraryWidget::materialSelected,
			this, [this](const Material& mat) { onMaterialDoubleClicked(mat); });
	}
}

void MaterialPropertiesPanel::updateUnsavedMaterialInMap()
{
	if (!_material || _currentMaterialKey.isEmpty()) return;

	if (!_material || _currentMaterialKey.isEmpty() || !_materialCacheRef) return;

	// CRITICAL: Ensure ADS values are recalculated before caching
	_material->updateConsistency();

	// Store the current material in MDI-scoped cache for persistence across material selections
	// This preserves both scalar and texture modifications when switching between materials
	// IMPORTANT: Preserve the original name and group metadata from cache (they don't change)
	auto cachedIt = _materialCacheRef->find(_currentMaterialKey);
	if (cachedIt != _materialCacheRef->end())
	{
		// Material already in cache - preserve its original metadata
		(*_materialCacheRef)[_currentMaterialKey] = CachedMaterial{
			*_material,
			cachedIt.value().name,      // Keep original name
			cachedIt.value().group      // Keep original group
		};
	}
	else
	{
		// Material not yet in cache - this happens when user selects a material and modifies it
		// Try to get the display name from the tree
		QString displayName = _currentMaterialKey;  // Default fallback
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
			if (!selected.isEmpty())
			{
				// Get the material name from the tree item (without asterisk if present)
				displayName = selected.first()->text(0);
				if (displayName.endsWith(" *"))
				{
					displayName.chop(2);  // Remove the " *" suffix
				}
			}
		}

		(*_materialCacheRef)[_currentMaterialKey] = CachedMaterial{
			*_material,
			displayName,                // Use actual name from tree, not the key
			_currentMaterialGroup
		};
	}
}

void MaterialPropertiesPanel::markMaterialAsModified()
{
	if (_currentMaterialKey.isEmpty()) return;

	// Factory/shipped materials should never be marked as they are read-only
	bool isAlreadyUnsaved = _unsavedMaterialKeys.contains(_currentMaterialKey);
	bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(_currentMaterialKey);
	bool isFactoryMaterial = !isAlreadyUnsaved && !isUserMaterial;

	// Only mark non-factory materials as modified
	if (!isFactoryMaterial && !isAlreadyUnsaved)
	{
		_unsavedMaterialKeys.insert(_currentMaterialKey);

		// Update tree display to show asterisk for modified saved material WITHOUT refreshing tree
		// (refreshing the tree would reset the selection to the first material)
		MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
		if (libraryWidget)
		{
			// Find the tree item for this material and update its display name directly
			QTreeWidget* tree = libraryWidget;
			for (int g = 0; g < tree->topLevelItemCount(); ++g)
			{
				QTreeWidgetItem* groupItem = tree->topLevelItem(g);
				if (!groupItem) continue;

				for (int m = 0; m < groupItem->childCount(); ++m)
				{
					QTreeWidgetItem* matItem = groupItem->child(m);
					if (!matItem) continue;

					QString itemKey = matItem->data(0, Qt::UserRole).toString();
					if (itemKey == _currentMaterialKey)
					{
						// Update display name with asterisk (avoid duplicate asterisks)
						QString displayName = matItem->text(0);
						if (!displayName.endsWith(" *"))
						{
							matItem->setText(0, displayName + " *");
						}
						// Update refresh button state now that material is marked as modified
						updateRefreshButtonState();
						return;  // Found and updated, exit
					}
				}
			}
		}
	}
}

void MaterialPropertiesPanel::restoreAsterisksForUnsavedMaterials()
{
	// After tree refresh, re-add asterisks to all materials still in _unsavedMaterialKeys
	// This is needed because refreshMaterialTree() loads from JSON which doesn't have asterisks
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (!libraryWidget) return;

	QTreeWidget* tree = libraryWidget;
	for (int g = 0; g < tree->topLevelItemCount(); ++g)
	{
		QTreeWidgetItem* groupItem = tree->topLevelItem(g);
		if (!groupItem) continue;

		for (int m = 0; m < groupItem->childCount(); ++m)
		{
			QTreeWidgetItem* matItem = groupItem->child(m);
			if (!matItem) continue;

			QString itemKey = matItem->data(0, Qt::UserRole).toString();
			if (_unsavedMaterialKeys.contains(itemKey))
			{
				// This material is unsaved, add asterisk if not already present
				QString displayName = matItem->text(0);
				if (!displayName.endsWith(" *"))
				{
					matItem->setText(0, displayName + " *");
					qDebug() << "Restored asterisk for unsaved material:" << displayName;
				}
			}
		}
	}
}

void MaterialPropertiesPanel::onContextMenu(const QPoint& pos)
{
	QMenu menu;

	// Check if context menu was requested from the tree widget
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		QList<QTreeWidgetItem*> selected = libraryWidget->selectedItems();
		if (!selected.isEmpty())
		{
			QString materialName = selected.first()->text(0);
			QString materialKey = selected.first()->data(0, Qt::UserRole).toString();

			// Add tree-specific menu items
			menu.addAction(tr("Copy Name"), this, [materialName]() {
				QApplication::clipboard()->setText(materialName);
				});

			menu.addAction(tr("Copy Key"), this, [materialKey]() {
				QApplication::clipboard()->setText(materialKey);
				});

			menu.addSeparator();

			// Check material type
			bool isUnsavedMaterial = _unsavedMaterialKeys.contains(materialKey);
			bool isUserMaterial = MaterialLibraryWidget::s_userMaterialKeys.contains(materialKey);

			// Allow rename for user materials and unsaved materials (not factory)
			if (isUserMaterial || isUnsavedMaterial)
			{
				menu.addAction(tr("Rename"), this, &MaterialPropertiesPanel::onRenameMaterial);
				menu.addSeparator();
			}

			// Allow deletion if it's a user or unsaved material
			if (isUserMaterial || isUnsavedMaterial)
			{
				menu.addAction(tr("Delete"), this, &MaterialPropertiesPanel::onDeleteMaterial);
			}

			menu.addSeparator();
		}
	}

	// Add global panel option
	menu.addAction(tr("Clear All Textures"), this, &MaterialPropertiesPanel::onClearAllTextures);
	menu.exec(mapToGlobal(pos));
}

bool MaterialPropertiesPanel::eventFilter(QObject* obj, QEvent* ev)
{
	if (ev->type() == QEvent::Drop)
	{
		QDropEvent* dropEv = static_cast<QDropEvent*>(ev);
		const QMimeData* mime = dropEv->mimeData();

		if (mime->hasUrls())
		{
			QList<QUrl> urls = mime->urls();
			if (!urls.isEmpty())
			{
				QString filePath = urls.first().toLocalFile();

				for (auto it = _textureSlots.begin(); it != _textureSlots.end(); ++it)
				{
					if (it.value().button == obj)
					{
						setTextureMapPath(it.key(), filePath);
						dropEv->acceptProposedAction();
						return true;
					}
				}
			}
		}
	}
	else if (ev->type() == QEvent::DragEnter)
	{
		QDragEnterEvent* dragEv = static_cast<QDragEnterEvent*>(ev);
		if (dragEv->mimeData()->hasUrls())
		{
			dragEv->acceptProposedAction();
			return true;
		}
	}

	return QWidget::eventFilter(obj, ev);
}

void MaterialPropertiesPanel::onSearchTextChanged(const QString& text)
{
	// Get the tree widget
	QTreeWidget* tw = qobject_cast<QTreeWidget*>(_ui->treeWidget);
	if (!tw) return;

	const QString needle = text.trimmed().toLower();
	const bool hasFilter = !needle.isEmpty();
	QStringList tokens;
	if (hasFilter) tokens = needle.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
	int matchCount = 0;

	// Recursively walk and update each item
	std::function<bool(QTreeWidgetItem*, bool)> matchAndShow = [&](QTreeWidgetItem* item, bool parentMatched) -> bool {
		const QString label = item->text(0).toLower();

		// Only consider self-match when a filter is active
		bool selfMatched = false;
		if (hasFilter)
		{
			selfMatched = true;
			for (const QString& t : tokens)
			{
				if (!label.contains(t)) { selfMatched = false; break; }
			}
		}

		// Always recurse children so we can reset their fonts/foreground when clearing
		// Pass down whether this item or any ancestor matched
		bool anyChildMatched = false;
		bool shouldShowChildren = parentMatched || selfMatched;
		for (int i = 0; i < item->childCount(); ++i)
		{
			if (matchAndShow(item->child(i), shouldShowChildren)) anyChildMatched = true;
		}

		// Item should be shown if:
		// 1. No filter is active (show all), OR
		// 2. This item matched the search, OR
		// 3. Any child matched the search, OR
		// 4. A parent/ancestor matched the search
		const bool matched = hasFilter ? (selfMatched || anyChildMatched || parentMatched) : true;
		item->setHidden(hasFilter ? !matched : false);

		// Expand only when filtering and the item matched
		if (hasFilter && matched) item->setExpanded(true);

		// Foreground color: use subtle blue when self matched; reset otherwise
		if (hasFilter && selfMatched)
		{
			item->setForeground(0, QBrush(QColor(0x1e88e5)));
		}
		else
		{
			item->setForeground(0, QBrush());  // reset to default
		}

		if (matched) ++matchCount;
		return matched;
		};

	// Start recursion from top-level items with parentMatched = false
	for (int i = 0; i < tw->topLevelItemCount(); ++i)
		matchAndShow(tw->topLevelItem(i), false);

	// Red border when no matches and query non-empty; reset otherwise
	if (!hasFilter)
	{
		_ui->searchEdit->setStyleSheet("");
		_ui->searchEdit->setToolTip("");
	}
	else if (matchCount == 0)
	{
		_ui->searchEdit->setStyleSheet("QLineEdit { border: 1px solid #e53935; }");
		_ui->searchEdit->setToolTip("No matches");
	}
	else
	{
		_ui->searchEdit->setStyleSheet("");
		_ui->searchEdit->setToolTip("");
	}
}

void MaterialPropertiesPanel::beginSaveUnsavedMaterials()
{
	// Block signals on the tree widget to prevent cascading signal events
	// during batch save operations (e.g., when closing MDI with unsaved materials)
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		libraryWidget->blockSignals(true);
		qDebug() << "Blocked signals for batch unsaved material save";
	}
}

void MaterialPropertiesPanel::endSaveUnsavedMaterials()
{
	// Refresh tree WHILE signals are still blocked, then unblock
	// This prevents selection signals from firing during the refresh
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (libraryWidget)
	{
		libraryWidget->refreshMaterialTree();
		libraryWidget->blockSignals(false);
	}
}

void MaterialPropertiesPanel::createUnsavedMaterialFromMesh(
	const QString& meshName,
	const Material& meshMaterial)
{
	MaterialLibraryWidget* libraryWidget = qobject_cast<MaterialLibraryWidget*>(_ui->treeWidget);
	if (!libraryWidget)
	{
		qWarning() << "Material library widget not found!";
		return;
	}

	// Generate unique key with "__mesh_" prefix for identification
	QString materialKey = "__mesh_" + QUuid::createUuid().toString(QUuid::WithoutBraces);

	// Copy the mesh material and ensure consistency
	Material newMaterial = meshMaterial;
	newMaterial.updateConsistency();

	qDebug() << "Creating unsaved material from mesh:" << meshName
		<< "Key:" << materialKey;

	// ========== Add to shared material map (s_materialMap) ==========
	auto& sharedMap = const_cast<QMap<QString, std::function<Material()>>&>(
		MaterialLibraryWidget::sharedMaterialMap());

	sharedMap[materialKey] = [this, materialKey]() -> Material {
		if (_modelViewer && _materialCacheRef)
		{
			auto it = _materialCacheRef->find(materialKey);
			if (it != _materialCacheRef->end())
			{
				return it.value().material;
			}
		}
		return Material();
		};

	// ========== Add to shared groups (s_groups) ==========
	auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
		MaterialLibraryWidget::sharedGroups());

	QString groupLabel = QStringLiteral("Mesh Materials");
	bool foundGroup = false;

	for (auto& groupPair : mutableGroups)
	{
		if (groupPair.first == groupLabel)
		{
			// Add material with " *" suffix (unsaved marker)
			groupPair.second.append(qMakePair(QString("[%1] *").arg(meshName), materialKey));
			foundGroup = true;
			qDebug() << "Added to existing 'Mesh Materials' group";
			break;
		}
	}

	// Create "Mesh Materials" group if it doesn't exist
	if (!foundGroup)
	{
		QVector<QPair<QString, QString>> newGroup;
		newGroup.append(qMakePair(QString("[%1] *").arg(meshName), materialKey));
		mutableGroups.append(qMakePair(groupLabel, newGroup));
		qDebug() << "Created new 'Mesh Materials' group";
	}

	// ========== Mark as unsaved ==========
	_unsavedMaterialKeys.insert(materialKey);
	_currentMaterialKey = materialKey;
	_currentMaterialGroup = groupLabel;

	// ========== Add to local MDI cache ==========
	if (_materialCacheRef)
	{
		(*_materialCacheRef)[materialKey] = CachedMaterial{
			newMaterial,
			QString("[%1]").arg(meshName),
			groupLabel
		};
		qDebug() << "Added to MDI cache - key:" << materialKey;
	}
	else
	{
		qWarning() << "ERROR: _materialCacheRef is null!";
		return;
	}

	// ========== Register with ModelViewer for cleanup ==========
	if (_modelViewer)
	{
		_modelViewer->registerOwnedUnsavedMaterial(materialKey);
		qDebug() << "Registered with ModelViewer for cleanup";
	}

	// ========== Refresh tree and select the new material ==========
	libraryWidget->blockSignals(true);
	libraryWidget->refreshMaterialTree();
	libraryWidget->selectMaterialByKey(materialKey);
	libraryWidget->blockSignals(false);

	// ========== Load material into panel ==========
	if (!_material) _material = new Material();
	*_material = newMaterial;
	_material->updateConsistency();
	bindMaterial(_material);

	updateRefreshButtonState();

	qDebug() << "Mesh material created and loaded into panel";
}

void MaterialPropertiesPanel::setEditingMeshUuid(const QUuid& uuid)
{
	_editingMeshUuid = uuid;
}

void MaterialPropertiesPanel::removeEmptyMeshMaterialsCategory()
{
	// Remove the "Mesh Materials" category if it's empty (no materials left in it)
	auto& mutableGroups = const_cast<QVector<QPair<QString, QVector<QPair<QString, QString>>>>&>(
		MaterialLibraryWidget::sharedGroups());

	for (int i = 0; i < mutableGroups.size(); ++i)
	{
		if (mutableGroups[i].first == "Mesh Materials")
		{
			// Check if this category is empty
			if (mutableGroups[i].second.isEmpty())
			{
				mutableGroups.removeAt(i);
				qDebug() << "Removed empty 'Mesh Materials' category";
			}
			else
			{
				qDebug() << "Mesh Materials category still has" << mutableGroups[i].second.size() << "material(s)";
			}
			break;
		}
	}
}
