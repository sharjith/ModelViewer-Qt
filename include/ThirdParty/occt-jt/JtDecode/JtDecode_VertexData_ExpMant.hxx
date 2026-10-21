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
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#ifndef _JtDecode_VertexData_ExpMant_HeaderFile
#define _JtDecode_VertexData_ExpMant_HeaderFile

#include <JtDecode_VertexData.hxx>

//! Decoder performing exp mant decoding.
class JtDecode_VertexData_ExpMant : public JtDecode_VertexData
{
public:
  //! Constructor.
  Standard_EXPORT JtDecode_VertexData_ExpMant (
    const Standard_Size theNbComponents,
    JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null);

protected:
  //! Get expected count of output components.
  Standard_EXPORT virtual Standard_Integer getOutCompCount (Standard_Size thePackageCount);

  //! Decoding method.
  Standard_EXPORT virtual void decode (Decoded::Ref theResults);
};

#endif
