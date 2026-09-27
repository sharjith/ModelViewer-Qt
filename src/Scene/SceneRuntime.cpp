#include "SceneRuntime.h"

#include "SceneNode.h"

#include <algorithm>
#include <iterator>

void SceneRuntime::invalidateRuntimeVisibilityHierarchy()
{
	_runtimeVisibilityHierarchyDirty = true;
	_runtimeVisibilityPrepared = false;
	_runtimeVisibilityRootIndex = -1;
	_runtimeVisibilityMeshStoreCount = static_cast<int>(_meshStore.size());
	_runtimeVisibilityBoundsRevision = 0;
	_runtimeVisibilityNodes.clear();
	_runtimeBaseVisibleMask.clear();
}

int SceneRuntime::buildRuntimeVisibilityNodeRecursive(const SceneNode* node,
                                                      const QHash<QUuid, int>& meshIndexByUuid)
{
	if (!node)
		return -1;

	const int nodeIndex = _runtimeVisibilityNodes.size();
	_runtimeVisibilityNodes.push_back(RuntimeVisibilityNode{});
	_runtimeVisibilityNodes[nodeIndex].sceneNode = node;
	_runtimeVisibilityNodes[nodeIndex].meshIndices.reserve(node->meshUuids.size());

	for (const QUuid& uuid : node->meshUuids)
	{
		const auto it = meshIndexByUuid.find(uuid);
		if (it != meshIndexByUuid.end())
			_runtimeVisibilityNodes[nodeIndex].meshIndices.push_back(it.value());
	}

	_runtimeVisibilityNodes[nodeIndex].children.reserve(node->children.size());
	for (const SceneNode* child : node->children)
	{
		const int childIndex = buildRuntimeVisibilityNodeRecursive(child, meshIndexByUuid);
		if (childIndex >= 0)
			_runtimeVisibilityNodes[nodeIndex].children.push_back(childIndex);
	}

	return nodeIndex;
}

void SceneRuntime::rebuildRuntimeVisibilityHierarchy(const SceneNode* root)
{
	_runtimeVisibilityNodes.clear();
	_runtimeVisibilityRootIndex = -1;
	_runtimeVisibilityPrepared = false;

	if (!root)
	{
		_runtimeVisibilityHierarchyDirty = false;
		_runtimeVisibilityMeshStoreCount = static_cast<int>(_meshStore.size());
		_runtimeVisibilityBoundsRevision = 0;
		return;
	}

	QHash<QUuid, int> meshIndexByUuid;
	meshIndexByUuid.reserve(static_cast<int>(_meshStore.size()));
	for (int meshIndex = 0; meshIndex < static_cast<int>(_meshStore.size()); ++meshIndex)
	{
		if (SceneMesh* mesh = meshAt(static_cast<size_t>(meshIndex)))
			meshIndexByUuid.insert(mesh->uuid(), meshIndex);
	}

	_runtimeVisibilityRootIndex = buildRuntimeVisibilityNodeRecursive(root, meshIndexByUuid);
	_runtimeVisibilityHierarchyDirty = false;
	_runtimeVisibilityMeshStoreCount = static_cast<int>(_meshStore.size());
	_runtimeVisibilityBoundsRevision = 0;
}

bool SceneRuntime::ensureRuntimeVisibilityHierarchy(const SceneNode* root)
{
	if (_runtimeVisibilityHierarchyDirty ||
	    _runtimeVisibilityMeshStoreCount != static_cast<int>(_meshStore.size()))
	{
		rebuildRuntimeVisibilityHierarchy(root);
	}

	return _runtimeVisibilityRootIndex >= 0 &&
	       _runtimeVisibilityRootIndex < _runtimeVisibilityNodes.size();
}

