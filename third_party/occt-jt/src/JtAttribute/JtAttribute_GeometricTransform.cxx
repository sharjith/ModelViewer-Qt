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

#include <JtAttribute_GeometricTransform.hxx>
#include <JtData_Reader.hxx>

#include <Standard_Version.hxx>
#include <gp_Trsf.hxx>
#include <Precision.hxx>

IMPLEMENT_STANDARD_HANDLE (JtAttribute_GeometricTransform, JtAttribute_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_GeometricTransform, JtAttribute_Base)

IMPLEMENT_OBJECT_CLASS(JtAttribute_GeometricTransform, "Attribute Object",
                       "10dd1083-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

namespace
{
  // Identity matrix
  static const Standard_Real THE_IDENTITY[16] =
  {
    1., 0., 0., 0.,
    0., 1., 0., 0.,
    0., 0., 1., 0.,
    0., 0., 0., 1.
  };
}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtAttribute_GeometricTransform::Read (JtData_Reader& theReader)
{
  if (!JtAttribute_Base::Read (theReader))
    return Standard_False;

  Jt_I16 aVersion;
  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadVersion(aVersion))
    return Standard_False;

  Standard_Integer aStoredValuesMask;
  if (!theReader.ReadU16 (aStoredValuesMask))
    return Standard_False;

  Standard_Boolean (JtData_Reader::*ReadFunc) (Standard_Real&);
  if (theReader.Model()->MajorVersion() <= 8)
    ReadFunc = &JtData_Reader::ReadF32;
  else
    ReadFunc = &JtData_Reader::ReadF64;

  Standard_Integer aMask = 0x8000;
  for (Standard_Integer anIter = 0; anIter < 16; ++anIter, aMask >>= 1)
  {
    if (aStoredValuesMask & aMask)
    {
      if (!(theReader.*ReadFunc) (myTransform[anIter]))
        return Standard_False;
    }
    else
    {
      myTransform[anIter] = THE_IDENTITY[anIter];
    }
  }

  return Standard_True;
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtAttribute_GeometricTransform::Dump (Standard_OStream& theStream) const
{
  theStream << "JtAttribute_GeometricTransform ";
  return JtAttribute_Base::Dump (theStream);
}

//=======================================================================
//function : GetTrsf
//purpose  :
//=======================================================================
void JtAttribute_GeometricTransform::GetTrsf (gp_Trsf& theTrsf) const
{
  theTrsf.SetValues (myTransform[0], myTransform[4], myTransform[ 8], myTransform[12],
                     myTransform[1], myTransform[5], myTransform[ 9], myTransform[13],
                     myTransform[2], myTransform[6], myTransform[10], myTransform[14]
#if OCC_VERSION_HEX < 0x060800
                     , Precision::Angular(), 1.e-6
#endif
                     );
}
