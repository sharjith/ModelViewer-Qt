// ModelViewer additions, GPLv3. JT Int32CDP move-to-front adapter.
#pragma once
#include <JtDecode_Int32CDP.hxx>
#include <array>
#include <stdexcept>

class JtDecode_Int32CDP_Mtf : public JtDecode_Int32CDP::EncodedData {
public:
  JtDecode_Int32CDP_Mtf(JtDecode_Int32CDP values,JtDecode_Int32CDP offsets,int count)
    : values(values), offsets(offsets), count(count) {}
  int32_t GetOutValCount() const override {return count;}
  Decoded::Mover Decode() override {
    Decoded v(values.DecodeI32()), o(offsets.DecodeI32()), result(count);
    if(o.Count()!=count) throw std::runtime_error("JT MTF offset count mismatch");
    std::array<int32_t,16> cache{}; int used=0,next=0;
    for(int i=0;i<count;++i) {
      const auto offset=o[i]; int slot; int32_t value;
      if(offset==-1) {
        if(next>=v.Count()) throw std::runtime_error("JT MTF value underflow");
        value=v[next++]; slot=(std::min)(used,15); if(used<16) ++used;
      } else {
        if(offset<0 || offset>=used) throw std::runtime_error("Invalid JT MTF cache index");
        slot=offset; value=cache[slot];
      }
      for(int k=slot;k>0;--k) cache[k]=cache[k-1];
      cache[0]=value; result[i]=value;
    }
    if(next!=v.Count()) throw std::runtime_error("Unused JT MTF values");
    return result.Move();
  }
private:
  JtDecode_Int32CDP values,offsets; int count;
};
