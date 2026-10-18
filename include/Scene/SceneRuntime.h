#pragma once

#include "BoundingBox.h"
#include "SceneMeshRecord.h"
#include "SceneMesh.h"
#include "TransformCommand.h"

#include <algorithm>
#include <functional>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QUuid>
#include <QVector>
#include <exception>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <assimp/scene.h>

struct SceneNode;

// ---------------------------------------------------------------------------
// TextureSamplerSettings
// ---------------------------------------------------------------------------
struct TextureSamplerSettings
{
	GLenum wrapS      = GL_REPEAT;
	GLenum wrapT      = GL_REPEAT;
	GLenum minFilter  = GL_LINEAR_MIPMAP_LINEAR;
	GLenum magFilter  = GL_LINEAR;

	bool operator==(const TextureSamplerSettings& other) const
	{
		return wrapS      == other.wrapS   &&
		       wrapT      == other.wrapT   &&
		       minFilter  == other.minFilter &&
		       magFilter  == other.magFilter;
	}
};

// ---------------------------------------------------------------------------
// CachedTextureEntry
// ---------------------------------------------------------------------------
struct CachedTextureEntry
{
	QImage  image;
	int     imageWidth      = 0;
	int     imageHeight     = 0;
	int     imageComponents = 0;
	GLenum  imageFormat     = GL_RGBA;

	GLuint                lastGPUTexture   = 0;
	TextureSamplerSettings lastSamplerSettings;
	int                   refCount         = 0;
};

// ---------------------------------------------------------------------------
// RuntimeVisibilityNode
// ---------------------------------------------------------------------------
struct RuntimeVisibilityNode
{
	const SceneNode* sceneNode = nullptr;
	QVector<int>     children;
	QVector<int>     meshIndices;
	BoundingBox      subtreeBounds;
	bool             subtreeHasVisibleMeshes = false;
};

// ---------------------------------------------------------------------------
// SceneRuntime
//
// Owns all scene-data state: mesh store, texture cache, runtime visibility
// BVH, the Assimp scene tree, and load-lifecycle flags.
// All fields are private; ViewportWidget accesses them through the typed API below.
// ---------------------------------------------------------------------------
class SceneRuntime
{
public:
	// RecycleBinEntry is part of the public interface (used in method signatures).
	struct RecycleBinEntry
	{
		SceneMesh* mesh;
		int           originalIndex;
		QDateTime     deletedAt;
	};

	// ---- Mesh store --------------------------------------------------------
	std::vector<SceneMeshRecord>&       meshStore()            { return _meshStore; }
	const std::vector<SceneMeshRecord>& meshStore()      const { return _meshStore; }
	SceneMeshRecord&       meshRecordAt(size_t index)         { return _meshStore.at(index); }
	const SceneMeshRecord& meshRecordAt(size_t index)   const { return _meshStore.at(index); }
	SceneMesh*             meshAt(size_t index)               { return _meshStore.at(index).mesh; }
	const SceneMesh*       meshAt(size_t index)         const { return _meshStore.at(index).mesh; }

	std::vector<int>&       displayedObjectsIds()              { return _displayedObjectsIds; }
	const std::vector<int>& displayedObjectsIds()        const { return _displayedObjectsIds; }

	std::vector<int>&       hiddenObjectsIds()                 { return _hiddenObjectsIds; }
	const std::vector<int>& hiddenObjectsIds()           const { return _hiddenObjectsIds; }

	const std::vector<int>& currentVisibleObjectIds() const
	{
		return _visibleSwapped ? _hiddenObjectsIds : _displayedObjectsIds;
	}

	// ---- Recycle bin -------------------------------------------------------
	QMap<QUuid, RecycleBinEntry>&       recycleBin()           { return _recycleBin; }
	const QMap<QUuid, RecycleBinEntry>& recycleBin()     const { return _recycleBin; }

	// ---- Texture cache -----------------------------------------------------
	std::unordered_map<QString, CachedTextureEntry>&       texCache()       { return _texCache; }
	const std::unordered_map<QString, CachedTextureEntry>& texCache() const { return _texCache; }

	std::unordered_map<unsigned int, int>&       texRefCount()       { return _texRefCount; }
	const std::unordered_map<unsigned int, int>& texRefCount() const { return _texRefCount; }

	// ---- Runtime visibility BVH --------------------------------------------
	QVector<RuntimeVisibilityNode>&       runtimeVisibilityNodes()       { return _runtimeVisibilityNodes; }
	const QVector<RuntimeVisibilityNode>& runtimeVisibilityNodes() const { return _runtimeVisibilityNodes; }

	int     runtimeVisibilityRootIndex()              const { return _runtimeVisibilityRootIndex; }
	bool    runtimeVisibilityPrepared()               const { return _runtimeVisibilityPrepared; }

	std::vector<unsigned char>&       runtimeBaseVisibleMask()       { return _runtimeBaseVisibleMask; }
	const std::vector<unsigned char>& runtimeBaseVisibleMask() const { return _runtimeBaseVisibleMask; }
	void invalidateRuntimeVisibilityHierarchy();
	void rebuildRuntimeVisibilityHierarchy(const SceneNode* root);
	bool ensureRuntimeVisibilityHierarchy(const SceneNode* root);
	void refreshRuntimeVisibilityCacheForCurrentView(
		const SceneNode* root,
		quint64 currentBoundsRevision,
		const std::function<bool(const SceneMesh*)>& isMeshVisibleFn);

	// ---- Assimp scene ------------------------------------------------------
	const aiScene*  assimpScene()                   const { return _assimpScene; }
	void            setAssimpScene(const aiScene* s)      { _assimpScene = s; }

