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

// Modified for ModelViewer-Qt, 2026-10-19 (including changes made 2026-10-18).
// JT 9.0/9.5 qualification and shared attribute/codec bounds updated: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtAttribute_Material.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtAttribute_Material, JtAttribute_Base)
IMPLEMENT_STANDARD_RTTIEXT(JtAttribute_Material, JtAttribute_Base)

IMPLEMENT_OBJECT_CLASS(JtAttribute_Material, "Material Object",
                       "10dd1030-2ac8-11d1-9b-6b-00-80-c7-bb-59-97")

namespace
{
  //! Auxiliary function
  static Standard_Boolean readColor (JtData_Reader&   theReader,
                                     Jt_F32         (&theVector) [4],
                                     Standard_Boolean theIsSingleF32 = Standard_False)
  {
    if (!theIsSingleF32)
      return theReader.ReadArray (theVector);

    Jt_F32 aVal;
    if (!theReader.ReadF32 (aVal))
      return Standard_False;

    theVector[0] = aVal;
    theVector[1] = aVal;
    theVector[2] = aVal;
    theVector[3] = 1.0;

    return Standard_True;
  }
}

//=======================================================================
//function : JtAttribute_Material
//purpose  :
//=======================================================================
JtAttribute_Material::JtAttribute_Material()
  : myVersion (0), myReflectivity (0), myBumpiness (1) {}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtAttribute_Material::Read (JtData_Reader& theReader)
{
  if (!JtAttribute_Base::Read (theReader))
    return Standard_False;

  if (theReader.Model()->MajorVersion() > 8
  && !theReader.ReadVersion(myVersion))
    return Standard_False;

  if (theReader.Model()->MajorVersion() == 9 && myVersion != 1 && !(theReader.Model()->MinorVersion() == 5 && myVersion == 2)) throw std::runtime_error("Unsupported JT 9.x material attribute version");
  if (theReader.Model()->MajorVersion() >= 10 && myVersion != 1) throw std::runtime_error("Unsupported JT material attribute version");

  if (!theReader.ReadU16 (myDataFlags)
   || !readColor (theReader, myAmbientColor , (myDataFlags & 3) == 3)
   || !readColor (theReader, myDiffuseColor)
   || !readColor (theReader, mySpecularColor, (myDataFlags & 5) == 5)
   || !readColor (theReader, myEmissionColor, (myDataFlags & 9) == 9)
   || !theReader.ReadF32 (myShininess))
    return Standard_False;

  if ((myVersion == 2 || theReader.Model()->MajorVersion() >= 10)
  && !theReader.ReadF32 (myReflectivity))
    return Standard_False;

  if (theReader.Model()->MajorVersion() >= 10 && !theReader.ReadF32(myBumpiness)) return Standard_False;

  if (!ReadFieldsV2(theReader)) return Standard_False;

  // Public JT 9.5 Rev-A section 7.2.1.1.2.2 retains the older combined
  // diffuse/alpha inhibit bit and adds separate bits 7/8. JT 10 reorders
  // these fields. Normalize to the scene adapter's JT 10 field numbering.
  // Keep generic attribute flags unchanged: this mapping is material-specific.
  if (theReader.Model()->MajorVersion() < 10)
  {
    const Jt_U32 raw = myFieldInhibitFlags;
    myFieldInhibitFlags = (raw & 1u) | ((raw & 0x1fcu) >> 1)
                       | ((raw & 2u) ? 0xc0u : 0u);
  }
  return Standard_True;
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtAttribute_Material::Dump (Standard_OStream& theStream) const
{
  theStream << "JtAttribute_Material ";
  return JtAttribute_Base::Dump (theStream);
}
