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
// JT 10.5/10.7/10.8/10.10 shared modern-family qualification: 2026-10-20.
// JT 9.0/9.5 qualification and shared attribute/codec bounds updated: 2026-10-20.
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see third_party/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtData_Model.hxx>
#include <JtData_FileReader.hxx>
#include <JtData_Inflate.hxx>
#include <JtData_BufferReader.hxx>
#include <memory>
#include <set>

#include <JtData_Message.hxx>

#include <JtNode_Partition.hxx>
#include <JtProperty_LateLoaded.hxx>
#include <JtProperty_String.hxx>
#include <JtAttribute_Base.hxx>
#include <limits>

#include <Image_PixMap.hxx>

#include <NCollection_DataMap.hxx>
#include <TCollection_AsciiString.hxx>
#include <TColStd_SequenceOfInteger.hxx>

const Standard_Boolean JtData_Model::IsLittleEndianHost =
  Image_PixMap::IsBigEndianHost() ? Standard_False : Standard_True;

static const Jt_GUID EOEMarkerGUID ("ffffffff-ffff-ffff-ff-ff-ff-ff-ff-ff-ff-ff");

IMPLEMENT_STANDARD_HANDLE (JtData_Model, Standard_Transient)
IMPLEMENT_STANDARD_RTTIEXT(JtData_Model, Standard_Transient)

//=======================================================================
//function : JtData_Model
//purpose  : Constructor
//=======================================================================
JtData_Model::JtData_Model (const TCollection_ExtendedString& theFileName,
                            const Handle(JtData_Model)&       theParent)
: myParent   (theParent)
, myFileName (theFileName)
, myIsFileLE (Standard_False)
, myMajorVersion (0)
, myMinorVersion (0) {}

//=======================================================================
//function : Init
//purpose  : Read JT file header, TOC and LSG and return a handle to the root LSG node
//=======================================================================
Handle(JtNode_Partition) JtData_Model::Init()
{
  // Open the file
  std::ifstream aFile;
  if (!open (aFile))
  {
    ALARM ("Error: Failed to open Jt file");
    return Handle(JtNode_Partition)();
  }

  // Read File Header

  // Read version
  char vstr[81] = {};
  aFile.read (vstr, 80);

  TCollection_AsciiString aMajorVersionStr (vstr);
  Standard_Integer aPos = aMajorVersionStr.Search ("Version");
  if (aPos < 1)
  {
    ALARM ("Error: Failed to read Jt file version");
    return Handle(JtNode_Partition)();
  }

  aMajorVersionStr = aMajorVersionStr.Token (" \t", 2);
  aPos = aMajorVersionStr.Search (".");
  if (aPos <= 1)
  {
    ALARM ("Error: Failed to read Jt file version");
    return Handle(JtNode_Partition)();
  }

  TCollection_AsciiString aMinorVersionStr = aMajorVersionStr.Split (aPos - 1);
  aMinorVersionStr.Remove (1);
  if (!aMajorVersionStr.IsIntegerValue()
   || !aMinorVersionStr.IsIntegerValue())
  {
    ALARM ("Error: Failed to read Jt file version");
    return Handle(JtNode_Partition)();
  }

  myMajorVersion = aMajorVersionStr.IntegerValue();
  myMinorVersion = aMinorVersionStr.IntegerValue();
  // Qualified 8.x lossless layout family; geometry codecs are checked separately.
  // Qualified 9.0/9.5 share the transitional decoder; local versions remain checked.
  // The observed 10.9 palette family is qualified independently of later versions.
  const bool legacy8Family=myMajorVersion==8 && myMinorVersion>=0 && myMinorVersion<=2;
  if(!((myMajorVersion==10 && (myMinorVersion==0 || myMinorVersion==3 || myMinorVersion==5 || myMinorVersion==7 || myMinorVersion==8 || myMinorVersion==9 || myMinorVersion==10)) || legacy8Family || (myMajorVersion==9 && (myMinorVersion==0 || myMinorVersion==5))))
    throw std::runtime_error("Unsupported JT version "+std::to_string(myMajorVersion)+"."+
      std::to_string(myMinorVersion)+"; native reader supports qualified JT 8.0-8.2, JT 9.0, JT 9.5, JT 10.0, JT 10.3, JT 10.5, JT 10.7, JT 10.8, JT 10.9 and JT 10.10 subsets");
  TRACE ("\n\nInfo: File version = " + myMajorVersion + "." + myMinorVersion);

  // Read byte order
  char aByteOrder;
  aFile.read (&aByteOrder, 1);
  if(!aFile || (aByteOrder!=0 && aByteOrder!=1)) throw std::runtime_error("Invalid JT byte order");
  myIsFileLE = (aByteOrder == 0);
  TRACE ("Info: Byte order = " + (myIsFileLE ? "LE" : "BE"));

  // Read reserved field, TOC offset, LSG Segment ID
  Jt_I32 aReservedField;
  Jt_I64 aTOCOffset;
  Jt_GUID anLSGSegmentGUID;
  {
    JtData_FileReader aReader (aFile, this);
    if (!aReader.ReadI32 (aReservedField)
     || !(myMajorVersion >= 10 ? aReader.ReadI64(aTOCOffset) : aReader.ReadValue(Jt_I32{}, aTOCOffset))
     || !aReader.ReadGUID (anLSGSegmentGUID))
    {
      ALARM ("Error: Failed to read TOC offset and LSG segment ID");
      return Handle(JtNode_Partition)();
    }
  }

  // Read TOC
  if (!readTOC (aFile, aTOCOffset))
  {
    ALARM ("Error: Failed to read TOC");
    return Handle(JtNode_Partition)();
  }

  // Find and read LSG segment
  Jt_I64 anLSGSegmentOffset;
  if (!myTOC.Find (anLSGSegmentGUID, anLSGSegmentOffset))
  {
    ALARM ("Error: No LSG segment");
    return Handle(JtNode_Partition)();
  }
  else
  {
    TRACE ("Info: LSG segment is found at offset " + anLSGSegmentOffset);
  }

  Handle(JtData_Object) aRootNode = readSegment (aFile, anLSGSegmentOffset, Standard_True);

  aFile.close();

  return Handle(JtNode_Partition)::DownCast (aRootNode);
}

