// Original ModelViewer-Qt addition, 2026-10-31.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// ModelViewer additions, GPLv3. Counts display-significant JT content that the decoder recognises
// but does not decode, so that the importer can report it instead of dropping it silently.
#pragma once
#include <map>
#include <mutex>
#include <string>
namespace JtData_SkippedContent {
namespace Detail {
inline std::mutex& mutex() {static std::mutex m;return m;}
inline std::map<std::string,size_t>& counts() {static std::map<std::string,size_t> c;return c;}
}
// Thread safe: shape payloads are decoded on a prefetch pool as well as on the build thread.
inline void record(const std::string& what) {
  std::lock_guard<std::mutex> lock(Detail::mutex());
  ++Detail::counts()[what];
}
// Returns what was recorded since the previous take() and clears it.
inline std::map<std::string,size_t> take() {
  std::lock_guard<std::mutex> lock(Detail::mutex());
  std::map<std::string,size_t> out;
  out.swap(Detail::counts());
  return out;
}
}
