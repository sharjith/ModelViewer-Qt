#pragma once

#include <array>
#include <vector>

class IGpuContextResource;

// Restore-ordering phase - see GpuResourceRegistry's own doc comment for
// why this exists instead of relying on plain registration order.
enum class GpuResourcePhase
{
	Controller,             // SceneRenderController - shader/primitive-owning, must restore first
	Decorations,            // TransformGizmo, floor, skybox, axis cone, viewcube, light helpers, clipping planes
	FramebufferAuxiliaries, // transmission/SSS buffers - need width()/height(), restore last

	Count // not a real phase, sizes _byPhase
};

// Non-owning, phased registry of IGpuContextResource instances, driven by
// ViewportWidget across QOpenGLContext recreation. Ownership of the
// registered objects stays with whoever constructed them (ViewportWidget
// itself, or the std::unique_ptr<IGpuContextResource> adapters it owns) -
// this class only ever holds raw observer pointers.
//
// Phases exist because GPU resources in this codebase are created from many
// different call sites with real dependencies between them (some need
// shaders to already exist, some need width()/height()). Pure "whatever
// order things happened to register in" is too implicit for something this
// central - a future addition could register at the wrong point without
// anyone noticing until it broke. restorePhase() is called explicitly, once
// per phase, at the correct point in ViewportWidget::initializeGL() -
// see that function for the exact call sites.
class GpuResourceRegistry
{
public:
	void add(IGpuContextResource* resource, GpuResourcePhase phase);

	// Safe to call even if resource was never added (no-op) - used
	// defensively during teardown alongside explicit deletes.
	void remove(IGpuContextResource* resource);

	// Releases every registered resource, across all phases, in reverse
	// phase order and reverse registration order within each phase -
	// mirrors normal C++ destruction convention. Called once, from
	// ViewportWidget::releaseGLSceneResources().
	void releaseAll();

	// Restores every resource registered in the given phase, in forward
	// registration order. Called once per phase, at each phase's correct
	// point in ViewportWidget::initializeGL() - never call restorePhase()
	// for a later phase before an earlier one has already run this pass.
	void restorePhase(GpuResourcePhase phase);

private:
	std::array<std::vector<IGpuContextResource*>, static_cast<size_t>(GpuResourcePhase::Count)> _byPhase;
};