//=======================================================================
//function : open
//purpose  : Open the file
//=======================================================================
Standard_Boolean JtData_Model::open (std::ifstream& aFile) const
{
#ifdef WNT
  aFile.open (reinterpret_cast<const wchar_t*> (myFileName.ToExtString()),
              std::ios::binary | std::ios::in);
#else
  TCollection_AsciiString anAsciiName (myFileName, ' ');
  aFile.open (anAsciiName.ToCString(), std::ios::binary | std::ios::in);
#endif

  return aFile.is_open();
}

//=======================================================================
//function : readTOC
//purpose  : Read TOC from the JT file
//=======================================================================
Standard_Boolean JtData_Model::readTOC (std::ifstream& theFile, const Jt_I64 theOffset)
{
  JtData_FileReader aReader (theFile, this, theOffset);

  // Read entry count
  Jt_I32 aCount;
  if (!aReader.ReadI32 (aCount) || aCount < 1 || aCount>1000000 || size_t(aCount)>(aReader.End()-aReader.GetPosition())/(myMajorVersion>=10 ? 32 : 28))
    return Standard_False;
  TRACE ("Info: TOC entries count = " + aCount);

  // Read TOC entries
  typedef NCollection_DataMap<Standard_Integer, Standard_Integer> TypeMap;
  TypeMap aTypeMap;
  while (aCount--)
  {
    // Read Segment ID, offset, length, attributes
    Jt_GUID aGUID;
    Jt_I64 aOffset;
    Jt_I32 aLength;
    Jt_U32 aAttrib;
    if (!aReader.ReadGUID (aGUID)
     || !(myMajorVersion >= 10 ? aReader.ReadI64(aOffset) : aReader.ReadValue(Jt_I32{}, aOffset))
     || !aReader.ReadI32  (aLength)
     || !aReader.ReadU32  (aAttrib))
    {
      return Standard_False;
    }

    const Standard_Integer aType = (aAttrib >> 24) & 0xFF;
    if(aOffset<0 || aLength<24 || size_t(aOffset)>aReader.End() || size_t(aLength)>aReader.End()-size_t(aOffset) || myTOC.IsBound(aGUID)) throw std::runtime_error("Invalid JT table-of-contents entry");
    if (aTypeMap.IsBound (aType))
      aTypeMap.ChangeFind (aType)++;
    else
      aTypeMap.Bind (aType, 1);

    myTOC.Bind (aGUID, aOffset);
  }

  TRACE ("Info: Type statistics");
  for (TypeMap::Iterator aMapIter (aTypeMap); aMapIter.More(); aMapIter.Next())
  {
    TRACE ("Type " + aMapIter.Key() + " count " + aMapIter.Value());
  }

  return Standard_True;
}