bool SceneRuntime::refreshRuntimeVisibilityNodeBounds(
	int nodeIndex,
	const std::vector<unsigned char>& baseVisibleMask,
	bool refreshBounds,
	const std::function<bool(const SceneMesh*)>& isMeshVisibleFn)
{
	if (nodeIndex < 0 || nodeIndex >= _runtimeVisibilityNodes.size())
		return false;

	RuntimeVisibilityNode& runtimeNode = _runtimeVisibilityNodes[nodeIndex];
	bool boundsInitialized = false;
	bool hasVisibleMesh = false;
	BoundingBox bounds;

	for (int meshIndex : std::as_const(runtimeNode.meshIndices))
	{
		if (meshIndex < 0 || meshIndex >= static_cast<int>(_meshStore.size()))
			continue;
		if (meshIndex >= static_cast<int>(baseVisibleMask.size()) || !baseVisibleMask[meshIndex])
			continue;

		const SceneMesh* mesh = meshAt(static_cast<size_t>(meshIndex));
		if (!mesh || !isMeshVisibleFn(mesh))
			continue;

		if (!boundsInitialized)
		{
			bounds = mesh->getBoundingBox();
			boundsInitialized = true;
		}
		else
		{
			bounds.addBox(mesh->getBoundingBox());
		}
		hasVisibleMesh = true;
	}

	for (int childIndex : std::as_const(runtimeNode.children))
	{
		if (!refreshRuntimeVisibilityNodeBounds(childIndex, baseVisibleMask, refreshBounds, isMeshVisibleFn))
			continue;

		const RuntimeVisibilityNode& childNode = _runtimeVisibilityNodes[childIndex];
		if (refreshBounds && !boundsInitialized)
		{
			bounds = childNode.subtreeBounds;
			boundsInitialized = true;
		}
		else if (refreshBounds)
		{
			bounds.addBox(childNode.subtreeBounds);
		}
		hasVisibleMesh = true;
	}

	runtimeNode.subtreeHasVisibleMeshes = hasVisibleMesh;
	if (refreshBounds && boundsInitialized)
		runtimeNode.subtreeBounds = bounds;

	return hasVisibleMesh;
}

void SceneRuntime::refreshRuntimeVisibilityCacheForCurrentView(
	const SceneNode* root,
	quint64 currentBoundsRevision,
	const std::function<bool(const SceneMesh*)>& isMeshVisibleFn)
{
	_runtimeVisibilityPrepared = false;
	if (!ensureRuntimeVisibilityHierarchy(root))
		return;

	const std::vector<int>& visibleIds = currentVisibleObjectIds();
	_runtimeBaseVisibleMask.assign(_meshStore.size(), 0u);
	for (int meshIndex : visibleIds)
	{
		if (meshIndex >= 0 && meshIndex < static_cast<int>(_runtimeBaseVisibleMask.size()))
			_runtimeBaseVisibleMask[meshIndex] = 1u;
	}

	const bool refreshBounds = (_runtimeVisibilityBoundsRevision != currentBoundsRevision);
	const bool maskChanged = (_runtimeVisibilityMaskProcessedRevision != _runtimeVisibilityMaskRevision);

	if (!refreshBounds && !maskChanged)
	{
		_runtimeVisibilityPrepared = true;
		return;
	}

	refreshRuntimeVisibilityNodeBounds(
		_runtimeVisibilityRootIndex,
		_runtimeBaseVisibleMask,
		refreshBounds,
		isMeshVisibleFn);
	if (refreshBounds)
		_runtimeVisibilityBoundsRevision = currentBoundsRevision;
	if (maskChanged)
		_runtimeVisibilityMaskProcessedRevision = _runtimeVisibilityMaskRevision;
	_runtimeVisibilityPrepared = true;
}

// ---------------------------------------------------------------------------
// Load lifecycle
// ---------------------------------------------------------------------------

bool SceneRuntime::swapVisible(bool checked)
{
	if (_visibleSwapped == checked)
		return false;
	_visibleSwapped = checked;
	++_runtimeVisibilityMaskRevision;
	return true;
}

// ---------------------------------------------------------------------------
// Mesh/material batch helpers
// ---------------------------------------------------------------------------

