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

#include <JtData_Inflate.hxx>

#define min(a, b) ((a) < (b) ? (a) : (b))

//=======================================================================
//function : JtData_Inflate
//purpose  : Constructor
//=======================================================================

JtData_Inflate::JtData_Inflate (JtData_Reader& theReader, const Standard_Size theLength)
  : JtData_Reader (theReader.Model())
  , myReader      (&theReader)
  , myInBuffer    (0L)
  , myInputRest   (theLength)
  , myOutBufPos   (myOutBuffer)
  , myOutBufRest  (0)
{
  myZStream.avail_in  = 0;
  myZStream.next_in   = Z_NULL;
  myZStream.zalloc    = Z_NULL;
  myZStream.zfree     = Z_NULL;
  myZStream.opaque    = Z_NULL;

  inflateInit (&myZStream);
}

//=======================================================================
//function : ~JtData_Inflate
//purpose  : Destructor
//=======================================================================

JtData_Inflate::~JtData_Inflate()
{
  inflateEnd (&myZStream);
  myReader->Unload (myInBuffer);
}

//=======================================================================
//function : ReadBytes
//purpose  : Buffered inflate raw bytes
//=======================================================================

Standard_Boolean JtData_Inflate::ReadBytes (void* theBuffer, Standard_Size theLength)
{
  // Do special actions if more data is needed than remains in the internal output buffer
  if (theLength > myOutBufRest)
  {
    // first, pick up the data remained in the buffer if any
    memcpy (theBuffer, myOutBufPos, myOutBufRest);

    // then, read the rest of the needed data
    theBuffer = reinterpret_cast <Bytef*>(theBuffer) + myOutBufRest;
    theLength =           theLength  - myOutBufRest;

    // if the data amount to read is large enough,
    // leave the internal buffer empty and read the data directly to the external buffer
    if (theLength > sizeof (myOutBuffer) / 2)
    {
      myOutBufRest = 0;
      return read (static_cast <Bytef*> (theBuffer), theLength) == theLength;
    }

    // load the internal buffer
    myOutBufPos  = myOutBuffer;
    myOutBufRest = read (myOutBuffer, sizeof (myOutBuffer));
    if (!myOutBufRest)
      return Standard_False;

    // go down and copy the needed data remainder from the internal output buffer
  }

  // Copy the needed data (or its remainder) from the internal output buffer
  if(theLength>myOutBufRest) return Standard_False;
  memcpy (theBuffer, myOutBufPos, theLength);
  myOutBufPos  += theLength;
  myOutBufRest -= theLength;
  return Standard_True;
}

//=======================================================================
//function : SkipBytes
//purpose  : Skip some bytes
//=======================================================================

Standard_Boolean JtData_Inflate::SkipBytes (Standard_Size theLength)
{
  while (theLength > myOutBufRest)
  {
    theLength   -= myOutBufRest;
    myOutBufPos  = myOutBuffer;
    myOutBufRest = read (myOutBuffer, sizeof (myOutBuffer));
    if (!myOutBufRest)
      return Standard_False;
  }

  myOutBufPos  += theLength;
  myOutBufRest -= theLength;
  return Standard_True;
}

//=======================================================================
//function : GetPosition
//purpose  : Get absolute reading position in the inflated data
//=======================================================================

Standard_Size JtData_Inflate::GetPosition() const
{
  return myZStream.total_out - myOutBufRest;
}

//=======================================================================
//function : read
//purpose  : unbuffered inflate
//=======================================================================

Standard_Size JtData_Inflate::read (Bytef* theBuffer, Standard_Size theLength)
{
  if(myStreamEnded) return 0;
  if(theLength>256u*1024u*1024u) throw std::runtime_error("JT inflate request exceeds limit");
  myZStream.next_out  = theBuffer;
  myZStream.avail_out = static_cast<unsigned>(theLength);

  // Run inflate() on input until the input exceeds or the output buffer is full
  while (myZStream.avail_out > 0)
  {
    JtData_Limits::poll();
    if(myZStream.total_out>512u*1024u*1024u) throw std::runtime_error("JT inflated segment exceeds limit");
    // load next piece of input data if needed
    if (!myInBuffer)
    {
      if (!myInputRest)
        break;

      Standard_Size aBufSize = min (CHUNK, myInputRest);
      myInBuffer = static_cast <const Bytef*> (myReader->Load (aBufSize));
      if (!myInBuffer)
        return 0;

      myZStream.next_in  = const_cast <Bytef*> (myInBuffer);
      myZStream.avail_in = static_cast<unsigned>(aBufSize);
      myInputRest       -= aBufSize;
    }

    // decompress the loaded data
    int ret = inflate (&myZStream, Z_NO_FLUSH);
    if(ret==Z_STREAM_END) {myStreamEnded=true;break;}

    // free the input buffer if it is exhausted
    if (myZStream.avail_in == 0)
    {
      myReader->Unload (myInBuffer);
      myInBuffer = 0L;
    }

    // return 0 in case of a failure
    switch (ret)
    {
    case Z_NEED_DICT:
    case Z_STREAM_ERROR:
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
    case Z_BUF_ERROR:
      return 0;
    }
  }

  return theLength - myZStream.avail_out;
}

bool JtData_Inflate::Finish(bool allowSingleZeroPadding)
{
  if(myOutBufRest) return false;
  Bytef extra=0;
  if(!myStreamEnded && read(&extra,1)!=0) return false;
  if(myStreamEnded && allowSingleZeroPadding && myInputRest+myZStream.avail_in==1) {
    if(myZStream.avail_in==1) return *myZStream.next_in==0;
    Jt_U8 padding=1;return myReader->ReadU8(padding) && padding==0;
  }
  return myStreamEnded && myInputRest==0 && myZStream.avail_in==0;
}
