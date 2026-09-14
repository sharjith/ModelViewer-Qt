#include "AnalysisComputeWorker.h"

void AnalysisComputeWorker::run()
{
	const int total = static_cast<int>(_snapshots.size());
	_results.assign(_snapshots.size(), std::any());

	for (int i = 0; i < total; ++i)
	{
		if (_cancelRequested.load(std::memory_order_acquire))
		{
			emit cancelled();
			return;
		}

		_results[i] = _taskFn ? _taskFn(_snapshots[i]) : std::any();
		emit perMeshResult(i);
		emit progress(i + 1, total);
	}

	emit finished(true);
}

void AnalysisComputeWorker::cancel()
{
	_cancelRequested.store(true, std::memory_order_release);
}