int SceneRuntime::addMeshToDisplay(SceneMesh* mesh)
{
	if (!mesh)
		return -1;

	_meshStore.push_back({ mesh, mesh->uuid() });
	const int index = static_cast<int>(_meshStore.size() - 1);
	_displayedObjectsIds.push_back(index);
	return index;
}

SceneMesh* SceneRuntime::detachMeshAt(int index)
{
	if (index < 0 || index >= static_cast<int>(_meshStore.size()))
		return nullptr;

	SceneMesh* mesh = meshAt(static_cast<size_t>(index));
	_meshStore.erase(_meshStore.begin() + index);

	auto removeIndexAndShift = [index](std::vector<int>& ids) {
		auto it = std::find(ids.begin(), ids.end(), index);
		if (it != ids.end())
			ids.erase(it);
		for (int& id : ids)
		{
			if (id > index)
				--id;
		}
	};

	removeIndexAndShift(_displayedObjectsIds);
	removeIndexAndShift(_hiddenObjectsIds);

	if (_meshStore.empty())
	{
		_displayedObjectsIds.clear();
		_hiddenObjectsIds.clear();
		_visibleSwapped = false;
	}

	return mesh;
}

int SceneRuntime::restoreDetachedMesh(SceneMesh* mesh, int originalIndex)
{
	if (!mesh)
		return -1;

	int insertIndex = originalIndex;
	if (insertIndex < 0 || insertIndex > static_cast<int>(_meshStore.size()))
		insertIndex = static_cast<int>(_meshStore.size());

	_meshStore.insert(_meshStore.begin() + insertIndex, { mesh, mesh->uuid() });

	for (int& id : _displayedObjectsIds)
	{
		if (id >= insertIndex)
			++id;
	}
	for (int& id : _hiddenObjectsIds)
	{
		if (id >= insertIndex)
			++id;
	}

	_displayedObjectsIds.push_back(insertIndex);
	std::sort(_displayedObjectsIds.begin(), _displayedObjectsIds.end());
	return insertIndex;
}

bool SceneRuntime::clearMeshStore()
{
	for (const SceneMeshRecord& meshRecord : _meshStore)
		delete meshRecord.mesh;
	_meshStore.clear();
	_displayedObjectsIds.clear();
	_hiddenObjectsIds.clear();
	const bool wasSwapped = _visibleSwapped;
	_visibleSwapped = false;
	return wasSwapped;
}

bool SceneRuntime::setDisplayList(const std::vector<int>& ids)
{
	_displayedObjectsIds = ids;
	++_runtimeVisibilityMaskRevision;

	std::vector<int> allObjectIDs;
	allObjectIDs.reserve(_meshStore.size());
	for (size_t i = 0; i < _meshStore.size(); ++i)
		allObjectIDs.push_back(static_cast<int>(i));

	_hiddenObjectsIds.clear();
	std::set_difference(
		allObjectIDs.begin(), allObjectIDs.end(),
		_displayedObjectsIds.begin(), _displayedObjectsIds.end(),
		std::back_inserter(_hiddenObjectsIds));

	const bool wasSwapped = _visibleSwapped;
	if (_hiddenObjectsIds.empty())
		_visibleSwapped = false;

	return wasSwapped && !_visibleSwapped;
}

bool SceneRuntime::moveToRecycleBin(const QUuid& uuid, int originalIndex)
{
	SceneMesh* mesh = nullptr;
	int index = -1;

	for (size_t i = 0; i < _meshStore.size(); ++i)
	{
		SceneMesh* candidate = _meshStore[i].mesh;
		if (candidate && candidate->uuid() == uuid)
		{
			mesh  = candidate;
			index = static_cast<int>(i);
			break;
		}
	}

	if (!mesh)
		return false;

	SceneMesh* detached = detachMeshAt(index);
	if (!detached)
		return false;

	RecycleBinEntry entry;
	entry.mesh          = detached;
	entry.originalIndex = originalIndex;
	entry.deletedAt     = QDateTime::currentDateTime();
	_recycleBin[uuid]   = entry;
	return true;
}

