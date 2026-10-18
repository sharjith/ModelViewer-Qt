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

#ifndef _JtNode_Base_HeaderFile
#define _JtNode_Base_HeaderFile

#include <JtData_Object.hxx>

//! Base Node Element is the general form of a node presented in LSG. All the other nodes
//! are inherited from the Base Node Element. The basic and general methods are implemented.
class JtNode_Base : public JtData_Object
{
public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Bind other objects to the object.
  Standard_EXPORT virtual void BindObjects (const MapOfObjects& theObjectsMap);

  //! Bind a name to the object.
  Standard_EXPORT virtual void BindName (const TCollection_ExtendedString& theName);

  //! Return object's attributes.
  const VectorOfObjects& Attributes() const { return myAttributes; }

  //! Return node's name.
  const TCollection_ExtendedString& Name() const { return myName; }
  bool Ignored() const {return myIgnored;}

  DEFINE_STANDARD_RTTIEXT(JtNode_Base, JtData_Object)
  DEFINE_OBJECT_CLASS (JtNode_Base)

protected:
  bool myIgnored=false;
  VectorOfObjects myAttributes;
  TCollection_ExtendedString myName;
};

DEFINE_STANDARD_HANDLE(JtNode_Base, JtData_Object)

#endif // _JtNode_Base_HeaderFile
