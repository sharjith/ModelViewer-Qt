// ModelViewer additions, GPLv3. JT third-generation bitlength decoding.
#pragma once
#include <JtDecode_Int32CDP_Bits.hxx>
#include <stdexcept>
class JtDecode_Int32CDP_Bitlength3 : public JtDecode_Int32CDP_Bits {
public:
  explicit JtDecode_Int32CDP_Bitlength3(JtData_Reader& r): JtDecode_Int32CDP_Bits(r) {}
protected:
  int32_t integer() {
    uint32_t value=0; unsigned bits=0;
    do {
      if(bits==32) throw std::runtime_error("Invalid JT nibble integer");
      value|=ReadU32(4)<<bits; bits+=4;
    } while(ReadBit());
    if(bits<32 && (value&(1u<<(bits-1)))) value|=(~uint32_t(0))<<bits;
    return int32_t(value);
  }
  void decode(int32_t* out,int32_t* end) override {
    if(!ReadBit()) {
      const int32_t lo=integer(),hi=integer();
      if(hi<lo) throw std::runtime_error("Invalid JT bitlength interval");
      uint32_t range=uint32_t(hi)-uint32_t(lo); unsigned width=0;
      for(auto r=range;r;r>>=1) ++width;
      while(out<end) *out++=int32_t(ReadU32Or0(width)+uint32_t(lo));
    } else {
      const auto mean=integer(); int width=0;
      while(out<end) {
        int delta; unsigned changes=0;
        do {
          if(++changes>32) throw std::runtime_error("Invalid JT bitlength width changes");
          delta=ReadI32(4); width+=delta;
        } while(delta==-8 || delta==7);
        const unsigned run=ReadU32(4);
        if(!run || run>size_t(end-out) || width<0 || width>32) throw std::runtime_error("Invalid JT bitlength run");
        for(unsigned i=0;i<run;++i) *out++=int32_t(uint32_t(ReadI32Or0(width))+uint32_t(mean));
      }
    }
  }
};