bool SceneRuntime::restoreFromRecycleBin(const QUuid& uuid)
{
	if (!_recycleBin.contains(uuid))
		return false;

	const RecycleBinEntry entry = _recycleBin.take(uuid);
	return restoreDetachedMesh(entry.mesh, entry.originalIndex) >= 0;
}

bool SceneRuntime::permanentlyDeleteFromRecycleBin(const QUuid& uuid)
{
	if (!_recycleBin.contains(uuid))
		return false;

	const RecycleBinEntry entry = _recycleBin.take(uuid);
	delete entry.mesh;
	return true;
}

std::vector<SceneMesh*> SceneRuntime::meshPointers() const
{
	std::vector<SceneMesh*> result;
	result.reserve(_meshStore.size());
	for (const SceneMeshRecord& meshRecord : _meshStore)
		result.push_back(meshRecord.mesh);
	return result;
}

SceneMesh* SceneRuntime::getMeshByUuid(const QUuid& uuid) const
{
	for (const SceneMeshRecord& meshRecord : _meshStore)
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (mesh && mesh->uuid() == uuid)
			return mesh;
	}

	if (_recycleBin.contains(uuid))
		return _recycleBin.value(uuid).mesh;

	return nullptr;
}

SceneMesh* SceneRuntime::getMeshByIndex(int index) const
{
	if (index >= 0 && index < static_cast<int>(_meshStore.size()))
		return _meshStore[static_cast<size_t>(index)].mesh;
	return nullptr;
}

int SceneRuntime::getIndexByUuid(const QUuid& uuid) const
{
	for (size_t i = 0; i < _meshStore.size(); ++i)
	{
		SceneMesh* mesh = _meshStore[i].mesh;
		if (mesh && mesh->uuid() == uuid)
			return static_cast<int>(i);
	}
	return -1;
}

QUuid SceneRuntime::getUuidByIndex(int index) const
{
	if (index >= 0 && index < static_cast<int>(_meshStore.size()))
	{
		SceneMesh* mesh = _meshStore[static_cast<size_t>(index)].mesh;
		return mesh ? mesh->uuid() : QUuid();
	}
	return QUuid();
}

bool SceneRuntime::applyMaterialToMeshes(const std::vector<int>& ids, const Material& mat)
{
	bool needsTransmission = false;
	for (int id : ids)
	{
		try
		{
			if (id < 0 || id >= static_cast<int>(_meshStore.size()))
				continue;
			SceneMesh* mesh = meshAt(static_cast<size_t>(id));
			if (!mesh)
				continue;
			mesh->setMaterial(mat);
			if (mat.hasTransmission() || mat.diffuseTransmissionFactor() > 0.0f)
				needsTransmission = true;
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in SceneRuntime::applyMaterialToMeshes\n" << ex.what() << std::endl;
		}
	}
	return needsTransmission;
}

void SceneRuntime::applyTextureMapsToMesh(int id, const Material& resolved)
{
	try
	{
		if (id < 0 || id >= static_cast<int>(_meshStore.size()))
			return;
		SceneMesh* mesh = meshAt(static_cast<size_t>(id));
		if (!mesh)
			return;
		mesh->setTextureMaps(resolved);
		mesh->invertOpacityADSMap(resolved.isOpacityMapInverted());
		mesh->invertOpacityPBRMap(resolved.isOpacityMapInverted());
	}
	catch (const std::exception& ex)
	{
		std::cout << "Exception in SceneRuntime::applyTextureMapsToMesh\n" << ex.what() << std::endl;
	}
}

void SceneRuntime::invertAdsOpacityMaps(const std::vector<int>& ids, bool inverted)
{
	for (int id : ids)
	{
		try
		{
			if (id < 0 || id >= static_cast<int>(_meshStore.size()))
				continue;
			SceneMesh* mesh = meshAt(static_cast<size_t>(id));
			if (!mesh)
				continue;
			mesh->invertOpacityADSMap(inverted);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in SceneRuntime::invertAdsOpacityMaps\n" << ex.what() << std::endl;
		}
	}
}

