#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

// The binary buffer of a glTF / GLB file: one growable block, every appended item starting on a 4-byte boundary (what glTF
// requires of accessor and buffer view offsets). std::vector grows geometrically, so appending is amortised linear (the
// Assimp writer this replaces copied the whole buffer on every append).
class GltfBinaryBuffer
{
public:
    void reserve(std::size_t bytes) { _bytes.reserve(bytes); }

    // Copies `byteCount` bytes to the next 4-byte boundary; returns the offset of the first copied byte.
    std::size_t append(const void* data, std::size_t byteCount)
    {
        const std::size_t offset = (_bytes.size() + 3u) & ~static_cast<std::size_t>(3u);
        _bytes.resize(offset + byteCount);
        if (byteCount > 0)
            std::memcpy(_bytes.data() + offset, data, byteCount);
        return offset;
    }

    std::size_t size() const { return _bytes.size(); }
    const unsigned char* data() const { return _bytes.data(); }

private:
    std::vector<unsigned char> _bytes;
};