//=======================================================================
//function : readSegment
//purpose  : Read objects from a JT file segment
//=======================================================================
Handle(JtData_Object) JtData_Model::readSegment (std::ifstream&         theFile,
                                                 const Jt_I64           theOffset,
                                                 const Standard_Boolean theIsLSG) const
{
  JtData_FileReader aReader (theFile, this, theOffset);

  Standard_Size aSegStart = aReader.GetPosition();

  // Read the segment header
  Jt_GUID aGUID;
  Jt_I32  aType;
  Jt_I32  aSize;
  if (!aReader.ReadGUID (aGUID)
   || !aReader.ReadI32  (aType)
   || !aReader.ReadI32  (aSize))
  {
    ALARM ("Error: Failed to read header of segment with offset " + double(theOffset));
    return Handle(JtData_Object)();
  }

  // Select appropriate reader for reading the segment data:
  if(aSize<24 || size_t(aSize)>aReader.End()-aSegStart) throw std::runtime_error("Invalid JT segment size");
  const size_t physicalEnd=aReader.End();
  // - use the file reader if the segment is not compressed;
  // - use JtData_Inflate is the segment is compressed.
  JtData_Reader* aDataReaderPtr = &aReader;
  std::unique_ptr<JtData_Reader> ownedReader;
  switch (aType)
  {
  case 1:
  case 2:
  case 3:
  case 4:
  case 17:
  case 18:
  case 20:
  case 24:
  case 30:
  case 31:
  case 33:
    {
      // read Compression Flag, Data Length, Algorithm
      if(aSize<33) throw std::runtime_error("Truncated JT compression header");
      Jt_I32 aFlag;
      Jt_I32 aDataLength;
      Jt_U8  aAlgorithm;
      if (!aReader.ReadI32 (aFlag)
       || !aReader.ReadI32 (aDataLength)
       || !aReader.ReadU8  (aAlgorithm))
      {
        ALARM ("Error: Failed to read compression flags of segment with offset " + double(theOffset));
        return Handle(JtData_Object)();
      }

      // if compressed, replace the reader by a JtData_Inflate instance
      // Qualified legacy exporter case: a final LSG segment excludes the
      // algorithm byte from its size, although its compressed length includes
      // it. Permit exactly that one byte, only at physical EOF, for JT 8.1.
      if(myMajorVersion==8 && myMinorVersion==1 && aType==1 && aFlag==2 && aAlgorithm==2
         && aDataLength==aSize-31 && aSegStart+size_t(aSize)+1==physicalEnd) {
        ++aSize;
      }
      if (aDataLength<1 || aDataLength>aSize-32) return {};
      aReader.Limit(aSegStart+size_t(aSize));
      if (aFlag == 2 && aAlgorithm == 2)
        ownedReader.reset(new JtData_Inflate (aReader, aDataLength - sizeof (Jt_U8)));
      else if (aFlag == 3 && aAlgorithm == 3)
        ownedReader.reset(new JtData_BufferReader(aReader, aDataLength-1));
      else if (aFlag!=0) throw std::runtime_error("Unsupported JT segment compression");
      if(ownedReader) aDataReaderPtr=ownedReader.get();
    }
  }

  aReader.Limit(aSegStart+size_t(aSize));
  aDataReaderPtr->SetExternallyCompressed(bool(ownedReader));

  // Read the segment data
  Handle(JtData_Object) aFirstObject;
  Standard_Boolean      aResult;

  if (theIsLSG)
    aResult = readLSGData (*aDataReaderPtr, aFirstObject);
  else
    aResult = readElement (*aDataReaderPtr, aFirstObject);

  // Free the JtData_Inflate reader if it was used
  if(auto* inflated=dynamic_cast<JtData_Inflate*>(ownedReader.get()))
    if(aResult && !inflated->Finish()) throw std::runtime_error("Invalid/trailing JT zlib segment data");
  ownedReader.reset();

  // Check the reading result
  if (!aResult)
  {
    ALARM ("Error: Segment data reading failed");
    return Handle(JtData_Object)();
  }

  // Check the loaded data length
  Standard_Size aBytesSpec = aSegStart + aSize;
  Standard_Size aReaderPos = aReader.GetPosition();
  if (aBytesSpec < aReaderPos)
  {
 #ifdef OCCT_DEBUG
    Standard_Size aBytesRest = aBytesSpec - aReaderPos;
    ALARM ("Error: " + (aBytesRest) + " extra bytes were read after end of the segment");
#endif
    return Handle(JtData_Object)();
  }
  if (aBytesSpec > aReaderPos)
  {
#ifdef OCCT_DEBUG
    Standard_Size aBytesRest = aBytesSpec - aReaderPos;
    WARNING ("Warning: " + aBytesRest + " segment bytes remained after reading, ignored");
#endif
  }

  // Return the first object read from the segment
  return aFirstObject;
}