std::vector<unsigned int> SceneRuntime::drainTextureCacheGpuIds()
{
	std::vector<unsigned int> gpuIds;
	for (auto& entry : _texCache)
	{
		if (entry.second.lastGPUTexture != 0)
			gpuIds.push_back(entry.second.lastGPUTexture);
	}
	_texCache.clear();
	_texRefCount.clear();
	return gpuIds;
}

void SceneRuntime::setMeshTransforms(const std::vector<int>& ids,
	const QVector3D& translation,
	const QVector3D& rotation,
	const QVector3D& scale)
{
	for (int id : ids)
	{
		try
		{
			if (id < 0 || id >= static_cast<int>(_meshStore.size()))
				continue;
			SceneMesh* mesh = meshAt(static_cast<size_t>(id));
			if (!mesh)
				continue;
			mesh->setTranslation(translation);
			mesh->setRotation(rotation);
			mesh->setScaling(scale);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in SceneRuntime::setMeshTransforms\n" << ex.what() << std::endl;
		}
	}
}

void SceneRuntime::resetMeshTransforms(const std::vector<int>& ids)
{
	for (int id : ids)
	{
		try
		{
			if (id < 0 || id >= static_cast<int>(_meshStore.size()))
				continue;
			SceneMesh* mesh = meshAt(static_cast<size_t>(id));
			if (!mesh)
				continue;
			mesh->resetTransformations();
		}
		catch (const std::exception& ex)
		{
			std::cout << "Exception in SceneRuntime::resetMeshTransforms\n" << ex.what() << std::endl;
		}
	}
}

void SceneRuntime::applyMeshTransforms(const QMap<int, TransformState>& transforms)
{
	for (auto it = transforms.begin(); it != transforms.end(); ++it)
	{
		const int index = it.key();
		const TransformState& state = it.value();
		if (index < 0 || index >= static_cast<int>(_meshStore.size()))
			continue;
		SceneMesh* mesh = meshAt(static_cast<size_t>(index));
		if (!mesh)
			continue;
		mesh->setTranslation(state.translation);
		if (state.hasExactRotation)
			mesh->setRotationQuaternion(state.rotationQuat, state.rotation);
		else
			mesh->setRotation(state.rotation);
		mesh->setScaling(state.scale);
	}
}

bool SceneRuntime::userModelTransformForFile(const QString& sourceFile, QMatrix4x4& outTransform) const
{
	bool found = false;
	QMatrix4x4 trsf;
	for (const SceneMeshRecord& meshRecord : _meshStore)
	{
		SceneMesh* mesh = meshRecord.mesh;
		if (!mesh || mesh->getSourceFile() != sourceFile)
			continue;
		if (!found)
		{
			trsf  = mesh->getTransformation();
			found = true;
		}
		else if (mesh->getTransformation() != trsf)
		{
			return false;
		}
	}

	if (!found || trsf.isIdentity())
		return false;

	outTransform = trsf;
	return true;
}

QString SceneRuntime::generateUniqueMeshName(const QString& baseName) const
{
	bool nameExists = false;
	for (const SceneMeshRecord& meshRecord : _meshStore)
	{
		const SceneMesh* mesh = meshRecord.mesh;
		if (!mesh)
			continue;
		if (mesh->getName() == baseName)
		{
			nameExists = true;
			break;
		}
	}

	if (!nameExists)
		return baseName;

	int counter = 2;
	QString uniqueName;
	while (true)
	{
		uniqueName = QString("%1 (%2)").arg(baseName).arg(counter);

		bool exists = false;
		for (const SceneMeshRecord& meshRecord : _meshStore)
		{
			const SceneMesh* mesh = meshRecord.mesh;
			if (!mesh)
				continue;
			if (mesh->getName() == uniqueName)
			{
				exists = true;
				break;
			}
		}

		if (!exists)
			break;

		++counter;
	}

	return uniqueName;
}
