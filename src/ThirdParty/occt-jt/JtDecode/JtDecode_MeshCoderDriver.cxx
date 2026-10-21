#include <JtData_Limits.hxx>
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
// Additional bounded palette/face-group qualification changes: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtDecode_MeshCoderDriver.hxx>

#include <JtData_VectorRef.hxx>

#include <Standard_Assert.hxx>
#include <NCollection_Handle.hxx>

#include <vector>
#include <bitset>

//#define NO_JT_MULTITHREADING
#include <JtData_Parallel.hxx>
#include <JtDecode_Int32CDPScheduler.hxx>

#ifdef _MSC_VER
  #pragma warning (disable : 4127) // to prevent compilation warnings on Standard_ASSERT
#endif

// Vector that operates on individual bits in an unsigned integer vector,
// where as the ith bit can be referenced by word(i>>cBitsLog2) and bit(i&0x1F)
// 32-bit implementation uses 32-bit words
class JtDecode_BitVec
{
public:
  enum { cWordBits = 32 }; // Number of bits per word
  enum { cBitsLog2 = 5 }; // 2^n Bits

public:
  JtDecode_BitVec () {}
  JtDecode_BitVec (const JtCheckedVector<uint32_t>& theVector) : myVector (theVector) {}

  void Clear() { myVector.clear(); }
  uint32_t* Data() { return &(myVector.front()); }
  void SetLength (int32_t theLength) { myVector.resize (theLength / 32 + 1); }

  bool Test (int32_t theBitNb)
  {
    JtCheckedVector<uint32_t>::size_type aPos (theBitNb >> cBitsLog2);

    if (aPos >= myVector.size())
      return false;

    return (myVector[aPos] & (1 << (theBitNb & 0x1F))) != 0;
  }

  void Set (int32_t theBitNb)
  {
    JtCheckedVector<uint32_t>::size_type aPos (theBitNb >> cBitsLog2);

    while (aPos >= myVector.size())
      myVector.push_back (0);

    myVector[aPos] |= (1 << (theBitNb & 0x1F));
  }

private:
  JtCheckedVector<uint32_t> myVector;
};

//! The DualVFMesh (Dual Vertex-Facet Mesh) is a support class paired with
//! the topology decoder itself, and represents a closed two-manifold polygon
//! mesh. The topology decoder reconstructs the encoded dual mesh into a DualVFMesh,
//! building it one vertex and one facet at a time. When the decoder is finished,
//! it will have visited each vertex and each face of the dual mesh exactly once.
class JtDecode_DualVFMesh
{
public:

  JtDecode_DualVFMesh()  {}

  JtDecode_DualVFMesh (const JtDecode_DualVFMesh &rhs);

  JtDecode_DualVFMesh &operator= (const JtDecode_DualVFMesh &rhs);

  // ========== Topology Interface ==========

  bool clear();

  // Vertex creation

  bool isValidVtx (int32_t iVtx) const;

  bool newVtx (int32_t iVtx, int32_t iValence, uint16_t uFlags = 0);

  bool setVtxFlags (int32_t iVtx, uint16_t uFlags);

  bool setVtxGrp (int32_t iVtx, int32_t iVGrp);

  uint16_t vtxFlags (int32_t iVtx) const;

  int32_t vtxGrp (int32_t iVtx) const;

  // Face creation

  bool isValidFace (int32_t iFace) const;

  bool newFace (int32_t iFace, int32_t cDegree, int32_t cFaceAttrs = 0, uint64_t uFaceAttrMask = 0, uint16_t uFlags = 0);

  bool newFace (int32_t iFace, int32_t cDegree, int32_t cFaceAttrs, const JtDecode_BitVec* pvbFaceAttrMask, uint16_t uFlags);

  bool setFaceFlags (int32_t iFace, uint16_t uFlags);

  uint16_t faceFlags (int32_t iVtx) const;

  bool setFaceAttr (int32_t iFace, int32_t iAttrSlot, int32_t iFaceAttr);

  int32_t faceAttr (int32_t iFace, int32_t iAttrSlot) const;

  // Topology connection

  bool setVtxFace (int32_t iVtx, int32_t iFaceSlot, int32_t iFace);

  bool setFaceVtx (int32_t iFace, int32_t iVtxSlot, int32_t iVtx);

  // Queries
  int32_t valence (int32_t iVtx) const
  {
    return _vVtxEnts[iVtx].cVal;
  }

  int32_t degree (int32_t iFace) const
  {
    return _vFaceEnts[iFace].cDeg;
  }

  int32_t face (int32_t iVtx, int32_t iFaceSlot) const
  {
    return _viVtxFaceIndices[(_vVtxEnts[iVtx]).iVFI + iFaceSlot];
  }

  int32_t vtx (int32_t iFace, int32_t iVtxSlot) const
  {
    return _viFaceVtxIndices[_vFaceEnts[iFace].iFVI + iVtxSlot];
  }

  int32_t numVts() const
  {
    return static_cast<int32_t>(_vVtxEnts.size());
  }

  int32_t numFaces() const
  {
    return static_cast<int32_t>(_vFaceEnts.size());
  }

  int32_t numAttrs() const
  {
    return static_cast<int32_t>(_viFaceAttrIndices.size());
  }

  int32_t numAttrs (int32_t iFace) const
  {
    return _vFaceEnts[iFace].cFaceAttrs;
  }

  uint64_t attrMask (int32_t iFace) const
  {
    return _vFaceEnts[iFace].uAttrMask;
  }

  const JtDecode_BitVec* attrMaskV (int32_t iFace) const
  {
    return _vFaceEnts[iFace].pvbAttrMask;
  }

  int32_t findVtxSlot (int32_t iFace, int32_t iTargVtx) const;

  int32_t findFaceSlot (int32_t iVtx, int32_t iTargFace) const;

  int32_t emptyFaceSlots (int32_t iFace) const
  {
    return _vFaceEnts[iFace].cEmptyDeg;
  }

  int32_t vtxFaceAttr (int32_t iVtx, int32_t iFace)
  {
    FaceEnt& rVE = _vFaceEnts.at (iFace);
    if (rVE.cFaceAttrs <= 0)
    {
      return -1;
    }

    int32_t cDeg = rVE.cDeg;
    int32_t iAttrSlot = rVE.cFaceAttrs - 1;
    for (int32_t iVtxSlot = 0; iVtxSlot < cDeg; iVtxSlot++)
    {
      int32_t iSlot = iVtxSlot;
      if (cDeg <= JtDecode_DualVFMesh::cMBits)
      {
        if ((rVE.uAttrMask & ((uint64_t)1 << iSlot)) != 0)
          iAttrSlot++;
      }
      else
      {
        if (rVE.pvbAttrMask->Test (iSlot))
          iAttrSlot++;
      }

      if (iAttrSlot >= rVE.cFaceAttrs)
      {
        iAttrSlot -= rVE.cFaceAttrs;
      }

      if (_viFaceVtxIndices.at (rVE.iFVI + iVtxSlot) == iVtx)
      {
        return _viFaceAttrIndices.at (rVE.iFAI + iAttrSlot);
      }
    }

    return -1;
  }

