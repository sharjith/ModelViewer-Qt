#pragma once

#include <QObject>
#include <any>
#include <functional>
#include <vector>

#include "AnalysisComputeWorker.h"

class SceneMesh;

// Thin controller owning the QThread + AnalysisComputeWorker + local
// QEventLoop dispatch boilerplate that would otherwise be hand-copied into
// every one of the 5 call sites this infra exists for (SurfaceAnalysisDialog's
// four Apply handlers + MassPropertiesDialog::populate()) - shape lifted
// directly from AssImpModelLoader's own dispatch block (see
// ViewportWidget.cpp's loadModel() implementation), narrowed for this app's
// simpler CPU-only case (see AnalysisComputeWorker's own doc comment on why
// it never touches GL). One instance is created, used for one runBlocking()
// call, and destroyed - not meant to be reused across multiple Apply clicks.
//
// KNOWN, DISCLOSED LIMITATION (not yet solved): runBlocking()'s nested
// QEventLoop keeps the caller's own UI responsive - which is the whole point
// - but that also means Qt can process a pending DeferredDelete event for an
// ANCESTOR of the calling dialog while runBlocking() is still on the call
// stack (e.g. the parent ModelViewer document being closed via
// deleteLater()/WA_DeleteOnClose while an Apply is in flight). If that
// ancestor owns the dialog that called runBlocking() (Qt parent-child
// ownership - true for SurfaceAnalysisDialog/MassPropertiesDialog, both
// constructed with the ModelViewer as parent), the calling dialog itself
// gets destroyed mid-call, and the remainder of that member function
// (including everything after runBlocking() returns) then executes on a
// dangling `this` - undefined behavior no local null-check inside that
// function can guard against, since the check itself would already be
// reading a destroyed object. Closing this requires the ANCESTOR's own
// close/destroy path to refuse or defer while a descendant reports an
// in-flight computation (out of scope for the dialog-side wiring this class
// exists for) - not yet done. Low-probability in practice (requires closing
// the whole document during the narrow window an Apply is computing) but
// real; flagged here rather than silently shipped.
class AnalysisComputeSession : public QObject
{
	Q_OBJECT
public:
	explicit AnalysisComputeSession(QObject* parent = nullptr) : QObject(parent) {}

	struct PerMeshOutcome
	{
		SceneMesh* meshHandle = nullptr;
		std::any result; // std::any_cast<> back to the concrete result type the caller's own TaskFn produced
		// The CacheKey the snapshot this result was computed FROM was stamped
		// with, at capture time (before dispatch) - NOT necessarily the mesh's
		// CURRENT key. The caller re-derives a fresh SurfaceAnalysisOverlay::
		// computeCurrentKey() and compares it to this one before applying the
		// result, to reject it if the mesh changed while this ran in the
		// background (see AnalysisComputeSession's own doc comment).
		SurfaceAnalysisOverlay::CacheKey snapshotKey;
	};

	// Genuinely blocks the CALLING thread's control flow until the worker
	// finishes or is cancelled - but via a local QEventLoop (same idiom
	// AssImpModelLoader's caller already uses), so paint/input events keep
	// being processed and onProgress (called back on the CALLING thread -
	// Qt::QueuedConnection marshals it there automatically) can drive a
	// live progress bar / incremental table population. Returns an empty
	// vector if `snapshots` was empty, or if requestCancel() was called
	// before the worker finished (a cancelled run applies NOTHING - see
	// this class's own doc comment on why a partial batch is never
	// half-applied).
	std::vector<PerMeshOutcome> runBlocking(
		std::vector<AnalysisMeshSnapshot> snapshots,
		AnalysisComputeWorker::TaskFn taskFn,
		const std::function<void(int processed, int total)>& onProgress = {});

	// Safe to call from the calling thread WHILE runBlocking() is executing
	// (e.g. from a Cancel button's clicked() handler, itself only reachable
	// because runBlocking()'s nested QEventLoop keeps processing UI events) -
	// queues a cancel request onto the worker thread. A no-op if no
	// runBlocking() call is currently in flight.
	void requestCancel();

private:
	AnalysisComputeWorker* _worker = nullptr; // non-null only while runBlocking() is executing
};
