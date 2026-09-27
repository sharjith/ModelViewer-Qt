#include "GpuResourceRegistry.h"
#include "IGpuContextResource.h"

#include <algorithm>

void GpuResourceRegistry::add(IGpuContextResource* resource, GpuResourcePhase phase)
{
	if (!resource)
		return;
	_byPhase[static_cast<size_t>(phase)].push_back(resource);
}

void GpuResourceRegistry::remove(IGpuContextResource* resource)
{
	if (!resource)
		return;
	for (auto& phaseResources : _byPhase)
	{
		phaseResources.erase(
			std::remove(phaseResources.begin(), phaseResources.end(), resource),
			phaseResources.end());
	}
}

void GpuResourceRegistry::releaseAll()
{
	for (auto phaseIt = _byPhase.rbegin(); phaseIt != _byPhase.rend(); ++phaseIt)
	{
		for (auto resourceIt = phaseIt->rbegin(); resourceIt != phaseIt->rend(); ++resourceIt)
			(*resourceIt)->releaseGpuResources();
	}
}

void GpuResourceRegistry::restorePhase(GpuResourcePhase phase)
{
	for (IGpuContextResource* resource : _byPhase[static_cast<size_t>(phase)])
		resource->restoreGpuResources();
}
