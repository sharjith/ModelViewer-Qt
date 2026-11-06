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

#include <JtData_DeferredObject.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtData_DeferredObject, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtData_DeferredObject, JtData_Object)

//=======================================================================
//function : Read
//purpose  : Read a deferred object from a JT file
//=======================================================================
Standard_Boolean JtData_DeferredObject::Read (JtData_Reader&         theReader,
                                              Handle(JtData_Object)& theResult)
{
  Jt_I32 anObjectID;
  if (!theReader.ReadI32 (anObjectID))
    return Standard_False;

  theResult = new JtData_DeferredObject (anObjectID);
  return Standard_True;
}

//=======================================================================
//function : Resolve
//purpose  : Replace the deferred object by the corresponding object from the given map
//=======================================================================
void JtData_DeferredObject::Resolve (Handle(JtData_Object)& theObject,
                                     const MapOfObjects&    theObjectsMap)
{
  Handle(JtData_DeferredObject) aDeferred = Handle(JtData_DeferredObject)::DownCast (theObject);
  if (!aDeferred.IsNull())
  {
    theObjectsMap.Find (aDeferred->myObjectID, theObject);
  }
}

//=======================================================================
//function : ReadVec
//purpose  : Read an array of deferred objects from a JT file
//=======================================================================
Standard_Boolean JtData_DeferredObject::ReadVec (JtData_Reader&   theReader,
                                                 VectorOfObjects& theResult)
{
  Jt_I32 aCount;
  if (!theReader.ReadI32 (aCount))
    return Standard_False;

  theResult.Allocate (aCount);
  for (Jt_I32 i = 0; i < aCount; i++)
  {
    if (!Read (theReader, theResult[i]))
    {
      theResult.Free();
      return Standard_False;
    }
  }

  return Standard_True;
}

//=======================================================================
//function : ResolveVec
//purpose  : Put objects from the given map instead of corresponding deferred objects
//=======================================================================
void JtData_DeferredObject::ResolveVec (VectorOfObjects&    theVector,
                                        const MapOfObjects& theObjectsMap)
{
  for (VectorOfObjects::SizeType i = 0; i < theVector.Count(); i++)
    Resolve (theVector[i], theObjectsMap);
}
