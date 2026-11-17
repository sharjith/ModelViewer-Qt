// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Base layouts for XT data embedded in JT files. See XtJtLayouts.cpp for provenance and
// the Siemens copyright notice. Standalone .x_t files must not use this table: their
// layouts are loaded from a local file derived from observed files.
#pragma once

#include "XtLayout.h"

namespace Xt {

const LayoutTable& jtEmbeddedBaseLayouts();

} // namespace Xt
