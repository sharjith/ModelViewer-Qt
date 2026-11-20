// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Free-memory guard for large Inventor imports, modelled on the JT importer's guard: a saved model is bounded by the
// memory the machine has left rather than by small fixed constants.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h defines these as empty macros, which breaks ordinary identifiers in the including file.
#undef near
#undef far
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace Inventor::Detail {

// Free physical memory in bytes; 0 when the platform cannot say (the guard then never stops the import).
inline std::size_t systemAvailableMemory()
{
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? static_cast<std::size_t>(status.ullAvailPhys) : 0;
#elif defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    unsigned long long kib = 0;
    while (meminfo >> key >> kib) {
        if (key == "MemAvailable:")
            return static_cast<std::size_t>(kib) * 1024;
        meminfo.ignore(256, '\n');
    }
    const long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? static_cast<std::size_t>(pages) * static_cast<std::size_t>(size) : 0;
#else
    return 0;
#endif
}

// Free memory that must remain after the next step, the same floor the JT importer uses by default.
constexpr std::size_t MinFreeMemoryBytes = std::size_t(1) * 1024 * 1024 * 1024;

// Thrown when the machine is out of memory: this is never a problem with one body, so it stops the whole import.
struct ResourceError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Stops with a clear message when the free memory, less `upcoming` bytes about to be allocated, is below the floor.
inline void requireFreeMemory(const char* step, std::size_t upcoming = 0)
{
    const std::size_t available = systemAvailableMemory();
    if (available == 0)
        return;
    if (available < MinFreeMemoryBytes + upcoming)
        throw ResourceError(std::string("Not enough free memory to continue reading this Inventor file (") + step +
                                 "): " + std::to_string(available / (1024 * 1024)) + " MiB free, at least " +
                                 std::to_string((MinFreeMemoryBytes + upcoming) / (1024 * 1024)) +
                                 " MiB are needed. Close other programs and try again.");
}

} // namespace Inventor::Detail
