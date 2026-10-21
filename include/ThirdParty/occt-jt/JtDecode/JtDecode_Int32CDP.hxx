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

#ifndef _JtDecode_Int32CDP_HeaderFile
#define _JtDecode_Int32CDP_HeaderFile

#include <JtData_Types.hxx>
#include <JtData_SingleHandle.hxx>
#include <JtData_Reader.hxx>
#include <JtDecode_Unpack.hxx>

//! Class performing decoding of the Jt file data using defined codecs.
class JtDecode_Int32CDP
{
public:
  Standard_EXPORT Standard_Boolean LoadPlain(JtData_Reader& reader);
  //! Abstract interface for decoders.
  class EncodedData
  {
  public:
    typedef JtData_Vector<int32_t, int32_t> Decoded;
    typedef JtData_Vector<uint32_t, int32_t> DecodedU;

    //! Return the number of decoded values.
    Standard_EXPORT virtual int32_t GetOutValCount() const = 0;

    //! Perform decoding.
    Standard_EXPORT virtual Decoded::Mover Decode() = 0;

    //! Destructor.
    Standard_EXPORT virtual ~EncodedData() {};

  protected:
    static Decoded::Mover decodePackage
      (JtDecode_Int32CDP& thePackage, JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
    { return thePackage.decode (theUnpacker); }
  };

  typedef Standard_Boolean (JtDecode_Int32CDP::* LoadFnPtr) (JtData_Reader& theReader);

protected:
  typedef EncodedData::Decoded DecodedData;
  typedef EncodedData::DecodedU DecodedDataU;

  struct DecodingFunctorData
  {
    JtDecode_Int32CDP * myPackage;
    JtDecode_Unpack   * myUnpacker;
    DecodedData       * myResult;
  };

public:
  //! Load CDP encoded data from the reader.
  Standard_Boolean Load1 (JtData_Reader& theReader)
  {
    myEncodedData = loadCDP1 (theReader);
    return myEncodedData.operator bool();
  }

  //! Load CDP2 encoded data from the reader.
  Standard_Boolean Load2 (JtData_Reader& theReader)
  {
    myEncodedData = loadCDP2 (theReader);
    return myEncodedData.operator bool();
  }

  //! Get expected count of output values.
  //! Can be called only before calling Decode.
  Jt_I32 GetOutValCount() const
  {
    return myEncodedData->GetOutValCount();
  }

  //! Decode the loaded I32 data. Can be called only once for an instance.
  Jt_VecI32::Mover DecodeI32 (JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    DecodedData aDecodedData (decode (theUnpacker));
    return aDecodedData.Move();
  }

  //! Decode the loaded U32 data. Can be called only once for an instance.
  Jt_VecI32::Mover DecodeU32 (JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    DecodedData aDecodedData (decode (theUnpacker));
    return aDecodedData.Move();
  }

  //! A decoding functor. Can be called only once for an instance.
  class DecodingFunctor : protected DecodingFunctorData
  {
  public:
    void operator ()() const { *myResult = myPackage->decode (*myUnpacker); }
  };

  //! Create I32 decoding functor.
  DecodingFunctor GetDecodingFunctor (Jt_VecI32& theResult,
                                      JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    DecodingFunctorData aData = {this, &theUnpacker, reinterpret_cast<DecodedData*> (&theResult)};
    return reinterpret_cast <DecodingFunctor&> (aData);
  }

  //! Create U32 decoding functor.
  DecodingFunctor GetDecodingFunctor (Jt_VecU32& theResult,
                                      JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    DecodingFunctorData aData = {this, &theUnpacker, reinterpret_cast<DecodedData*> (&theResult)};
    return reinterpret_cast <DecodingFunctor&> (aData);
  }

protected:
  typedef JtData_SingleHandle<EncodedData> EncodedDataHandle;

protected:
  Standard_EXPORT static EncodedDataHandle loadCDP1 (JtData_Reader& theReader);
  Standard_EXPORT static EncodedDataHandle loadCDP2 (JtData_Reader& theReader);


  DecodedData::Mover decode (JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    if (!myEncodedData)
      return DecodedData().Move();

    DecodedData aDecodedData (myEncodedData->Decode());
    myEncodedData = 0;
    theUnpacker (aDecodedData);

    return aDecodedData.Move();
  }

protected:
  EncodedDataHandle myEncodedData;
};

#endif
