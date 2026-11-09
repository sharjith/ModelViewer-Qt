#pragma once

#include <QtGlobal>

// Memory the process currently holds, from the OS. Used only by diagnostics. Returns false where unsupported.
namespace ProcessMemory
{
bool query(quint64& privateBytes, quint64& workingSetBytes);

// Page faults taken by the process so far (soft and hard). A frame that has to read paged-out memory back shows up as a jump.
bool pageFaults(quint64& count);

// Where the process's committed memory lives (Windows only; all zero elsewhere).
struct Breakdown
{
	int heapCount = 0;
	quint64 heapAllocated = 0;      // bytes handed out by the process heaps
	quint64 heapCommitted = 0;      // bytes the heaps hold from the OS
	quint64 privateCommitted = 0;   // all private committed virtual memory (heaps, big allocations, driver)
	quint64 mappedCommitted = 0;
	quint64 imageCommitted = 0;     // executables and DLLs
	// Private committed memory by size of the region it sits in.
	quint64 regionBytes[5] = {};    // < 64 KB, < 1 MB, < 16 MB, < 256 MB, >= 256 MB
	quint64 regionCount[5] = {};
};
bool breakdown(Breakdown& out);
}