  // ========== VFMesh Data Members ==========

public:

  struct VtxEnt
  {
    uint16_t cVal;   // Vertex valence
    uint16_t uFlags; // User flags
    int32_t  iVGrp;  // Vertex group
    int32_t  iVFI;   // Index into _viVtxFaceIndices of cVal incident faces

    VtxEnt() : cVal (0), uFlags (0), iVGrp (-1), iVFI (-1) {}
  };

  // Number of optimized mask bits.
  static const int32_t cMBits = 64;

  struct FaceEnt
  {
    uint16_t cDeg;       // Face degree
    uint16_t cEmptyDeg;  // Empty degrees (opt for emptyFaceSlots())
    uint16_t cFaceAttrs; // Number of face attributes
    uint16_t uFlags;     // User flags

    uint64_t         uAttrMask;   // Degree-ring attribute mask as a UInt64
    JtDecode_BitVec* pvbAttrMask; // Degree-ring attribute mask as a BitVec

    int32_t iFVI; // Index into _viFaceVtxIndices of cDeg incident vertices
    int32_t iFAI; // Index into _viFaceAttrIndices of cAttr attributes

    FaceEnt()
      : cDeg (0), cEmptyDeg (0), cFaceAttrs (0), uFlags (0),
        uAttrMask (0), pvbAttrMask (NULL), iFVI (-1), iFAI (-1)
    {
    }

    FaceEnt (const FaceEnt &rhs)
      : cDeg (rhs.cDeg), cEmptyDeg (rhs.cEmptyDeg), cFaceAttrs (rhs.cFaceAttrs),
        uFlags(rhs.uFlags), iFVI (rhs.iFVI), iFAI (rhs.iFAI)
    {
      if (cDeg <= cMBits)
      {
        pvbAttrMask = NULL;
        uAttrMask = rhs.uAttrMask;
      }
      else
      {
        pvbAttrMask = new JtDecode_BitVec (*rhs.pvbAttrMask);
        uAttrMask = 0;
      }
    }

    ~FaceEnt()
    {
      delete pvbAttrMask;
    }
  };

protected:

  // Subscripted by atom number, the entry contains the vertex valence and
  // points to the location in _viVtxFaceIndices of valence consecutive
  // integers that in turn contain the indices of the incident faces
  // in _vFaceRecs to the vertex.
  JtCheckedVector<VtxEnt> _vVtxEnts;

  // Subscripted by unique vertex record number, the entry contains the
  // face degree and points to the location in _viFaceVtxIndices of
  // cDeg consecutive integers that in turn contain the indices of the
  // vertices incident upon the face, in CCW order, in _vVtxRecs.
  JtCheckedVector<FaceEnt> _vFaceEnts;

  // Combined storage for all vertices.
  JtCheckedVector<int32_t> _viVtxFaceIndices;

  // Combined storage for all faces.
  JtCheckedVector<int32_t> _viFaceVtxIndices;

  // Combined storage for all face attribute record identifiers
  JtCheckedVector<int32_t> _viFaceAttrIndices;
};

bool JtDecode_DualVFMesh::clear()
{
  _vVtxEnts.clear();
  _vFaceEnts.clear();
  _viVtxFaceIndices.clear();
  _viFaceVtxIndices.clear();
  _viFaceAttrIndices.clear();

  return true;
}

bool JtDecode_DualVFMesh::isValidVtx (int32_t iVtx) const
{
  bool bRet = false;
  if (iVtx >= 0 && iVtx < (int32_t)_vVtxEnts.size())
  {
    const VtxEnt &rFE = _vVtxEnts[iVtx];
    bRet = (rFE.cVal != 0);
  }
  return bRet;
}

bool JtDecode_DualVFMesh::newVtx (int32_t iVtx, int32_t iValence, uint16_t uFlags)
{
  // for debug
  Standard_ASSERT (iVtx == (int32_t)_vVtxEnts.size(), "Error", return false);

  _vVtxEnts.push_back (VtxEnt());
  VtxEnt& rFE = _vVtxEnts.back();
  if (rFE.cVal != iValence)
  {
    rFE.cVal = static_cast<uint16_t> (iValence);
    rFE.uFlags = uFlags;
    rFE.iVFI = static_cast<int32_t>(_viVtxFaceIndices.size());
    _viVtxFaceIndices.resize (rFE.iVFI + iValence);
    for (int32_t i = rFE.iVFI; i < rFE.iVFI + iValence; ++i)
      _viVtxFaceIndices[i] = -1;
  }
  return true;
}

bool JtDecode_DualVFMesh::setVtxGrp (int32_t iVtx, int32_t iVGrp)
{
  VtxEnt &rFE = _vVtxEnts[iVtx];
  rFE.iVGrp = iVGrp;
  return true;
}

bool JtDecode_DualVFMesh::setVtxFlags (int32_t iVtx, uint16_t uFlags)
{
  VtxEnt &rFE = _vVtxEnts[iVtx];
  rFE.uFlags = uFlags;
  return true;
}

int32_t JtDecode_DualVFMesh::vtxGrp (int32_t iVtx) const
{
  int32_t u = -1;
  if (iVtx >= 0 && iVtx < (int32_t)_vVtxEnts.size())
  {
    const VtxEnt &rFE = _vVtxEnts[iVtx];
    u = rFE.iVGrp;
  }
  return u;
}

uint16_t JtDecode_DualVFMesh::vtxFlags (int32_t iVtx) const
{
  uint16_t u = 0;
  if (iVtx >= 0 && iVtx < (int32_t)_vVtxEnts.size())
  {
    const VtxEnt &rFE = _vVtxEnts[iVtx];
    u = rFE.uFlags;
  }

  return u;
}

bool JtDecode_DualVFMesh::isValidFace (int32_t iFace) const
{
  bool bRet = false;
  if (iFace >= 0 && iFace < (int32_t)_vFaceEnts.size())
  {
    const FaceEnt &rVE = _vFaceEnts[iFace];
    bRet = (rVE.cDeg != 0);
  }
  return bRet;
}

bool JtDecode_DualVFMesh::newFace (int32_t  iFace,
                                   int32_t  cDegree,
                                   int32_t  cFaceAttrs,
                                   uint64_t uFaceAttrMask,
                                   uint16_t uFlags)
{
  // for debug
  Standard_ASSERT ((int32_t)_vFaceEnts.size() == iFace, "Error", return false);

  _vFaceEnts.push_back (FaceEnt());
  FaceEnt& rVE = _vFaceEnts.back();

  if (rVE.cDeg != cDegree)
  {
    rVE.cDeg       = (uint16_t)cDegree;
    rVE.cEmptyDeg  = (uint16_t)cDegree;
    rVE.cFaceAttrs = (uint16_t)cFaceAttrs;
    rVE.uFlags     = uFlags;
    rVE.uAttrMask  = uFaceAttrMask;
    rVE.iFVI       = static_cast<int32_t>(_viFaceVtxIndices.size());
    rVE.iFAI       = static_cast<int32_t>(_viFaceAttrIndices.size());

    _viFaceVtxIndices.resize (rVE.iFVI + cDegree);

    if (cFaceAttrs > 0)
      _viFaceAttrIndices.resize (rVE.iFAI + cFaceAttrs);

    for (int32_t i = rVE.iFVI; i < rVE.iFVI + cDegree; ++i)
      _viFaceVtxIndices[i] = -1;

    for (int32_t i = rVE.iFAI ; i < rVE.iFAI + cFaceAttrs; ++i)
      _viFaceAttrIndices[i] = -1;
  }

  return true;
}