//=======================================================================
//function : readLSGData
//purpose  : Read LSG segment data
//=======================================================================
Standard_Boolean JtData_Model::readLSGData (JtData_Reader&         theReader,
                                            Handle(JtData_Object)& theFirstObject) const
{
  const TCollection_ExtendedString anObjectNameKey = "JT_PROP_NAME";

  // Read graph elements
  JtData_Object::MapOfObjects aMapObjects;
  if (!readElements (theReader, aMapObjects, theFirstObject))
  {
    ALARM ("Error: Failed to read LSG elements");
    return Standard_False;
  }

  // Bind the objects together
  for (JtData_Object::MapOfObjects::Iterator it (aMapObjects); it.More(); it.Next())
    it.ChangeValue()->BindObjects (aMapObjects);

  // Read property atom elements
  JtData_Object::MapOfObjects aMapProperties;
  if (!readElements (theReader, aMapProperties, theFirstObject))
  {
    ALARM ("Error: Failed to read property atom elements");
    return Standard_False;
  }

  // Read Property Table
  Jt_I16 aTableVersion;
  Jt_I32 aTableLength;
  if (!theReader.ReadI16 (aTableVersion)
   || !theReader.ReadI32 (aTableLength) || aTableLength<0 || aTableLength>1000000)
  {
    ALARM ("Error: Failed to read property table");
    return Standard_False;
  }

  if(aTableVersion!=1) throw std::runtime_error("Unsupported JT property table version");
  std::set<Jt_I32> propertyNodes;
  size_t propertyCount=0;

  while (aTableLength-- > 0)
  {
    // get an object
    Jt_I32 anObjectId;
    if (!theReader.ReadI32 (anObjectId))
    {
      ALARM ("Error: Failed to read LSG object ID");
      return Standard_False;
    }

    if(!propertyNodes.insert(anObjectId).second) throw std::runtime_error("Duplicate JT node property table");

    Handle(JtData_Object) anObject;
    aMapObjects.Find (anObjectId, anObject);

    // read (key,value) pairs
    JtData_Object::ListOfLateLoads aLateLoads;
    Jt_I32 aKey, aValue;
    for (;;)
    {
      // read key
      if (!theReader.ReadI32 (aKey))
      {
        ALARM ("Error: Failed to read property key");
        return Standard_False;
      }

      if (aKey == 0)
        break;

      if(++propertyCount>1000000) throw std::runtime_error("JT property count limit exceeded");
      JtData_Limits::poll();

      // read value
      if (!theReader.ReadI32 (aValue))
      {
        ALARM ("Error: Failed to read property value");
        return Standard_False;
      }

      Handle(JtData_Object) keyProperty,valueProperty;
      aMapProperties.Find(aKey,keyProperty);aMapProperties.Find(aValue,valueProperty);
      const auto propertyNode=Handle(JtNode_Base)::DownCast(anObject);
      // Unsupported optional atoms remain distinguishable from scalar values.
      // Late-loaded representation references keep their existing separate path.
      if(!propertyNode.IsNull() && Handle(JtProperty_LateLoaded)::DownCast(valueProperty).IsNull())
        propertyNode->BindProperty(aKey,aValue,keyProperty,valueProperty);

      // analyse the value
      Handle(JtData_Object) aProperty;
      if (aMapProperties.Find (aValue, aProperty))
      {
        // if this is late loaded property, add it to a list
        Handle(JtProperty_LateLoaded) aLateProp =
          Handle(JtProperty_LateLoaded)::DownCast (aProperty);
        if (!aLateProp.IsNull())
          aLateLoads.Append (aLateProp);
        // if this is a string property with key = "JT_PROP_NAME", bind it to the object
        else
        {
          Handle(JtProperty_String) aStringProp =
            Handle(JtProperty_String)::DownCast (aProperty);
          if (!aStringProp.IsNull())
          {
            Handle(JtData_Object) aKeyProperty;
            if (aMapProperties.Find (aKey, aKeyProperty))
            {
              Handle(JtProperty_String) aKeyStringProp =
                Handle(JtProperty_String)::DownCast (aKeyProperty);
              if (!anObject.IsNull() && !aKeyStringProp.IsNull() && aKeyStringProp->Value() == anObjectNameKey)
                anObject->BindName (aStringProp->Value());
            }
          }
        }
      }
    }

    // bind the late loaded properties to the object
    if (!anObject.IsNull() && !aLateLoads.IsEmpty())
      anObject->BindLateLoads (aLateLoads);
  }

  return Standard_True;
}

