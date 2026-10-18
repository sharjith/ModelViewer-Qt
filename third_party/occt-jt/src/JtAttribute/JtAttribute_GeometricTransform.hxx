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

#ifndef _JtAttribute_GeometricTransform_HeaderFile
#define _JtAttribute_GeometricTransform_HeaderFile

#include <JtAttribute_Base.hxx>

class gp_Trsf;

//! Class representing Jt transformation attribute.
class JtAttribute_GeometricTransform : public JtAttribute_Base
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Return transformation.
  Standard_EXPORT void GetTrsf (gp_Trsf& theTrsf) const;

  //! Return transformationy.
  const Standard_Real* GetTrsf() const { return myTransform; }

  DEFINE_STANDARD_RTTIEXT(JtAttribute_GeometricTransform, JtAttribute_Base)
  DEFINE_OBJECT_CLASS (JtAttribute_GeometricTransform)

protected:
  Standard_Real myTransform[16];
};

DEFINE_STANDARD_HANDLE(JtAttribute_GeometricTransform, JtAttribute_Base)

#endif // _JtAttribute_GeometricTransform_HeaderFile
