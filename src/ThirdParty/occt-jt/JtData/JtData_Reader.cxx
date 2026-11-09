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
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtData_Reader.hxx>

//=======================================================================
//function : JtData_Reader
//purpose  : Constructor
//=======================================================================

JtData_Reader::JtData_Reader (const Handle(JtData_Model)& theModel)
  : myModel    (theModel)
  , myNeedSwap (theModel->IsFileLE() != JtData_Model::IsLittleEndianHost) {}

//=======================================================================
//function : ~JtData_Reader
//purpose  : Destructor
//=======================================================================

JtData_Reader::~JtData_Reader() {}

//=======================================================================
//function : LoadBytes
//purpose  : Load/map raw bytes from the stream to memory
//=======================================================================

const void* JtData_Reader::LoadBytes (const Standard_Size theLength)
{
  JtData_Limits::allocation(theLength);
  void* aBuffer = Standard::Allocate (theLength);
  if (!aBuffer)
    return 0L;

  if (!Read (aBuffer, theLength))
  {
    Standard::Free (aBuffer);
    return 0L;
  }

  return aBuffer;
}

//=======================================================================
//function : UnloadBytes
//purpose  : Free/unmap memory allocated/mapped by the Load method
//=======================================================================

void JtData_Reader::UnloadBytes (const void* theAddress)
{
  if (theAddress)
    Standard::Free (const_cast <void*&> (theAddress));
}

//=======================================================================
//function : ReadSbString
//purpose  : Read a 1-byte character string
//=======================================================================

Standard_Boolean JtData_Reader::ReadSbString (TCollection_AsciiString& theString)
{
  Jt_I32 aCount;
  if (!ReadI32 (aCount) || aCount<0 || aCount>1000000)
    return Standard_False;

  Standard_Character* aStrBuffer = new Standard_Character[aCount + 1];
  if (!Read (aStrBuffer, aCount)) {delete[] aStrBuffer;return Standard_False;}
  aStrBuffer[aCount] = '\0';
  theString = aStrBuffer;
  delete[] aStrBuffer;

  return Standard_True;
}

//=======================================================================
//function : ReadMbString
//purpose  : Read a 2-byte character string
//=======================================================================

Standard_Boolean JtData_Reader::ReadMbString (TCollection_ExtendedString& theString)
{
  Jt_I32 aCount;
  if (!ReadI32 (aCount) || aCount<0 || aCount>1000000)
    return Standard_False;

  Standard_ExtCharacter* aStrBuffer = new Standard_ExtCharacter[aCount + 1];
  if (!ReadArray (reinterpret_cast<uint16_t*>(aStrBuffer), aCount)) {delete[] aStrBuffer;return Standard_False;}
  aStrBuffer[aCount] = '\0';
  theString = aStrBuffer;
  delete[] aStrBuffer;

  return Standard_True;
}
