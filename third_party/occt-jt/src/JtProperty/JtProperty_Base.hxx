// JT format reading and visualization tools
// Copyright (C) 2013-2015 OPEN CASCADE SAS
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

#ifndef _JtProperty_Base_HeaderFile
#define _JtProperty_Base_HeaderFile

#include <JtData_Object.hxx>

class JtProperty_Base : public JtData_Object
{
public:
  //! Default constructor.
  Standard_EXPORT JtProperty_Base();

  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  DEFINE_STANDARD_RTTIEXT(JtProperty_Base, JtData_Object)
  DEFINE_OBJECT_CLASS (JtProperty_Base)

protected:
  Jt_I16 myVersion;
  Jt_U32 myStateFlags;
};

DEFINE_STANDARD_HANDLE(JtProperty_Base, JtData_Object)

#endif // _JtProperty_Base_HeaderFile
