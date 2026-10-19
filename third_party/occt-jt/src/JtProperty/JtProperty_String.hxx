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

#ifndef _JtProperty_String_HeaderFile
#define _JtProperty_String_HeaderFile

#include <JtProperty_Base.hxx>
#include <TCollection_ExtendedString.hxx>

class JtProperty_String : public JtProperty_Base
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Get the property value.
  const TCollection_ExtendedString& Value() const { return myValue; }

  DEFINE_STANDARD_RTTIEXT(JtProperty_String, JtProperty_Base)
  DEFINE_OBJECT_CLASS (JtProperty_String)

protected:
  TCollection_ExtendedString myValue;
};

DEFINE_STANDARD_HANDLE(JtProperty_String, JtProperty_Base)

#endif // _JtProperty_String_HeaderFile
