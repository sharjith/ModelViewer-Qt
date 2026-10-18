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

#ifndef _JtData_Model_HeaderFile
#define _JtData_Model_HeaderFile

#include <JtData_Types.hxx>
#include <JtData_Object.hxx>

#include <TCollection_ExtendedString.hxx>

#include <Standard_DefineHandle.hxx>
#include <Standard_OStream.hxx>
#include <NCollection_DataMap.hxx>

#include <fstream>

class JtNode_Partition;

DEFINE_STANDARD_HANDLE(JtData_Model, Standard_Transient)

//! Model class dealing with Jt file and providing base services
//! on file open, read header, fetching Jt segments.
class JtData_Model : public Standard_Transient
{
public:
  static const Standard_Boolean IsLittleEndianHost;

public:
  //! Constructor initializing model by specified file
  //! @param theFileName - name of Jt file
  //! @param theParent - the parent model in case of non monolythic assembly.
  Standard_EXPORT JtData_Model (const TCollection_ExtendedString& theFileName,
                                const Handle(JtData_Model)& theParent = Handle(JtData_Model)());

  //! Read JT file header, TOC and LSG and return a handle to the root LSG node.
  Standard_EXPORT Handle(JtNode_Partition) Init();

  //! Lookup offset of a segment in TOCs of this model and its ancestor models.
  Standard_EXPORT Handle(JtData_Model) FindSegment (const Jt_GUID& theGUID,
                                                          Jt_I64&  theOffset) const;

  //! Read object from a late loaded segment.
  Standard_EXPORT Handle(JtData_Object) ReadSegment (const Jt_I64 theOffset) const;

  //! Dump this entity.
  Standard_EXPORT Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Return file name.
  const TCollection_ExtendedString& FileName()     const { return myFileName; }

  //! Return Little Endian state.
  Standard_Boolean                  IsFileLE()     const { return myIsFileLE; }

  //! Return major version of Jt file.
  Standard_Integer                  MajorVersion() const { return myMajorVersion; }

  //! Return minor version of Jt file.
  Standard_Integer                  MinorVersion() const { return myMinorVersion; }

  DEFINE_STANDARD_RTTIEXT(JtData_Model, Standard_Transient)

protected:
  //! Open the file.
  Standard_Boolean open (std::ifstream& aFile) const;

  //! Read TOC from the JT file.
  Standard_Boolean readTOC (std::ifstream& theFile, const Jt_I64 theOffset);

  //! Read object(s) from a JT file segment.
  Handle(JtData_Object) readSegment (std::ifstream&           theFile,
                                     const Jt_I64             theOffset,
                                     const Standard_Boolean   theIsLSG) const;
  //! Read LSG segment data.
  Standard_Boolean readLSGData  (JtData_Reader&               theReader,
                                 Handle(JtData_Object)&       theFirstObject) const;

  //! Read a sequence of elements from a JT file segment.
  Standard_Boolean readElements (JtData_Reader&               theReader,
                                 JtData_Object::MapOfObjects& theMap,
                                 Handle(JtData_Object)&       theFirstObject) const;

  //! Read an element from a JT file segment.
  Standard_Boolean readElement  (JtData_Reader&               theReader,
                                 Handle(JtData_Object)&       theObject,
                                 Jt_I32*                      theObjectIDPtr = 0L) const;

protected:
  Handle(JtData_Model)       myParent;

  TCollection_ExtendedString myFileName;
  Standard_Boolean           myIsFileLE;

  Standard_Integer           myMajorVersion;
  Standard_Integer           myMinorVersion;

  NCollection_DataMap <Jt_GUID, Jt_I64, Jt_GUID> myTOC;
};

#endif // _JtData_Model_HeaderFile
