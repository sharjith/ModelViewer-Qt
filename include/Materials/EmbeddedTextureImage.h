// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "TextureMipChain.h"

// Resident images use the same top-row-first pixels as disk PNGs. Preserve
// alpha, orientation and the private authored-mip carrier during packaging.
namespace EmbeddedTextureImage
{
inline QByteArray png(const QImage& image)
{
    TextureMipChain::validate(image, TextureMipChain::decode(image));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")
        || bytes.isEmpty() || bytes.size() > TextureMipChain::Limit)
        throw std::runtime_error("Cannot package resident texture image within PNG size limit");
    return bytes;
}
}
