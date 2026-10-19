// JT format reading and visualization tools
// Copyright (C) 2014-2015 OPEN CASCADE SAS
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License, or any later
// version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// Copy of the GNU General Public License is in LICENSE.txt and
// on <http://www.gnu.org/licenses/>.

// Modified for ModelViewer-Qt, 2026-10-19 (including changes made 2026-10-18).
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#ifndef _JtDecode_MeshCoderDriver_HeaderFile
#define _JtDecode_MeshCoderDriver_HeaderFile

#include <Standard.hxx>
#include <JtData_Types.hxx>
#include <JtData_SingleHandle.hxx>
#include <JtDecode_Int32CDP.hxx>

class JtDecode_MeshDecoder;
class JtDecode_DualVFMesh;
class JtDecode_BitVec;

//! Class performing decoding of dual mesh into primal mesh.
class JtDecode_MeshCoderDriver
{
public:
  struct InputData
  {
    typedef JtData_SingleHandle<InputData> Handle;

    JtDecode_Int32CDP _viOutDegSyms[8];
    JtDecode_Int32CDP _vviOutValSyms;
    JtDecode_Int32CDP _viOutFGrpSyms;
    JtDecode_Int32CDP _vuOutFaceFlags;
    JtDecode_Int32CDP _vvuOutAttrMasks[8];
    JtDecode_Int32CDP _faceAttributeMask8_30;
    JtDecode_Int32CDP _faceAttributeMask8_4;
    JtDecode_Int32CDP _vuOutAttrMasksLrg;
    JtDecode_Int32CDP _viOutSplitVtxSyms;
    JtDecode_Int32CDP _viOutSplitPosSyms;
    bool modernMasks=false;
  };

  typedef JtData_Vector<int32_t, int32_t> IndicesVec;

  //! Read encoded data from the stream.
  Standard_EXPORT static InputData::Handle LoadInputData (JtData_Reader& theReader);

  //! Constructor.
  Standard_EXPORT JtDecode_MeshCoderDriver() : _pMeshDecoder (NULL) {}

  Standard_EXPORT ~JtDecode_MeshCoderDriver();

  //! Decode and store input data.
  Standard_EXPORT void SetInputData (InputData& theData);

  //! Decodes the mesh.
  Standard_EXPORT void Decode (IndicesVec* theVertexIndices, IndicesVec* theNormalIndices);

  int32_t _nextDegSymbol (int32_t iCCntx);
  int32_t _nextValSymbol();
  int32_t _nextFGrpSymbol();
  int32_t _nextVtxFlagSymbol();
  int64_t _nextAttrMaskSymbol (int32_t iCCntx);
  void    _nextAttrMaskSymbol (JtDecode_BitVec* iopvbAttrMask, int cDegree);
  int32_t _nextSplitFaceSymbol();
  int32_t _nextSplitPosSymbol();
  int32_t _faceCntxt (int32_t iVtx, JtDecode_DualVFMesh* pVFM);

private:
  bool myModernMasks=false;
  class decodeVFMesh;

private:
  Jt_VecI32 _viOutDegSyms[8];
  Jt_VecI32 _vviOutValSyms;
  Jt_VecI32 _viOutFGrpSyms;
  Jt_VecI32 _vuOutFaceFlags;
  Jt_VecI32 _vvuOutAttrMasks[8];
  Jt_VecI32 _faceAttributeMask8_30;
  Jt_VecI32 _faceAttributeMask8_4;
  Jt_VecU32 _vuOutAttrMasksLrg;
  Jt_VecI32 _viOutSplitVtxSyms;
  Jt_VecI32 _viOutSplitPosSyms;

  int32_t _iDegReadPos[8];
  int32_t _iValReadPos;
  int32_t _iVGrpReadPos;
  int32_t _iFFlagReadPos;
  int32_t _iAttrMaskReadPos[8];
  int32_t _iAttrMaskLrgReadPos;
  int32_t _iSplitFaceReadPos;
  int32_t _iSplitPosReadPos;

  JtDecode_MeshDecoder* _pMeshDecoder;
};

#endif // _JtDecode_MeshCoderDriver_HeaderFile