//=======================================================================
//function : readElements
//purpose  : Read a sequence of elements from a JT file segment
//=======================================================================
Standard_Boolean JtData_Model::readElements (JtData_Reader&               theReader,
                                             JtData_Object::MapOfObjects& theMap,
                                             Handle(JtData_Object)&       theFirstObject) const
{
  Handle(JtData_Object) anObject;
  Jt_I32                anObjectID;

  // Read elements while possible until End-Of-Elements marker is reached
  while (readElement (theReader, anObject, &anObjectID))
  {
    // if End-Of-Elements marker is reached, finish the reading
    if (anObject.IsNull())
      return Standard_True;

    // if an object of a known type is read, store it
    if (!anObject->IsInstance (STANDARD_TYPE (JtData_Object)))
    {
      if (theMap.IsBound(anObjectID)) throw std::runtime_error("Duplicate JT object ID");
      theMap.Bind (anObjectID, anObject);
      if (theFirstObject.IsNull())
        theFirstObject = anObject;
    }
  }

  // Fail if the reading was failed
  return Standard_False;
}

//=======================================================================
//function : readElement
//purpose  : Read an element from a JT file segment
//=======================================================================
namespace
{
  // Bound every payload read/skip, including inner Sabot headers, to its envelope.
  class ElementReader final : public JtData_Reader
  {
  public:
    ElementReader(JtData_Reader& source, Standard_Size end)
      : JtData_Reader(source.Model()), mySource(source), myEnd(end) {SetExternallyCompressed(source.IsExternallyCompressed());}
    Standard_Boolean ReadBytes(void* out, Standard_Size count) override
    {
      const auto pos = GetPosition();
      if (pos > myEnd || count > myEnd-pos) return Standard_False;
      return mySource.ReadBytes(out, count);
    }
    Standard_Boolean SkipBytes(Standard_Size count) override
    {
      const auto pos = GetPosition();
      if (pos > myEnd || count > myEnd-pos) return Standard_False;
      return mySource.SkipBytes(count);
    }
    Standard_Size GetPosition() const override { return mySource.GetPosition(); }
    Standard_Size ElementBytesRemaining() const override {return myEnd-GetPosition();}
  private:
    JtData_Reader& mySource;
    Standard_Size myEnd;
  };
}

