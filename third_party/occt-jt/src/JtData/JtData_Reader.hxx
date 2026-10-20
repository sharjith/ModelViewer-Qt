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
// Additional bounded palette/face-group qualification changes: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#ifndef _JtData_Reader_HeaderFile
#define _JtData_Reader_HeaderFile
#include <limits>

#include <JtData_ReaderInterface.hxx>
#include <JtData_ByteSwap.hxx>

//! Reader provides low level reading operation.
class JtData_Reader : public JtData_ReaderInterface<JtData_Reader>
{
public:
  //! Reader class incapsulating a pointer to JtData_Reader.
  //! This is a static virtual class.
  //! It should be used as a parent class in the following way:
  //! class Derived : public JtData_Reader::Wrapper<Derived>.
  template <class Derived>
  class Wrapper : public JtData_ReaderInterface<Derived>
  {
  public:
    Wrapper (JtData_Reader& theReader) : myReader (&theReader) {}

    operator       JtData_Reader&()       { return *myReader; }
    operator const JtData_Reader&() const { return *myReader; }

  private:
    JtData_Reader* myReader;
  };

public:
  //! Constructor.
  Standard_EXPORT JtData_Reader (const Handle(JtData_Model)& theModel);

  //! Destructor.
  Standard_EXPORT virtual ~JtData_Reader();

  //! Read raw bytes from the stream.
  Standard_EXPORT virtual Standard_Boolean ReadBytes (void* theBuffer, Standard_Size theLength) = 0;

  //! Skip some bytes.
  Standard_EXPORT virtual Standard_Boolean SkipBytes (Standard_Size theLength) = 0;

  //! Get absolute reading position.
  Standard_EXPORT virtual Standard_Size GetPosition() const = 0;

  bool IsExternallyCompressed() const {return myExternallyCompressed;}
  void SetExternallyCompressed(bool value) {myExternallyCompressed=value;}

  //! Remaining bytes when decoding a bounded element payload.
  virtual Standard_Size ElementBytesRemaining() const {return std::numeric_limits<Standard_Size>::max();}

  //! Get the associated model.
  const Handle(JtData_Model)& Model() const { return myModel; }

  //! Read a primitive value from the stream.
  template <class Type>
  Standard_Boolean ReadPrimitiveValue (Type& theValue)
  {
    if (!ReadBytes (&theValue, sizeof (Type)))
      return Standard_False;

    if (myNeedSwap)
      ByteSwap (theValue);

    return Standard_True;
  }

  //! Specialization for signed byte.
  Standard_Boolean ReadPrimitiveValue (int8_t& theValue)
    { return ReadBytes (&theValue, sizeof (int8_t)); }

  //! Specialization for unsigned byte.
  Standard_Boolean ReadPrimitiveValue (uint8_t& theValue)
    { return ReadBytes (&theValue, sizeof (uint8_t)); }

  //! Read given number of primitive values all having the same type.
  template <class Type, class SizeT>
  Standard_Boolean ReadPrimitiveArray (Type* theArray, const SizeT theLength)
  {
    if (theLength < 0 || size_t(theLength) > size_t(256u*1024u*1024u)/sizeof(Type))
      throw std::runtime_error("Invalid JT primitive array length");
    if (!ReadBytes (theArray, theLength * sizeof (Type)))
      return Standard_False;

    if (myNeedSwap)
      for (SizeT i = 0; i < theLength; i++)
        ByteSwap (theArray[i]);

    return Standard_True;
  }

  //! Specialization for array of signed bytes.
  template <class SizeT>
  Standard_Boolean ReadPrimitiveArray (int8_t* theArray, const SizeT theLength)
    { return ReadBytes (theArray, theLength); }

  //! Specialization for array of unsigned bytes.
  template <class SizeT>
  Standard_Boolean ReadPrimitiveArray (uint8_t* theArray, const SizeT theLength)
    { return ReadBytes (theArray, theLength); }

  //! Load/map raw bytes from the stream to memory
  //! and return a pointer to access the data.
  const void* LoadBytes (const Standard_Size theLength);

  //! Free/unmap memory allocated/mapped by the Load method.
  void UnloadBytes (const void* theAddress);

  //! Read a 1-byte character string.
  Standard_Boolean ReadSbString (TCollection_AsciiString& theString);

  //! Read a 2-byte character string.
  Standard_Boolean ReadMbString (TCollection_ExtendedString& theString);

protected:
  Handle(JtData_Model) myModel;
  Standard_Boolean     myNeedSwap;
  bool myExternallyCompressed=false;
};

#endif // _JtData_Reader_HeaderFile
