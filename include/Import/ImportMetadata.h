// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Keys of the aiNode metadata through which an importer tells the viewer something about a node. Any importer, built in or
// third party, can set them on the aiNodes of its scene; the viewer reads them in AssImpModelLoader.
namespace ImportMetadata {

// Boolean. The node, and everything below it, is loaded but starts hidden (the same state as unchecking it in the model tree),
// because the file says so: a component hidden in a saved view, an object whose visibility is switched off. The user can show it.
inline constexpr const char* InitiallyHidden = "ModelViewer.Node.InitiallyHidden";

} // namespace ImportMetadata