bool JtDecode_DualVFMesh::newFace (int32_t iFace,
                                   int32_t cDegree,
                                   int32_t cFaceAttrs,
                                   const JtDecode_BitVec* pvbFaceAttrMask,
                                   uint16_t uFlags)
{
  Standard_ASSERT ((int32_t)_vFaceEnts.size() == iFace, "Error", return false);

  _vFaceEnts.push_back (FaceEnt());
  FaceEnt& rVE = _vFaceEnts.back();

  if (rVE.cDeg != cDegree)
  {
    rVE.cDeg        = (uint16_t)cDegree;
    rVE.cEmptyDeg   = (uint16_t)cDegree;
    rVE.cFaceAttrs  = (uint16_t)cFaceAttrs;
    rVE.uFlags      = uFlags;
    rVE.pvbAttrMask = new JtDecode_BitVec (*pvbFaceAttrMask);
    rVE.iFVI        = static_cast<int32_t>(_viFaceVtxIndices.size());
    rVE.iFAI        = static_cast<int32_t>(_viFaceAttrIndices.size());

    _viFaceVtxIndices.resize (rVE.iFVI + cDegree);

    if (cFaceAttrs > 0)
      _viFaceAttrIndices.resize (rVE.iFAI + cFaceAttrs);

    for (int32_t i = rVE.iFVI; i < rVE.iFVI + cDegree; ++i)
      _viFaceVtxIndices[i] = -1;

    for (int32_t i = rVE.iFAI; i < rVE.iFAI + cFaceAttrs; ++i)
      _viFaceAttrIndices[i] = -1;
  }

  return true;
}

bool JtDecode_DualVFMesh::setFaceFlags (int32_t iFace, uint16_t uFlags)
{
  FaceEnt &rVE = _vFaceEnts[iFace];
  rVE.uFlags = uFlags;
  return true;
}

uint16_t JtDecode_DualVFMesh::faceFlags (int32_t iFace) const
{
  uint16_t u = 0;
  if (iFace >= 0 && iFace < (int32_t)_vFaceEnts.size())
  {
    const FaceEnt &rVE = _vFaceEnts[iFace];
    u = rVE.uFlags;
  }
  return u;
}

bool JtDecode_DualVFMesh::setFaceAttr (int32_t iFace,
                                       int32_t iAttrSlot,
                                       int32_t iFaceAttr)
{
  FaceEnt &rVE = _vFaceEnts[iFace];
  int32_t *paiFAI = &(_viFaceAttrIndices.front());
  paiFAI[rVE.iFAI + iAttrSlot] = iFaceAttr;
  return true;
}

int32_t JtDecode_DualVFMesh::faceAttr (int32_t iFace, int32_t iAttrSlot) const
{
  int32_t u = 0;
  if (iFace >= 0 && iFace < (int32_t)_vFaceEnts.size())
  {
    const FaceEnt &rVE = _vFaceEnts[iFace];
    if (iAttrSlot >= 0 && iAttrSlot < rVE.cDeg)
    {
      const int32_t* paiFAI = &(_viFaceAttrIndices.front());
      u = paiFAI[rVE.iFAI + iAttrSlot];
    }
  }
  return u;
}

// Attaches VF face iFace to VF vertex iVtx in the vertex's
// face slot iFaceSlot
bool JtDecode_DualVFMesh::setVtxFace (int32_t iVtx, int32_t iFaceSlot, int32_t iFace)
{
  VtxEnt &rFE = _vVtxEnts[iVtx];
  _viVtxFaceIndices[rFE.iVFI + iFaceSlot] = iFace;
  return true;
}

// Attaches VF vertex iVtx to VF face iFace in the face's
// vertex slot iVtxSlot
bool JtDecode_DualVFMesh::setFaceVtx (int32_t iFace, int32_t iVtxSlot, int32_t iVtx)
{
  FaceEnt &rVE = _vFaceEnts[iFace];
  int32_t *paiFVI = &(_viFaceVtxIndices.front());
  rVE.cEmptyDeg -= (paiFVI[rVE.iFVI + iVtxSlot] != iVtx);
  paiFVI[rVE.iFVI + iVtxSlot] = iVtx;
  return true;
}

// Searches the list of incident vts to face iFace for
// iTargVtx and returns the vtx slot at which it is found
// or -1 if iTargVtx is not found.
int32_t JtDecode_DualVFMesh::findVtxSlot (int32_t iFace, int32_t iTargVtx) const
{
  const FaceEnt &rVE = _vFaceEnts[iFace];
  const int32_t *const pFaceVtxIndices = &(_viFaceVtxIndices.front()) + rVE.iFVI;
  int32_t cDeg = rVE.cDeg;
  int32_t iSlot = -1;
  for (int32_t iVtxSlot = 0 ; iVtxSlot < cDeg ; iVtxSlot++)
  {
    if (pFaceVtxIndices[iVtxSlot] == iTargVtx)
    {
      iSlot = iVtxSlot;
      break;
    }
  }
  return iSlot;
}

// Searches the list of incident faces to vertex iVtx for
// iTargFace and returns the face slot at which it is found
// or -1 if iTargFace is not found.
int32_t JtDecode_DualVFMesh::findFaceSlot (int32_t iVtx, int32_t iTargFace) const
{
  const VtxEnt &rFE = _vVtxEnts[iVtx];
  const int32_t* const pVtxFaceIndices = &(_viVtxFaceIndices.front()) + rFE.iVFI;
  for (int32_t iFaceSlot = 0; iFaceSlot < rFE.cVal; ++iFaceSlot)
  {
    if (pVtxFaceIndices[iFaceSlot] == iTargFace)
    {
      return iFaceSlot;
    }
  }
  return -1;
}

// This class serves as the abstract base class from which two concrete classes
// are derived to implement the core operations for a polygonal
// mesh coder or decoder. An instance of this object is used by the
// MeshCoderDriver to encode and decode polygonal meshes.
//
// This class makes extensive use of DualVFMesh objects as the primary source and
// destination mesh topology storage data structures. This mediating data
// structure is necessary because the mesh coding scheme is deeply cooperative
// with and dependent upon such a vertex-facet data structure. Please refer to
// DualVFMesh for more information.
class JtDecode_MeshCodec
{
public:

  // ========== Housekeeping Interface ==========

  JtDecode_MeshCodec (JtDecode_MeshCoderDriver *pTMC = NULL)
    :_iFaceAttrCtr (0)
  {
    _pTMC = pTMC;
  }

protected:

  virtual ~JtDecode_MeshCodec()
  {
    //
  }

public:

  // ========== Setup and Apply Interface ==========

  void setMeshCoderDriver (JtDecode_MeshCoderDriver *pTMC)
  {
    _pTMC = pTMC;
  }

