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

#ifndef _JtData_Inflate_HeaderFile
#define _JtData_Inflate_HeaderFile

#include <JtData_Reader.hxx>

#include <zlib.h>

//! Class performing unpacking of data using zlib.
class JtData_Inflate : public JtData_Reader
{
public:
  //! Constructor.
  Standard_EXPORT JtData_Inflate (JtData_Reader& theReader, const Standard_Size theLength);

  //! Destructor.
  Standard_EXPORT virtual ~JtData_Inflate();

  //! Buffered inflate raw bytes.
  Standard_EXPORT virtual Standard_Boolean ReadBytes (void* theBuffer, Standard_Size theLength);

  //! Skip some bytes.
  Standard_EXPORT virtual Standard_Boolean SkipBytes (Standard_Size theLength);

  //! Get absolute reading position in the inflated data.
  Standard_EXPORT virtual Standard_Size GetPosition() const;

protected:
  Standard_Size read (Bytef* theBuffer, Standard_Size theLength);

protected:
  static const Standard_Size CHUNK = 32768;

  JtData_Reader* myReader;
  z_stream       myZStream;

  const Bytef*   myInBuffer;
  Standard_Size  myInputRest;

  Bytef          myOutBuffer[CHUNK];
  const Bytef*   myOutBufPos;
  Standard_Size  myOutBufRest;
};

#endif // _JtData_Inflate_HeaderFile
