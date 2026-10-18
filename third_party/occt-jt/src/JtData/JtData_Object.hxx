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

#ifndef _JtData_Object_HeaderFile
#define _JtData_Object_HeaderFile

#include <Standard_Transient.hxx>
#include <Standard_DefineHandle.hxx>
#include <NCollection_List.hxx>
#include <TCollection_ExtendedString.hxx>

#include <JtData_Vector.hxx>
#include <JtData_ClassInfo.hxx>

class JtData_Reader;
class JtProperty_LateLoaded;

DEFINE_STANDARD_HANDLE(JtData_Object, Standard_Transient)

//! The base class representing Jt entity.
class JtData_Object : public Standard_Transient
{
public:
  typedef JtData_ClassInfo <JtData_Object> ClassInfo;

  //! Return class by Jt Object type ID.
  //! @param theGUID - Jt object's type GUID.
  static const ClassInfo* FindClass (const Jt_GUID& theGUID) { return ClassInfo::Find (theGUID); }

  typedef NCollection_DataMap <Standard_Integer, Handle(JtData_Object)> MapOfObjects;
  typedef JtData_Vector       <Handle(JtData_Object)>                   VectorOfObjects;
  typedef NCollection_List    <Handle(JtProperty_LateLoaded)>           ListOfLateLoads;
  typedef JtData_Vector       <Handle(JtProperty_LateLoaded)>           VectorOfLateLoads;

public:
  //! Read this entity from a JT file.
  //! @param theReader - current Jt file reader.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& S) const;

  //! Bind other objects to the object.
  Standard_EXPORT virtual void BindObjects (const MapOfObjects& theObjectsMap);

  //! Bind a late loaded property to the object.
  Standard_EXPORT virtual void BindLateLoads (const ListOfLateLoads& theLateLoads);

  //! Bind a name to the object.
  Standard_EXPORT virtual void BindName (const TCollection_ExtendedString& theName);

  DEFINE_STANDARD_RTTIEXT(JtData_Object, Standard_Transient)
};

#define DEFINE_OBJECT_CLASS(Class) \
  DEFINE_CLASS_INFO(Class, JtData_Object)

#define IMPLEMENT_OBJECT_CLASS(Class, Name, GUID) \
  IMPLEMENT_CLASS_INFO(Class, JtData_Object, Name, GUID)

#endif // _JtData_Object_HeaderFile