  JtDecode_DualVFMesh *vfm() const
  {
    return const_cast<JtDecode_DualVFMesh*> (_pDstVFM.operator->());
  }

  void run();

  // ========== Generic encode/decode Driver Chain ==========

  void clear();

  void runComponent (bool &obFoundComponent);

  void initNewComponent (bool &obFoundComponent);

  void    completeV (int32_t iFace);

  int32_t activateF (int32_t iVtx, int32_t iVSlot);

  int32_t activateV (int32_t iFace, int32_t iFSlot);

  void    completeF (int32_t iVtx, int32_t jFSlot);

  void    addVtxToFace (int32_t iVtx, int32_t iVSlot, int32_t iFace, int32_t iFSlot);

  // Active face list management

  bool    addActiveFace (int32_t iFace);

  int32_t nextActiveFace();

  void    removeActiveFace (int32_t iFace);

  int32_t activeFaceOffset (int32_t iFace) const;

private:

  // ========== Polymorphic I/O Interface ==========

  virtual int32_t ioVtxInit() = 0;

  virtual int32_t ioVtx (int32_t iFace, int32_t jFSlot) = 0;

  virtual int32_t ioFace (int32_t iVtx, int32_t iVSlot) = 0;

  virtual int32_t ioSplitFace (int32_t iVtx, int32_t iVSlot) = 0;

  virtual int32_t ioSplitPos (int32_t iVtx, int32_t iVSlot) = 0;

  // ========== Member Data ==========

protected:

  JtDecode_MeshCoderDriver *_pTMC; // TopoDualMeshCoder this codec is attached to

  NCollection_Handle<JtDecode_DualVFMesh> _pSrcVFM; // Input VFMesh
  NCollection_Handle<JtDecode_DualVFMesh> _pDstVFM; // Output VFMesh

  JtCheckedVector<int32_t> _viActiveFaces;   // Stack of incomplete "active faces"
  JtDecode_BitVec _vbRemovedActiveFaces; // Helper bitvec parallel to above

  // Used by decoder to assign running attr indices
  int32_t _iFaceAttrCtr;
};

#define IncModN( val, n ) { val = (val + 1) & ((val == n - 1) - 1); }
#define DecModN( val, n ) { val = ((val - 1) & ((val == 0) - 1)) | ((n - 1) & ((val != 0) - 1)); }

// Runs the mesh encoder/decoder machine.
// If decoding is being performed, it consumes the mesh
// coding symbols from pre-filled member variables to produce
// the output VFMesh _pDstVFM.
void JtDecode_MeshCodec::run()
{
  // Assert state is consistent and ready to co/dec
  if (_pDstVFM.IsNull())
    _pDstVFM = new JtDecode_DualVFMesh();

  Standard_ASSERT_RETURN (!_pDstVFM.IsNull(), "Error", );
  _pDstVFM->clear();
  clear();

  // Co/dec connected mesh components one at a time
  bool bFoundComponent = true;
  while (bFoundComponent)
  {
    runComponent (bFoundComponent);
  }
}

void JtDecode_MeshCodec::clear()
{
  // Setup
  _viActiveFaces.clear();
  _vbRemovedActiveFaces.Clear();
  _iFaceAttrCtr = 0;
}

// Decodes one "connected component" (contiguous group of polygons) into
// _pDstVFM. Because the polygonal model may be formed of multiple
// disconnected mesh components, it may be necessary for run() to call this
// method multiple times. This method returns obFoundComponent = True
// if it actually encoded a new mesh component, and obFoundComponent = False
// if it did not.
void JtDecode_MeshCodec::runComponent (bool &obFoundComponent)
{
  int32_t iFace;
  initNewComponent (obFoundComponent);
  if (!obFoundComponent)
    return;

  while ((iFace = nextActiveFace()) != -1)
  {
    completeV (iFace);
    removeActiveFace (iFace);
  }
}

// Locates an unencoded vertex and begins the encoding
// process for the newly-found mesh component.
void JtDecode_MeshCodec::initNewComponent (bool &obFoundComponent)
{
  obFoundComponent = true;

  // Call ioVtxInit() to start us off with the seed face
  // from a new "connected component" of polygons.
  int32_t iVtx, i;
  if ((iVtx = ioVtxInit()) == -1)
  {
    obFoundComponent = false; // All vtxs are processed
    return;
  }

  int32_t cVal = _pDstVFM->valence (iVtx);
  for (i = 0 ; i < cVal ; ++i)
    activateF (iVtx, i); // Process all faces
}

// Completes the VFMesh face iFace on _pDstVFM by calling activateV() and
// completeF() for each as-yet inactive incident vertexes in the face's
// degree ring.
void JtDecode_MeshCodec::completeV (int32_t iFace)
{
  // While there is an empty vertex slot on the face
  int32_t jVtxSlot;
  int32_t iVSlot = 0;
  while ((jVtxSlot = _pDstVFM->findVtxSlot (iFace, -1)) != -1)
  {
    // Create and return a vertex iVtx, attaching it to iFace at vertex
    // slot jVtxSlot.
    int32_t iVtx = activateV (iFace, jVtxSlot);

    // Assert FV consistency
    Standard_ASSERT_RETURN (
      _pDstVFM->vtx (iFace, jVtxSlot) == iVtx && _pDstVFM->face (iVtx, iVSlot) == iFace, "Error", );

    // Process the faces of iVtx starting from face slot
    // jVtxSlot where iVtx is incident on iFace
    completeF (iVtx, jVtxSlot);

    // Invariant "VF": vertex (iVtx).face (iVSlot) == iFace &&
    // face (iFace).vtx (jVtxSlot) == iVtx
  }
}

// "Activates" the VFMesh face, on _pDstVFM, at face iFace vertex slot iVSlot
// by calling ioFace() to obtain a new vertex number and hooking it up to the
// topological structure. If the face is a SPLIT face, then call
// ioSplitFace() and ioSplitPos() to get the information necessary to connect
// to an already-active face. Note that we use the term "activate" here to
// mean "read" for mesh decoding.
int32_t JtDecode_MeshCodec::activateF (int32_t iVtx, int32_t iVSlot)
{
  int32_t jFSlot;
  // ioFace might return -2 as an error condition
  int32_t iFace = ioFace (iVtx, iVSlot);

  if (iFace >= 0) // If a new active face
  {
    if (!_pDstVFM->setVtxFace (iVtx, iVSlot, iFace) ||
        !_pDstVFM->setFaceVtx (iFace, 0, iVtx) ||
        !addActiveFace (iFace))
    {
      return -2;
    }
  }
  else if (iFace == -1) // Face already exists, so Split
  {
    iFace = ioSplitFace (iVtx, iVSlot); // v's index in ActiveSet, returns v
    jFSlot = ioSplitPos (iVtx, iVSlot); // Position of iVtx in v
    _pDstVFM->setVtxFace (iVtx, iVSlot, iFace);
    addVtxToFace (iVtx, iVSlot, iFace, jFSlot);
  }

  return iFace;
}

