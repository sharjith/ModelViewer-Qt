// Original ModelViewer-Qt addition, 2026-10-18; provenance recorded 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// ModelViewer additions to the GPLv2-or-later OCCT JT reader subset.
#pragma once
#include <JtData_Reader.hxx>
#include <lzma.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

class JtData_BufferReader : public JtData_Reader {
public:
  JtData_BufferReader(JtData_Reader& source, size_t compressedSize)
    : JtData_Reader(source.Model()) {
    if (compressedSize > 256u*1024u*1024u) throw std::runtime_error("JT compressed segment exceeds limit");
    std::vector<uint8_t> input(compressedSize);
    if (!source.ReadBytes(input.data(), input.size())) throw std::runtime_error("Truncated JT LZMA segment");
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_auto_decoder(&stream, 128u*1024u*1024u, 0) != LZMA_OK)
      throw std::runtime_error("Cannot initialize JT LZMA decoder");
    struct End { lzma_stream* s; ~End() { lzma_end(s); } } end{&stream};
    stream.next_in=input.data(); stream.avail_in=input.size();
    std::array<uint8_t, 65536> chunk;
    for (;;) {
      JtData_Limits::poll();
      stream.next_out=chunk.data(); stream.avail_out=chunk.size();
      const auto status=lzma_code(&stream,LZMA_FINISH);
      const size_t made=chunk.size()-stream.avail_out;
      if (bytes.size()+made>512u*1024u*1024u) throw std::runtime_error("JT decompressed segment exceeds limit");
      bytes.insert(bytes.end(),chunk.data(),chunk.data()+made);
      if (status==LZMA_STREAM_END) {
        if (stream.avail_in) throw std::runtime_error("Trailing bytes in JT LZMA segment");
        break;
      }
      if (status!=LZMA_OK || !made) throw std::runtime_error("Invalid/truncated JT LZMA stream");
    }
  }
  Standard_Boolean ReadBytes(void* dst, Standard_Size count) override {
    if (count>bytes.size()-position) return false;
    if(count) std::memcpy(dst,bytes.data()+position,count);
    position+=count; return true;
  }
  Standard_Boolean SkipBytes(Standard_Size count) override {
    if(count>bytes.size()-position) return false;
    position+=count; return true;
  }
  Standard_Size GetPosition() const override { return position; }
private:
  std::vector<uint8_t> bytes;
  size_t position=0;
};
