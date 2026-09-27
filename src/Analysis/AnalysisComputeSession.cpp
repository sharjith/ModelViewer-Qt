#include "AnalysisComputeSession.h"

#include <QEventLoop>
#include <QThread>

std::vector<AnalysisComputeSession::PerMeshOutcome> AnalysisComputeSession::runBlocking(
	std::vector<AnalysisMeshSnapshot> snapshots,
	AnalysisComputeWorker::TaskFn taskFn,
	const std::function<void(int, int)>& onProgress)
{
	std::vector<PerMeshOutcome> outcomes;
	if (snapshots.empty())
		return outcomes;

	// Kept separately from the snapshots handed to the worker (which takes
	// ownership via move, below) so results can be correlated back to a real
	// mesh by index, and re-validated against the key they were actually
	// computed against, without paying for a second full copy of every
	// snapshot's points/indices/normals - only the handle + key are small.
	std::vector<SceneMesh*> handleByIndex;
	std::vector<SurfaceAnalysisOverlay::CacheKey> keyByIndex;
	handleByIndex.reserve(snapshots.size());
	keyByIndex.reserve(snapshots.size());
	for (const AnalysisMeshSnapshot& snapshot : snapshots)
	{
		handleByIndex.push_back(snapshot.meshHandle);
		keyByIndex.push_back(snapshot.key);
	}

	auto* worker = new AnalysisComputeWorker();
	worker->setSnapshots(std::move(snapshots));
	worker->setTaskFn(std::move(taskFn));

	QEventLoop waitLoop;
	QThread workerThread;
	bool wasCancelled = false;

	QMetaObject::Connection finishedConnection = connect(
		worker, &AnalysisComputeWorker::finished,
		this, [&waitLoop](bool) { waitLoop.quit(); },
		Qt::QueuedConnection);

	QMetaObject::Connection cancelledConnection = connect(
		worker, &AnalysisComputeWorker::cancelled,
		this, [&waitLoop, &wasCancelled]() { wasCancelled = true; waitLoop.quit(); },
		Qt::QueuedConnection);

	QMetaObject::Connection progressConnection;
	if (onProgress)
	{
		progressConnection = connect(
			worker, &AnalysisComputeWorker::progress,
			this, onProgress,
			Qt::QueuedConnection);
	}

	_worker = worker;
	worker->moveToThread(&workerThread);

	workerThread.start();
	QMetaObject::invokeMethod(worker, &AnalysisComputeWorker::run, Qt::QueuedConnection);

	waitLoop.exec();

	workerThread.quit();
	workerThread.wait();

	disconnect(finishedConnection);
	disconnect(cancelledConnection);
	if (progressConnection)
		disconnect(progressConnection);

	// A cancelled run applies NOTHING - never half-apply a batch where some
	// meshes' results are ready and others aren't (see this class's own doc
	// comment). The worker's own results() may hold partial entries at the
	// point cancellation was noticed, but this deliberately never reads them.
	if (!wasCancelled)
	{
		const std::vector<std::any>& results = worker->results();
		outcomes.reserve(handleByIndex.size());
		for (size_t i = 0; i < handleByIndex.size(); ++i)
		{
			PerMeshOutcome outcome;
			outcome.meshHandle = handleByIndex[i];
			outcome.result = i < results.size() ? results[i] : std::any();
			outcome.snapshotKey = keyByIndex[i];
			outcomes.push_back(std::move(outcome));
		}
	}

	worker->moveToThread(this->thread());
	delete worker;
	_worker = nullptr;

	return outcomes;
}

void AnalysisComputeSession::requestCancel()
{
	if (_worker)
		_worker->cancel(); // thread-safe atomic store; must not be queued behind run()
}