// "Activates" the VFMesh vertex, on _pDstVFM, at face iFace vertex slot iVSlot
// by calling ioFace() to obtain a new face number and hooking it up to the
// topological structure. Note that we use the term "activate" here to
// mean "read" for mesh decoding.
int32_t JtDecode_MeshCodec::activateV (int32_t iFace, int32_t iVSlot)
{
  int32_t iVtx = ioVtx (iFace, iVSlot); // I/O valence; create a vtx
  _pDstVFM->setVtxFace (iVtx, 0, iFace);
  addVtxToFace (iVtx, 0, iFace, iVSlot);
  return iVtx;
}

// Completes the vertex iVtx on _pDstVFM by activating all inactive faces
// incident upon it. As an optimization, the user must also pass in iVSlot
// which is the vertex slot on face 0 of iVtx where iVtx is located. This
// method begins its examination of iVtx's faces at face 0 by working its
// way around the vertex in both CCW and CW directions, checking to see if there
// are any faces that can be hooked into iVtx without calling activateF().
// This can happen when a face is completed by a nearby vertex before coming
// here. The situation can be detected by traversing the topology of the
// _pDstVFM over to the neighboring vertex and checking if it already has a
// face number for the corresponding face entry on iVtx. If so, then
// iVtx and the already completed face are connected together, and the
// next face around iVtx is examined. When the process can go no further,
// this method calls _activateF() on the remaining unresolved span of faces
// around the vertex.
void JtDecode_MeshCodec::completeF (int32_t iVtx, int32_t iVSlot)
{
  JtDecode_DualVFMesh *pDstVFM = const_cast<JtDecode_DualVFMesh*> (_pDstVFM.operator->());

  int32_t i, vp, vn, jp, jn, iVtx2;
  int32_t cVal = pDstVFM->valence (iVtx);

  // Walk CCW from face slot 0, attempting to link in as many
  // already-reachable faces as possible until we reach one
  // that is inactive.
  vp = pDstVFM->face (iVtx, 0);
  jp = iVSlot;
  i = 1;

  while ((vn = pDstVFM->face (iVtx, i)) != -1) // Forces "FV" in the "next" direction
  {
    DecModN (jp, pDstVFM->degree (vp));
    iVtx2 = pDstVFM->vtx (vp, jp);

    if (iVtx2 == -1)
      break;

    jn = pDstVFM->findVtxSlot (vn, iVtx2);
    Standard_ASSERT_RETURN (jn > -1, "Error", );
    DecModN (jn, pDstVFM->degree (vn));
    addVtxToFace (iVtx, i, vn, jn);
    vp = vn;
    jp = jn;
    i++;

    if (i >= cVal)
      return;
  }

  // Walk CW from face slot 0, attempting to link in as many
  // already-reachable faces as possible until we reach one
  // that is inactive
  int32_t ilast = i;
  vp = pDstVFM->face (iVtx, 0);
  jp = iVSlot;
  i = pDstVFM->valence (iVtx) - 1;

  while ((vn = pDstVFM->face (iVtx, i)) != -1) // Forces "VF" in "prev" direction
  {
    IncModN (jp, pDstVFM->degree (vp));

    iVtx2 = pDstVFM->vtx (vp, jp);
    if (iVtx2 == -1)
      break;

    jn = pDstVFM->findVtxSlot (vn, iVtx2);
    Standard_ASSERT_RETURN (jn > -1, "Error", );

    IncModN (jn, pDstVFM->degree (vn));
    addVtxToFace (iVtx, i, vn, jn);
    vp = vn;
    jp = jn;
    i--;

    if (i < ilast)
      return;
  }

  // Activate the remaining faces on iVtx that cannot be decoced from
  // the already-assembled topology in the destination VFMesh
  for (; ilast <= i ; ilast++)
  {
    int32_t iFace = activateF (iVtx, ilast);
    Standard_ASSERT_RETURN (iFace >= -1, "Error", );
  }
}

// This method connects vertex iVtx into the topology of
// _pDstVFM at and around iFace. First, it connects iVtx
// to iFace's degree ring at position iVSlot. Next, it
// will connect iVtx into the faces at the other ends of
// the shared edges between iVtx and the next vertices CS and
// CCW about iFace if necessary.
void JtDecode_MeshCodec::addVtxToFace (int32_t iVtx, int32_t jFSlot, int32_t iFace, int32_t iVSlot)
{
  int32_t jFSlotCW = iVSlot, jFSlotCCW = iVSlot, fp, fn;

  JtDecode_DualVFMesh *pDstVFM = const_cast<JtDecode_DualVFMesh*> (_pDstVFM.operator->());
  IncModN (jFSlotCCW, pDstVFM->degree (iFace));
  DecModN (jFSlotCW, pDstVFM->degree (iFace));

  // Connect iVtx to iFace/iVSlot
  pDstVFM->setFaceVtx (iFace, iVSlot, iVtx);

  // Connect iVtx across the shared edge between iVtx and the vtx CW
  // from iVtx at iFace. Connect iVtx into the face at the other
  // end of this edge if it is not already connected there
  if ((fp = pDstVFM->vtx (iFace, jFSlotCW)) != -1)
  {
    int32_t ip = pDstVFM->findFaceSlot (fp, iFace);
    int32_t iVSlotCCW = jFSlot;
    IncModN (iVSlotCCW, pDstVFM->valence (iVtx));

    if (pDstVFM->face (iVtx, iVSlotCCW) == -1)
    {
      DecModN (ip, pDstVFM->valence (fp));
      pDstVFM->setVtxFace (iVtx, iVSlotCCW, pDstVFM->face (fp, ip));
    }
  }

  // Connect iVtx across the shared edge between iVtx and the vtx CCW
  // from iVtx at iFace. Connect iVtx into the face at the other
  // end of this edge if it is not already connected there.
  if ((fn = pDstVFM->vtx (iFace, jFSlotCCW)) != -1)
  {
    int32_t in = pDstVFM->findFaceSlot (fn, iFace);
    int32_t iVSlotCW = jFSlot;
    DecModN (iVSlotCW, pDstVFM->valence (iVtx));

    if (pDstVFM->face (iVtx, iVSlotCW) == -1)
    {
      IncModN (in, pDstVFM->valence (fn));
      pDstVFM->setVtxFace (iVtx, iVSlotCW, pDstVFM->face (fn, in));
    }
  }
}

bool JtDecode_MeshCodec::addActiveFace (int32_t iFace)
{
  _viActiveFaces.push_back (iFace);
  return true;
}