	aiScene*        globalScene()                   const { return _globalScene; }
	aiScene*&       globalScene()                         { return _globalScene; }
	void            setGlobalScene(aiScene* s)            { _globalScene = s; }

	glm::mat4&      globalSceneTransform()                { return _globalSceneTransform; }
	const glm::mat4& globalSceneTransform()         const { return _globalSceneTransform; }

	// ---- Load lifecycle ----------------------------------------------------
	QList<QUuid>&       pendingSceneUuids()         { return _pendingSceneUuids; }
	const QList<QUuid>& pendingSceneUuids()   const { return _pendingSceneUuids; }

	std::vector<int>&       centerScreenObjectIDs()         { return _centerScreenObjectIDs; }
	const std::vector<int>& centerScreenObjectIDs()   const { return _centerScreenObjectIDs; }

	bool  visibleSwapped()                    const { return _visibleSwapped; }
	bool& visibleSwapped()                          { return _visibleSwapped; }
	void  setVisibleSwapped(bool v)                 { _visibleSwapped = v; }
	bool  swapVisible(bool checked);

	bool progressiveLoadingEnabled()          const { return _progressiveLoadingEnabled; }
	void setProgressiveLoadingEnabled(bool v)       { _progressiveLoadingEnabled = v; }

	bool cancelRequested()                    const { return _cancelRequested; }
	void setCancelRequested(bool v)                 { _cancelRequested = v; }

	bool loadCancelled()                      const { return _loadCancelled; }
	void setLoadCancelled(bool v)                   { _loadCancelled = v; }

	// ---- Mesh/material batch helpers --------------------------------------
	int        addMeshToDisplay(SceneMesh* mesh);
	SceneMesh* detachMeshAt(int index);
	int        restoreDetachedMesh(SceneMesh* mesh, int originalIndex);
	bool       clearMeshStore();
	bool       setDisplayList(const std::vector<int>& ids);

	bool moveToRecycleBin(const QUuid& uuid, int originalIndex);
	bool restoreFromRecycleBin(const QUuid& uuid);
	bool permanentlyDeleteFromRecycleBin(const QUuid& uuid);
	bool           isInRecycleBin(const QUuid& uuid) const  { return _recycleBin.contains(uuid); }
	QVector<QUuid> recycleBinUuids()                 const  { return _recycleBin.keys().toVector(); }

	std::vector<SceneMesh*> meshPointers()                                            const;
	SceneMesh* getMeshByUuid(const QUuid& uuid)                                       const;
	SceneMesh* getMeshByIndex(int index)                                              const;
	int        getIndexByUuid(const QUuid& uuid)                                      const;
	QUuid      getUuidByIndex(int index)                                              const;

	bool applyMaterialToMeshes(const std::vector<int>& ids, const Material& mat);
	void applyTextureMapsToMesh(int id, const Material& resolved);
	void invertAdsOpacityMaps(const std::vector<int>& ids, bool inverted);
	std::vector<unsigned int> drainTextureCacheGpuIds();

	void setMeshTransforms(const std::vector<int>& ids,
		const QVector3D& translation,
		const QVector3D& rotation,
		const QVector3D& scale);
	void resetMeshTransforms(const std::vector<int>& ids);
	void applyMeshTransforms(const QMap<int, TransformState>& transforms);

	bool isModelLevelTransform(int transformCount) const
	    { return transformCount == static_cast<int>(_meshStore.size()); }

	bool    userModelTransformForFile(const QString& sourceFile, QMatrix4x4& outTransform) const;
	QString generateUniqueMeshName(const QString& baseName) const;

private:
	// ---- Mesh store ----
	std::vector<SceneMeshRecord> _meshStore;
	std::vector<int>             _displayedObjectsIds;
	std::vector<int>             _hiddenObjectsIds;

	// ---- Recycle bin ----
	QMap<QUuid, RecycleBinEntry> _recycleBin;

	// ---- Texture cache ----
	std::unordered_map<QString, CachedTextureEntry> _texCache;
	std::unordered_map<unsigned int, int>           _texRefCount;

	// ---- Runtime visibility BVH ----
	QVector<RuntimeVisibilityNode> _runtimeVisibilityNodes;
	int     _runtimeVisibilityRootIndex              = -1;
	bool    _runtimeVisibilityHierarchyDirty         = true;
	int     _runtimeVisibilityMeshStoreCount         = -1;
	bool    _runtimeVisibilityPrepared               = false;
	quint64 _runtimeVisibilityBoundsRevision         = 0;
	quint64 _runtimeVisibilityMaskRevision           = 1;
	quint64 _runtimeVisibilityMaskProcessedRevision  = 0;
	std::vector<unsigned char> _runtimeBaseVisibleMask;

	// ---- Assimp scene ----
	const aiScene* _assimpScene         = nullptr;
	aiScene*       _globalScene         = nullptr;
	glm::mat4      _globalSceneTransform{ 1.0f };

	// ---- Load lifecycle ----
	QList<QUuid>     _pendingSceneUuids;
	std::vector<int> _centerScreenObjectIDs;
	bool _visibleSwapped             = false;
	bool _progressiveLoadingEnabled  = false;
	bool _cancelRequested            = false;
	bool _loadCancelled              = false;

	int buildRuntimeVisibilityNodeRecursive(const SceneNode* node,
	                                        const QHash<QUuid, int>& meshIndexByUuid);
	bool refreshRuntimeVisibilityNodeBounds(int nodeIndex,
	                                        const std::vector<unsigned char>& baseVisibleMask,
	                                        bool refreshBounds,
	                                        const std::function<bool(const SceneMesh*)>& isMeshVisibleFn);
};
