// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Free-memory guard for large JT imports. The decoder's allocation counter bounds one unit of work (see JtData_Limits.hxx);
// what bounds an assembly of any size is whether the machine still has memory for the next unit.
#pragma once
#include "Jt/JtImporter.h"
#include <JtData_Limits.hxx>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // windows.h must not turn std::min/std::max in the including file into macros
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif
namespace Jt::Detail {
// Free physical memory in bytes; 0 when the platform cannot say (the guard then does not stop anything).
inline size_t systemAvailableMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    return GlobalMemoryStatusEx(&status)?size_t(status.ullAvailPhys):0;
#elif defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");std::string key;unsigned long long kib=0;
    while(meminfo>>key>>kib){if(key=="MemAvailable:")return size_t(kib)*1024;meminfo.ignore(256,'\n');}
    const long pages=sysconf(_SC_AVPHYS_PAGES),size=sysconf(_SC_PAGESIZE);
    return pages>0&&size>0?size_t(pages)*size_t(size):0;
#else
    return 0;
#endif
}
// Starts a unit of work: restarts the decoder's allocation counter and stops with JtData_Limits::OutOfMemory when the free
// memory is below the configured floor. A reading of 0 means "unknown" and never stops the import.
inline void beginUnit(const Options& options) {
    JtData_Limits::beginUnit();
    if(!options.minFreeMemoryBytes) return;
    const size_t available=options.availableMemory?options.availableMemory():systemAvailableMemory();
    if(available && available<options.minFreeMemoryBytes)
        throw JtData_Limits::OutOfMemory("Not enough free memory to load the rest of this model: "+std::to_string(available/(1024*1024))+
            " MiB free, at least "+std::to_string(options.minFreeMemoryBytes/(1024*1024))+" MiB are needed to continue. "
            "Close other programs, or load the assembly on demand.");
}
}