// Returns a face from the active queue to be completed. This needn't be the
// one at the end of the queue, because the choice of the next active face
// can affect how many SPLIT symbols are produced. This method employs a
// fairly simple scheme of searching the most recent 16 active faces for the
// first one with the smallest number of incomplete slots in its degree ring.
int32_t JtDecode_MeshCodec::nextActiveFace()
{
  int32_t iFace = -1;

  // Search the 16 face record at the end of the
  // queue for the one with lowest remaining degree
  while (_viActiveFaces.size() > 0 && _vbRemovedActiveFaces.Test (_viActiveFaces.back()))
    _viActiveFaces.pop_back();

  int32_t cLowestEmptyDegree = 9999999;
  int32_t i, cEmptyDeg;
  const int32_t cWidth = 16;

  JtDecode_DualVFMesh *pDstVFM = const_cast<JtDecode_DualVFMesh*> (_pDstVFM.operator->());
  for (i = static_cast<int32_t>(_viActiveFaces.size()) - 1;
       i >= Max (0, static_cast<int32_t>(_viActiveFaces.size()) - cWidth); --i)
  {
    int32_t iFace0 = _viActiveFaces[i];
    if (_vbRemovedActiveFaces.Test (iFace0))
    {
      _viActiveFaces.erase (_viActiveFaces.begin() + i); // TOXIC: O(N^2)
      continue;
    }

    cEmptyDeg = pDstVFM->emptyFaceSlots (iFace0);
    if (cEmptyDeg < cLowestEmptyDegree)
    {
      cLowestEmptyDegree = cEmptyDeg;
      iFace = iFace0;
    }
  }

  // Return the selected active face
  return iFace;
}

// Removes iFace from the active face queue.
void JtDecode_MeshCodec::removeActiveFace (int32_t iFace)
{
  _vbRemovedActiveFaces.Set (iFace);
}

// Searches the active face queue for iFace and returns
// its index position from the _end_ of the queue. This is
// needed by the ioFace() method when encoding a SPLIT
// symbol
int32_t JtDecode_MeshCodec::activeFaceOffset (int32_t iFace) const
{
  int32_t iOffset = -1;
  int32_t i, cLen = static_cast<int32_t>(_viActiveFaces.size());

  const int32_t* paiActiveFaces = &(_viActiveFaces.front());

  for (i = cLen - 1 ; i >= 0 ; --i)
  {
    if (paiActiveFaces[i] == iFace)
    {
      // The offset is how far FROM THE END of the active
      // face list we found iFace. This serves the make
      // the iOffset a much smaller number, which is better
      // for compression
      iOffset = cLen - i;
      break;
    }
  }

  return iOffset;
}

// This class implements the five abstract methods from
// MeshCodec to realize a mesh decoder.
class JtDecode_MeshDecoder : public JtDecode_MeshCodec
{
  // ========== Polymorphic I/O Interface ==========
  virtual int32_t ioVtxInit();
  virtual int32_t ioVtx       (int32_t iFace, int32_t iVSlot);
  virtual int32_t ioFace      (int32_t iVtx,  int32_t jFSlot);
  virtual int32_t ioSplitFace (int32_t iVtx,  int32_t jFSlot);
  virtual int32_t ioSplitPos  (int32_t iVtx,  int32_t jFSlot);

public:
  // ========== Housekeeping Interface ==========
  JtDecode_MeshDecoder (JtDecode_MeshCoderDriver *pTMC = NULL);
};

JtDecode_MeshDecoder::JtDecode_MeshDecoder (JtDecode_MeshCoderDriver *pTMC)
  : JtDecode_MeshCodec (pTMC)
{
  //
}

// Begins decoding a new connected mesh component by calling
// ioVtx() to read the next vertex from the symbol stream.
int32_t JtDecode_MeshDecoder::ioVtxInit()
{
  return ioVtx (-1, -1);
}

// Read a vertex valence symbol, vertex group number, and vertex
// flags from the input symbols stream. Create a new vertex
// on _pDstVFM with this data, and return the new vertex number.
// It is this method's responsibility to detect the end of
// the input symbol stream by returning -1 when that happens.
int32_t JtDecode_MeshDecoder::ioVtx (int32_t /*iFace*/, int32_t /*iVSlot*/)
{
  // Obtain a VERTEX VALENCE symbol
  int32_t eSym = _pTMC->_nextValSymbol();
  int32_t iVtx = -1;
  if (eSym > -1)
  {
    // Create a new vtxt on the VFMesh
    iVtx = _pDstVFM->numVts();
    int32_t iVtxVal = eSym;
    _pDstVFM->newVtx      (iVtx, iVtxVal);
    _pDstVFM->setVtxGrp   (iVtx, _pTMC->_nextFGrpSymbol());
    _pDstVFM->setVtxFlags (iVtx, static_cast<uint16_t> (_pTMC->_nextVtxFlagSymbol()));
  }

  return iVtx;
}

// Read a face degree symbol, and attribute mask bit
// vector, create a new DualVFMesh face, initialize the
// face attribute record numbers from a running counter,
// and return the new face number. If the degree symbol
// read from the input symbol stream is 0, signify this by
// returning -1.
int32_t JtDecode_MeshDecoder::ioFace (int32_t iVtx, int32_t /*jFSlot*/)
{
  // Obtain a FACE DEGREE symbol
  int32_t iCntxt = _pTMC->_faceCntxt (iVtx, const_cast<JtDecode_DualVFMesh*> (_pDstVFM.operator->()));
  int32_t eSym = _pTMC->_nextDegSymbol (iCntxt);
  int32_t iFace = -1;

  if (eSym != 0)
  {
    // Create a new face on the VFMesh
    iFace = _pDstVFM->numFaces();
    int32_t cDeg = eSym;
    int32_t nFaceAttrs = 0;

    if (cDeg <= JtDecode_DualVFMesh::cMBits)
    {
      uint64_t uAttrMask = _pTMC->_nextAttrMaskSymbol (/*iCntxt*/Min (7, Max (0, cDeg - 2)));

      for (uint64_t uMask = uAttrMask; uMask; nFaceAttrs += (uMask & 1), uMask >>= 1);

      _pDstVFM->newFace (iFace, cDeg, nFaceAttrs, uAttrMask);
    }
    else
    {
      JtDecode_BitVec vbAttrMask;
      _pTMC->_nextAttrMaskSymbol (&vbAttrMask, cDeg);

      for (int32_t i = 0 ; i < cDeg ; ++i)
      {
        if (vbAttrMask.Test (i))
          nFaceAttrs++;
      }

      _pDstVFM->newFace (iFace, cDeg, nFaceAttrs, &vbAttrMask, 0);
    }

    // Error check for a corrupt degree or attrmask
    if (nFaceAttrs > cDeg)
    {
      Standard_ASSERT_RETURN (nFaceAttrs <= cDeg, "Error", -1);
      return -2;
    }

    // Set up the face attributes
    for (int32_t iAttrSlot = 0 ; iAttrSlot < nFaceAttrs ; iAttrSlot++)
    {
      _pDstVFM->setFaceAttr (iFace, iAttrSlot, _iFaceAttrCtr++);
    }
  }

  return iFace;
}

// Consumes a split offset symbol from the SPLIT offset
// symbol stream, and determines the face number referenced
// by the offset. Returns the referenced face number.
int32_t JtDecode_MeshDecoder::ioSplitFace (int32_t /*iVtx*/, int32_t /*jFSlot*/)
{
  // Obtain a SPLITFACE symbol
  int32_t eSym = _pTMC->_nextSplitFaceSymbol();
  Standard_ASSERT_RETURN (eSym >= -1, "Error", -1);

  int32_t iFace = -1;
  if (eSym > -1)
  {
    // Use the offset to index into the active face queue
    // to determine the actual face number.
    int32_t iOffset = eSym;
    Standard_Size cLen = _viActiveFaces.size();

    Standard_ASSERT_RETURN (iOffset > 0 && iOffset <= static_cast<int32_t>(cLen), "Error", -1);
    iFace = _viActiveFaces[cLen - iOffset];
  }

  return iFace;
}

