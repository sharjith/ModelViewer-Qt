// ModelViewer additions, GPLv3. JT lossless IEEE-754 component arrays.
#pragma once
#include <JtDecode_VertexData.hxx>
#include <cstring>
class JtDecode_VertexData_Binary : public JtDecode_VertexData {
public:
  JtDecode_VertexData_Binary(size_t components,JtDecode_Unpack& predictor)
    :JtDecode_VertexData(components,predictor) {}
protected:
  Standard_Integer getOutCompCount(Standard_Size count) override {return int(count);}
  void decode(Decoded::Ref out) override {
    for(int j=0;j<out.CompCount();++j) {
      Jt_VecI32 bits(decodePackage(j));
      for(int i=0;i<out.Count();++i) std::memcpy(&out[i][j],&bits[i],4);
    }
  }
};