Standard_Boolean JtData_Model::readElement (JtData_Reader& theInput,
                                            Handle(JtData_Object)& theObject,
                                            Jt_I32* theObjectIDPtr) const
{
  JtData_Limits::poll();
  Jt_I32 length;
  if (!theInput.ReadI32(length)) return Standard_False;
  const auto start = theInput.GetPosition();
  if (length < 16 || length > 512*1024*1024
      || Standard_Size(length) > std::numeric_limits<Standard_Size>::max()-start)
    throw std::runtime_error("Invalid JT element envelope");
  const auto end = start + Standard_Size(length);
  ElementReader reader(theInput, end);
  Jt_GUID type;
  if (!reader.ReadGUID(type)) return Standard_False;
  if (type == EOEMarkerGUID)
  {
    if (length != 16) throw std::runtime_error("Invalid JT end marker envelope");
    theObject.Nullify();
    return Standard_True;
  }
  if (length < 21) throw std::runtime_error("Invalid JT element envelope");
  Jt_U8 base;
  Jt_I32 id = -1;
  if (!reader.ReadU8(base)) return Standard_False;
  if ((theObjectIDPtr || MajorVersion() > 8) && !reader.ReadI32(id)) return Standard_False;

  static const Jt_GUID sabot("96603dd3-5a0f-40f5-97-cb-1e-96-47-cb-d3-7e");
  const bool wrapped = type == sabot;
  if (wrapped)
  {
    if (MajorVersion() < 10 || base != 9)
      throw std::runtime_error("Unsupported JT Sabot wrapper layout");
    // The embedded Element Header has GUID/base/ID, but no element-length word.
    // The inner object ID replaces the disposable outer wrapper ID.
    if (!reader.ReadGUID(type) || !reader.ReadU8(base) || !reader.ReadI32(id))
      throw std::runtime_error("Truncated JT Sabot attribute header");
    if (type == sabot || base != 3 || type == EOEMarkerGUID)
      throw std::runtime_error("Invalid JT Sabot wrapped attribute");
  }
  const auto* info = JtData_Object::FindClass(type);
  if (!info)
  {
    if (wrapped) throw std::runtime_error("Unsupported JT Sabot wrapped attribute type");
    theObject = new JtData_Object;
    return reader.SkipBytes(end-reader.GetPosition());
  }
  theObject = info->Create();
  const auto attribute = Handle(JtAttribute_Base)::DownCast(theObject);
  if (wrapped && attribute.IsNull())
    throw std::runtime_error("Invalid JT Sabot non-attribute payload");
  if (!theObject->Read(reader))
  {
    ALARM(TCollection_ExtendedString("Cannot decode JT element: ") + info->Name());
    return Standard_False;
  }
  if (wrapped && attribute->BaseVersion() != 2)
    throw std::runtime_error("Unsupported JT Sabot attribute base version");
  if (wrapped && reader.GetPosition() != end)
    throw std::runtime_error("Invalid JT Sabot attribute envelope length");
  if (!reader.SkipBytes(end-reader.GetPosition())) return Standard_False;
  if (theObjectIDPtr) *theObjectIDPtr = id;
  return Standard_True;
}

//=======================================================================
//function : FindSegment
//purpose  : Lookup offset of a segment in TOCs of this model and its ancestor models
//=======================================================================
Handle(JtData_Model) JtData_Model::FindSegment (const Jt_GUID& theGUID,
                                                      Jt_I64&  theOffset) const
{
  if (myTOC.Find (theGUID, theOffset))
    return this;
  else if (!myParent.IsNull())
    return myParent->FindSegment (theGUID, theOffset);
  else
  {
    Standard_Character aGuidString[128];
    theGUID.ToString (aGuidString);
    ALARM ("Error: Failed to find segment with GUID: " + aGuidString);
    return Handle(JtData_Model)();
  }
}

//=======================================================================
//function : ReadSegment
//purpose  : Read object from a late loaded segment
//=======================================================================
Handle(JtData_Object) JtData_Model::ReadSegment (const Jt_I64 theOffset) const
{
  std::ifstream aFile;
  if (!open (aFile))
  {
    ALARM ("Error: Failed to open Jt file of late-loaded segment");
    return Handle(JtNode_Partition)();
  }

  return readSegment (aFile, theOffset, Standard_False);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtData_Model::Dump (Standard_OStream& /*theStream*/) const
{
  return 0;
}
