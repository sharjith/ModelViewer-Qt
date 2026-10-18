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

#ifndef _JtData_FileReader_HeaderFile
#define _JtData_FileReader_HeaderFile

#include <JtData_Reader.hxx>
#include <fstream>

class JtData_FileReader : public JtData_Reader
{
public:
  //! Constructor.
  Standard_EXPORT JtData_FileReader (std::ifstream&                theFile,
                                     const Handle(JtData_Model)&   theModel,
                                     const std::ifstream::pos_type theOffset = -1);

  //! Read raw bytes from the stream.
  Standard_EXPORT virtual Standard_Boolean ReadBytes (void* theBuffer, Standard_Size theLength);

  //! Skip some bytes.
  Standard_EXPORT virtual Standard_Boolean SkipBytes (Standard_Size theLength);

  //! Get absolute reading position.
  Standard_EXPORT virtual Standard_Size GetPosition() const;
  void Limit(Standard_Size end) { if(end>myEnd || end<GetPosition()) throw std::runtime_error("JT segment lies outside file");myEnd=end; }
  Standard_Size End() const {return myEnd;}

protected:
  std::ifstream* myFile;
  Standard_Size myEnd=0;
};

#endif // _JtData_FileReader_HeaderFile
