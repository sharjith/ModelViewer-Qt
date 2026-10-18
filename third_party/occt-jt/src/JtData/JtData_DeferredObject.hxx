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

#ifndef _JtData_DeferredObject_HeaderFile
#define _JtData_DeferredObject_HeaderFile

#include <JtData_Object.hxx>

class JtData_DeferredObject : public JtData_Object
{
public:
  //! Read a deferred object from a JT file.
  static Standard_Boolean Read (JtData_Reader&         theReader,
                                Handle(JtData_Object)& theResult);

  //! Replace the deferred object by the corresponding object from the given map.
  static void Resolve (Handle(JtData_Object)& theObject,
                       const MapOfObjects&    theObjectsMap);

  //! Read an array of deferred objects from a JT file.
  static Standard_Boolean ReadVec (JtData_Reader&   theReader,
                                   VectorOfObjects& theResult);

  //! Replace the deferred objects by the corresponding objects from the given map.
  static void ResolveVec (VectorOfObjects&    theVector,
                          const MapOfObjects& theObjectsMap);

public:
  JtData_DeferredObject (Jt_I32 theObjectID) : myObjectID (theObjectID) {}

  DEFINE_STANDARD_RTTIEXT(JtData_DeferredObject, JtData_Object)

protected:
  Jt_I32 myObjectID;
};

DEFINE_STANDARD_HANDLE(JtData_DeferredObject, JtData_Object)

#endif // _JtData_DeferredObject_HeaderFile
