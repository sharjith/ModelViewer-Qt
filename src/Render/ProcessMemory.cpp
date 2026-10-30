#include "ProcessMemory.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <psapi.h>
#  include <malloc.h>
#endif

namespace ProcessMemory
{
bool query(quint64& privateBytes, quint64& workingSetBytes)
{
#ifdef _WIN32
	// K32GetProcessMemoryInfo from kernel32, so no extra import library is needed (same as ImportDiagnostics).
	using Query = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
	static const Query query = reinterpret_cast<Query>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
	if (!query)
		return false;
	PROCESS_MEMORY_COUNTERS_EX counters{};
	if (!query(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters), sizeof(counters)))
		return false;
	privateBytes = counters.PrivateUsage;
	workingSetBytes = counters.WorkingSetSize;
	return true;
#else
	Q_UNUSED(privateBytes)
	Q_UNUSED(workingSetBytes)
	return false;
#endif
}

bool breakdown(Breakdown& out)
{
#ifdef _WIN32
	out = Breakdown{};
	HANDLE heaps[256];
	const DWORD count = GetProcessHeaps(256, heaps);
	for (DWORD i = 0; i < count && i < 256; ++i)
	{
		HEAP_SUMMARY summary{};
		summary.cb = sizeof(summary);
		if (HeapSummary(heaps[i], 0, &summary))
		{
			++out.heapCount;
			out.heapAllocated += summary.cbAllocated;
			out.heapCommitted += summary.cbCommitted;
		}
	}

	SYSTEM_INFO si{};
	GetSystemInfo(&si);
	const char* address = static_cast<const char*>(si.lpMinimumApplicationAddress);
	const char* const end = static_cast<const char*>(si.lpMaximumApplicationAddress);
	MEMORY_BASIC_INFORMATION mbi{};
	while (address < end && VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi))
	{
		if (mbi.State == MEM_COMMIT)
		{
			if (mbi.Type == MEM_PRIVATE)
			{
				out.privateCommitted += mbi.RegionSize;
				const SIZE_T size = mbi.RegionSize;
				const int bucket = size < (64u << 10) ? 0 : size < (1u << 20) ? 1 : size < (16u << 20) ? 2
					: size < (256u << 20) ? 3 : 4;
				out.regionBytes[bucket] += size;
				++out.regionCount[bucket];
			}
			else if (mbi.Type == MEM_MAPPED)
				out.mappedCommitted += mbi.RegionSize;
			else if (mbi.Type == MEM_IMAGE)
				out.imageCommitted += mbi.RegionSize;
		}
		address = static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
	}
	return true;
#else
	Q_UNUSED(out)
	return false;
#endif
}
} // namespace ProcessMemory