// Consumes a split position symbol from the associated symbol
// stream, and returns the vertex slot number on the current
// split face at which the topological split/merge occurred.
int32_t JtDecode_MeshDecoder::ioSplitPos (int32_t /*iVtx*/, int32_t /*jFSlot*/)
{
  // Obtain a SPLITVTX symbol
  int32_t eSym = _pTMC->_nextSplitPosSymbol();
  Standard_ASSERT_RETURN (eSym >= -1, "Error", -1);

  int32_t iVSlot = -1;

  if (eSym > -1)
  {
    // Return the vtx slot number
    iVSlot = eSym;
  }

  return iVSlot;
}

JtDecode_MeshCoderDriver::~JtDecode_MeshCoderDriver()
{
  delete _pMeshDecoder;
}

JtDecode_MeshCoderDriver::InputData::Handle
JtDecode_MeshCoderDriver::LoadInputData (JtData_Reader& theReader)
{
  InputData::Handle aData (new InputData);

  aData->modernMasks=theReader.Model()->MajorVersion()>=10;
  JtDecode_Int32CDP* streams[]={
    &aData->_viOutDegSyms[0],&aData->_viOutDegSyms[1],&aData->_viOutDegSyms[2],&aData->_viOutDegSyms[3],
    &aData->_viOutDegSyms[4],&aData->_viOutDegSyms[5],&aData->_viOutDegSyms[6],&aData->_viOutDegSyms[7],
    &aData->_vviOutValSyms,&aData->_viOutFGrpSyms,&aData->_vuOutFaceFlags,
    &aData->_vvuOutAttrMasks[0],&aData->_vvuOutAttrMasks[1],&aData->_vvuOutAttrMasks[2],&aData->_vvuOutAttrMasks[3],
    &aData->_vvuOutAttrMasks[4],&aData->_vvuOutAttrMasks[5],&aData->_vvuOutAttrMasks[6],&aData->_vvuOutAttrMasks[7],
    &aData->_faceAttributeMask8_30,&aData->_faceAttributeMask8_4,&aData->_vuOutAttrMasksLrg,
    &aData->_viOutSplitVtxSyms,&aData->_viOutSplitPosSyms};
  for (Standard_Size i = 0; i < 24; i++)
  {
    if(aData->modernMasks && i==20) continue;
    // High-degree masks are a raw VecU32 in both JT 9.5 (7.2.2.1)
    // and JT 10. Only the separate upper-four-bit stream differs.
    if (!(i==21 ? streams[i]->LoadPlain(theReader) : streams[i]->Load2 (theReader))) {
      throw std::runtime_error("Cannot read JT topology stream " + std::to_string(i) + " at " + std::to_string(theReader.GetPosition()));
      return (InputData::Handle)0;
    }
  }

  uint32_t aHash;
  if (!theReader.ReadU32 (aHash))
    return (InputData::Handle)0;

  return aData;
}

void JtDecode_MeshCoderDriver::SetInputData (InputData& theData)
{
  myModernMasks=theData.modernMasks;
  JtDecode_Int32CDPScheduler<100> aScheduler;

  aScheduler.Run (theData._vviOutValSyms        , _vviOutValSyms);
  aScheduler.Run (theData._viOutFGrpSyms        , _viOutFGrpSyms);
  aScheduler.Run (theData._vuOutFaceFlags       , _vuOutFaceFlags         , JtDecode_Unpack_Lag1);
  aScheduler.Run (theData._faceAttributeMask8_30, _faceAttributeMask8_30);
  if(!myModernMasks) aScheduler.Run (theData._faceAttributeMask8_4 , _faceAttributeMask8_4);
  aScheduler.Run (theData._vuOutAttrMasksLrg    , _vuOutAttrMasksLrg);
  aScheduler.Run (theData._viOutSplitVtxSyms    , _viOutSplitVtxSyms      , JtDecode_Unpack_Lag1);
  aScheduler.Run (theData._viOutSplitPosSyms    , _viOutSplitPosSyms);

  for (Standard_Size i = 0; i < 8; i++)
  {
    aScheduler.Run (theData._viOutDegSyms   [i], _viOutDegSyms   [i]);
    aScheduler.Run (theData._vvuOutAttrMasks[i], _vvuOutAttrMasks[i]);
  }

  aScheduler.Wait();
}

class JtDecode_MeshCoderDriver::decodeVFMesh
{
  JtDecode_DualVFMesh* myDualVFMesh;
  IndicesVec           myStartIndices;
  IndicesVec*          myVertexIndices;
  IndicesVec*          myNormalIndices;
  IndicesVec*          myFaceGroups;

public:
  decodeVFMesh (JtDecode_DualVFMesh* theDualVFMesh,
                IndicesVec*          theVertexIndices,
                IndicesVec*          theNormalIndices, IndicesVec* theFaceGroups)
    : myDualVFMesh    (theDualVFMesh)
    , myStartIndices  (theDualVFMesh->numVts())
    , myVertexIndices (theVertexIndices)
    , myNormalIndices (theNormalIndices)
    , myFaceGroups (theFaceGroups)
  {
    int32_t numFaces = myDualVFMesh->numVts();
    int32_t numIndices = 0;
    for (int32_t iFace = 0; iFace < numFaces; iFace++)
    {
      if (myDualVFMesh->vtxGrp (iFace) >= 0)
      {
        if(myDualVFMesh->valence(iFace)!=3)throw std::runtime_error("Unsupported JT non-triangular topological face");
        myStartIndices[iFace] = numIndices;
        numIndices += myDualVFMesh->valence (iFace);
      }
      else
        myStartIndices[iFace] = -1;
    }

    if (myVertexIndices) myVertexIndices->Allocate (numIndices);
    if (myNormalIndices) myNormalIndices->Allocate (numIndices);
    if (myFaceGroups) myFaceGroups->Allocate(numIndices/3);

    JtData_Parallel::For ((int32_t)0, numFaces, *this);
  }

  void operator() (int32_t iFace) const
  {
    int32_t curIndex = myStartIndices[iFace];
    if (curIndex < 0)
      return;

    if(myFaceGroups)(*myFaceGroups)[curIndex/3]=myDualVFMesh->vtxGrp(iFace);
    for (int32_t iVSlot = 0; iVSlot < myDualVFMesh->valence (iFace); iVSlot++)
    {
      int32_t vertexIndex = myDualVFMesh->face (iFace, iVSlot);

      if (myVertexIndices)
        (*myVertexIndices)[curIndex] = vertexIndex;

      if (myNormalIndices)
        (*myNormalIndices)[curIndex] = myDualVFMesh->vtxFaceAttr (iFace, vertexIndex);

      curIndex++;
    }
  }
};

