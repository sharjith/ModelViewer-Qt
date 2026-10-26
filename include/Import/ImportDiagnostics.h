// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

// Opt-in, synchronously flushed import breadcrumbs: numeric mesh indices and
// memory counters, no geometry. Exception text may contain private paths.
// The terminate hook records C++ exceptions; it cannot catch SEH
// access violations, direct aborts, heap corruption or Windows fast-fail.
namespace ImportDiagnostics {
void begin() noexcept;
void checkpoint(const char* stage, qint64 sceneIndex = -1, quint64 completed = 0,
                quint64 total = 0) noexcept;
void failure(const char* stage, const char* reason, qint64 sceneIndex = -1,
             quint64 completed = 0) noexcept;
}
