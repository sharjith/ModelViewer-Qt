#pragma once

#include <QObject>
#include <any>
#include <atomic>
#include <functional>
#include <vector>

#include "AnalysisMeshSnapshot.h"

// Runs a caller-supplied, per-mesh CPU computation (a CGAL analyzer's
// snapshot-based overload, or MeshProperties's computeMeshTopology()) on a
// background thread, one AnalysisMeshSnapshot at a time - modeled on
// AssImpModelLoader's QObject-moved-to-QThread shape (see
// ViewportWidget.cpp's dispatch block for that precedent), narrowed for
// CPU-only work: unlike AssImpModelLoader's meshBatchReady, nothing here
// touches GL - unpacking a result into RenderableMesh::
// setAnalysisOverlayColors()/...FlatColors() is entirely the CALLER's
// responsibility, back on the main/GL thread, after this worker's finished()/
// cancelled() signal fires. Not meant to be used directly - see
// AnalysisComputeSession, which owns the QThread/QEventLoop dispatch
// boilerplate this class itself stays agnostic of.
class AnalysisComputeWorker : public QObject
{
	Q_OBJECT
public:
	// Return value is opaque to this class on purpose - Draft Angle/Deviation
	// return a plain std::vector<float>, Curvature a CurvatureResult,
	// Wall-Thickness a WallThicknessResult, Mass Properties a
	// MeshTopologyCheckResult. The caller (which knows which analysis it
	// asked for) std::any_cast<>s the concrete type back out of results().
	using TaskFn = std::function<std::any(const AnalysisMeshSnapshot&, const std::atomic<bool>& cancelRequested)>;

	explicit AnalysisComputeWorker(QObject* parent = nullptr) : QObject(parent) {}

	// Both setters are called from the main thread BEFORE moveToThread() -
	// see AnalysisComputeSession::runBlocking() - never call these once run()
	// may already be executing on the worker thread.
	void setSnapshots(std::vector<AnalysisMeshSnapshot> snapshots) { _snapshots = std::move(snapshots); }
	void setTaskFn(TaskFn fn) { _taskFn = std::move(fn); }

	// Valid to read only after finished()/cancelled() has been delivered to
	// the caller - run() (worker thread) and this getter (caller thread,
	// after the wait loop it wound down in has returned) never execute
	// concurrently, since Qt's queued signal delivery that wakes the caller
	// happens-after run() has already finished writing every entry.
	const std::vector<std::any>& results() const { return _results; }

public slots:
	// Invoked via QMetaObject::invokeMethod(worker, &AnalysisComputeWorker::run,
	// Qt::QueuedConnection) once this object has been moved to its own
	// QThread - runs _taskFn once per snapshot, in order. It checks for a
	// cancellation request between meshes and supplies the same atomic token
	// to each task so analyzers can also stop during their own long loops.
	// Individual third-party calls remain uninterruptible until they return.
	void run();

	// Sets only an atomic flag and is therefore deliberately safe to call
	// directly from the UI thread while run() occupies the worker thread's
	// event loop. Queuing this call to the worker thread would deadlock the
	// cancellation protocol: that queue cannot drain until run() returns.
	void cancel();

signals:
	void progress(int processed, int total);
	// `index` is a position into the snapshot list this worker was given
	// (and therefore also into results()) - NOT a SceneMesh* or any other
	// pointer, deliberately, so this signal never needs a registered
	// cross-thread metatype and the caller (which already has its own copy
	// of the snapshot list, see AnalysisComputeSession) can correlate it back
	// to a real mesh itself.
	void perMeshResult(int index);
	void finished(bool success);
	void cancelled();

private:
	std::vector<AnalysisMeshSnapshot> _snapshots;
	TaskFn _taskFn;
	std::vector<std::any> _results;
	std::atomic<bool> _cancelRequested{ false };
};