//! Decodes the mesh
void JtDecode_MeshCoderDriver::Decode (IndicesVec* theVertexIndices, IndicesVec* theNormalIndices, IndicesVec* theFaceGroups)
{
  // Allocate a coder
  if (_pMeshDecoder == NULL)
  {
    _pMeshDecoder = new JtDecode_MeshDecoder (this);
  }

  // Reset the symbol counters
  memset (_iDegReadPos     , 0, 8 * sizeof (int32_t));
  memset (_iAttrMaskReadPos, 0, 8 * sizeof (int32_t));
  _iValReadPos = 0;
  _iVGrpReadPos = 0;
  _iFFlagReadPos = 0;
  _iAttrMaskLrgReadPos = 0;
  _iSplitFaceReadPos = 0;
  _iSplitPosReadPos = 0;

  // Run the decoder
  _pMeshDecoder->run();

  // Assert that ALL symbols have been consumed
  for (Standard_Integer i = 0; i < 8; ++i)
  {
    if (_iDegReadPos[i]      != _viOutDegSyms[i].Count() ||
        _iAttrMaskReadPos[i] != _vvuOutAttrMasks[i].Count())
    {
      Standard_ASSERT (false, "ERROR: Not all symbols have been consumed!", return);
    }
  }

  if (_iValReadPos         != _vviOutValSyms.Count() ||
      _iVGrpReadPos        != _viOutFGrpSyms.Count() ||
      _iFFlagReadPos       != _vuOutFaceFlags.Count() ||
      _iAttrMaskLrgReadPos != _vuOutAttrMasksLrg.Count() ||
      _iSplitFaceReadPos   != _viOutSplitVtxSyms.Count() ||
      _iSplitPosReadPos    != _viOutSplitPosSyms.Count())
  {
    Standard_ASSERT (false, "ERROR: Not all symbols have been consumed!", return);
  }

  // Decode output VFMesh
  decodeVFMesh (_pMeshDecoder->vfm(), theVertexIndices, theNormalIndices, theFaceGroups);
}

//! Next degree symbol
int32_t JtDecode_MeshCoderDriver::_nextDegSymbol (int32_t iCCntx)
{
  int32_t eSym = -1;
  if (_iDegReadPos[iCCntx] < _viOutDegSyms[iCCntx].Count())
  {
    eSym = _viOutDegSyms[iCCntx][_iDegReadPos[iCCntx]++];
  }

  return eSym;
}

//! Next value symbol
int32_t JtDecode_MeshCoderDriver::_nextValSymbol()
{
  int32_t eSym = -1;
  if (_iValReadPos < _vviOutValSyms.Count())
  {
    eSym = _vviOutValSyms[_iValReadPos++];
  }

  return eSym;
}

//! Next face group symbol
int32_t JtDecode_MeshCoderDriver::_nextFGrpSymbol()
{
  int32_t eSym = -1;
  if (_iVGrpReadPos < _viOutFGrpSyms.Count())
  {
    eSym = _viOutFGrpSyms[_iVGrpReadPos++];
  }

  return eSym;
}

//! Next vertex flag symbol
int32_t JtDecode_MeshCoderDriver::_nextVtxFlagSymbol()
{
  int eSym = 0;
  if (_iFFlagReadPos < _vuOutFaceFlags.Count())
  {
    eSym = _vuOutFaceFlags[_iFFlagReadPos++];
  }

  return eSym;
}

//! Next attr mask symbol
int64_t JtDecode_MeshCoderDriver::_nextAttrMaskSymbol (int32_t iCCntx)
{
  int64_t eSym = 0;
  int32_t readpos = _iAttrMaskReadPos[iCCntx];
  if (readpos < _vvuOutAttrMasks[iCCntx].Count())
  {
    eSym = _vvuOutAttrMasks[iCCntx][readpos];
  }

  if (iCCntx == 7)
  {
    if(myModernMasks) eSym = int64_t(uint64_t(uint32_t(eSym)) | (uint64_t(uint32_t(_faceAttributeMask8_30[readpos]))<<32));
    else eSym |= (((int64_t) _faceAttributeMask8_4 [readpos]) << 60) +
            (((int64_t) _faceAttributeMask8_30[readpos]) << 30);
  }

  _iAttrMaskReadPos[iCCntx]++;
  return eSym;
}

void JtDecode_MeshCoderDriver::_nextAttrMaskSymbol (JtDecode_BitVec* iopvbAttrMask, int cDegree)
{
  if (_iAttrMaskLrgReadPos < _vuOutAttrMasksLrg.Count())
  {
    iopvbAttrMask->SetLength (cDegree);
    uint32_t* pu = iopvbAttrMask->Data();
    int32_t nWords = (cDegree + JtDecode_BitVec::cWordBits - 1) >> JtDecode_BitVec::cBitsLog2;
    memcpy (pu, &_vuOutAttrMasksLrg[_iAttrMaskLrgReadPos], nWords * sizeof (uint32_t));
    _iAttrMaskLrgReadPos += nWords;
  }
  else
  {
    iopvbAttrMask->SetLength (0);
  }
}

//! Split face symbol
int32_t JtDecode_MeshCoderDriver::_nextSplitFaceSymbol()
{
  int32_t eSym = -1;
  if (_iSplitFaceReadPos < _viOutSplitVtxSyms.Count())
  {
    eSym = _viOutSplitVtxSyms[_iSplitFaceReadPos++];
  }

  return eSym;
}

//! Split position symbol
int32_t JtDecode_MeshCoderDriver::_nextSplitPosSymbol()
{
  int32_t eSym = -1;
  if (_iSplitPosReadPos < _viOutSplitPosSyms.Count())
  {
    eSym = _viOutSplitPosSyms[_iSplitPosReadPos++];
  }
  return eSym;
}

//! Computes a "compression context" from 0 to 7 inclusive for faces on vertex iVtx. The context
//! is based on the vertex's valence, and the total _known_ degree of already-coded faces on the
//! vertex at the time of the call.
int32_t JtDecode_MeshCoderDriver::_faceCntxt (int32_t iVtx, JtDecode_DualVFMesh* pVFM)
{
  // Here, we are going to gather data to be used to determine a
  // compression contest for the face degree.
  int32_t cVal = pVFM->valence (iVtx);
  int32_t nKnownFaces = 0;
  int32_t cKnownTotDeg = 0;

  for (int32_t i = 0; i < cVal; i++)
  {
    int32_t iTmpFace = pVFM->face (iVtx, i);
    if (!pVFM->isValidFace (iTmpFace))
    {
      continue;
    }

    nKnownFaces++;
    cKnownTotDeg += pVFM->degree (iTmpFace);
  }

  int32_t iCCntxt = 0;
  if (cVal == 3)
  {
    // Regular tristrip-like meshes tend to have degree 6 faces
    iCCntxt = (cKnownTotDeg < nKnownFaces * 6) ? 0 : (cKnownTotDeg == nKnownFaces * 6) ? 1 : 2;
  }
  else if (cVal == 4)
  {
    // Regular quadstrip-like meshes tend to have degree 4 faces
    iCCntxt = (cKnownTotDeg < nKnownFaces * 4) ? 3 : (cKnownTotDeg == nKnownFaces * 4) ? 4 : 5;
  }
  else if (cVal == 5)
  {
    // Pentagons are all lumped into context 6
    iCCntxt = 6;
  }
  else
  {
    // All other polygons are lumped into context 7
    iCCntxt = 7;
  }

  return iCCntxt;
}

