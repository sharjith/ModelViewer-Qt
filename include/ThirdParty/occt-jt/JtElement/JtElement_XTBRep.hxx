// JT format reading and visualization tools
// Copyright (C) 2001-2015 OPEN CASCADE SAS
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

#ifndef _JtElement_XTBRep_HeaderFile
#define _JtElement_XTBRep_HeaderFile

#include <JtData_Object.hxx>

class JtElement_XTBRep : public JtData_Object
{
public:
  //! Default constructor.
  JtElement_XTBRep() : myVersion (0), myPKMajorVersion (0), myPKMinorVersion (0), myPKBuild (0) {}

  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader &theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& S) const;

  const Jt_String& Data() const { return myData; }

  void ClearData() { myData.Free(); }

  DEFINE_STANDARD_RTTIEXT(JtElement_XTBRep, JtData_Object)
  DEFINE_OBJECT_CLASS (JtElement_XTBRep)

protected:
  Jt_I32 myVersion;
  Jt_I32 myPKMajorVersion; //!< Parasolid Kernel Major Version
  Jt_I32 myPKMinorVersion; //!< Parasolid Kernel Minor Version
  Jt_I32 myPKBuild;        //!< Parasolid Kernel Build Number
  Jt_String myData;        //!< Parasolid Binary Data
};

DEFINE_STANDARD_HANDLE(JtElement_XTBRep, JtData_Object)

#endif // _JtElement_XTBRep_HeaderFile
