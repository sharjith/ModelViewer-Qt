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

#include <JtData_FileReader.hxx>

//=======================================================================
//function : JtData_FileReader
//purpose  : Constructor
//=======================================================================

JtData_FileReader::JtData_FileReader (std::ifstream&                theFile,
                                      const Handle(JtData_Model)&   theModel,
                                      const std::ifstream::pos_type theOffset)
  : JtData_Reader (theModel)
  , myFile        (&theFile)
{
  const auto saved=myFile->tellg();myFile->seekg(0,std::ios::end);
  const auto end=myFile->tellg();if(saved<0 || end<0) throw std::runtime_error("Invalid JT input stream");
  myEnd=Standard_Size(end);myFile->seekg(saved);
  if (theOffset != std::ifstream::pos_type (-1))
  {
    if(theOffset<0 || Standard_Size(theOffset)>myEnd) throw std::runtime_error("JT offset outside file");
    myFile->seekg (theOffset);
  }
}

//=======================================================================
//function : ReadBytes
//purpose  : Read raw bytes from the stream
//=======================================================================

Standard_Boolean JtData_FileReader::ReadBytes (void* theBuffer, Standard_Size theLength)
{
  JtData_Limits::poll();
  const auto position=GetPosition();if(position>myEnd || theLength>myEnd-position) return Standard_False;
  myFile->read (reinterpret_cast<char*> (theBuffer), theLength);
  return bool(*myFile) && Standard_Size(myFile->gcount())==theLength;
}

//=======================================================================
//function : SkipBytes
//purpose  : Skip some bytes
//=======================================================================

Standard_Boolean JtData_FileReader::SkipBytes (Standard_Size theLength)
{
  JtData_Limits::poll();
  const auto position=GetPosition();if(position>myEnd || theLength>myEnd-position) return Standard_False;
  myFile->seekg (theLength, std::ios_base::cur);
  return bool(*myFile);
}

//=======================================================================
//function : GetPosition
//purpose  : Get absolute reading position
//=======================================================================

Standard_Size JtData_FileReader::GetPosition() const
{
  return static_cast <Standard_Size> (myFile->tellg());
}
