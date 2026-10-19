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

#ifndef _JtData_SingleHandle_HeaderFile
#define _JtData_SingleHandle_HeaderFile

template <class Type>
class JtData_SingleHandle
{
public:
  typedef Type ObjectType;

  JtData_SingleHandle() : myObject (0L) {}
  JtData_SingleHandle (Type*                theObject) : myObject (theObject)       {}
  JtData_SingleHandle (JtData_SingleHandle& theOther)  : myObject ((Type*)theOther) {}

  template <class OtherType>
  JtData_SingleHandle (OtherType* theObject)
    : myObject (theObject) {}

  template <class OtherType>
  JtData_SingleHandle (JtData_SingleHandle<OtherType> theOther)
    : myObject ((OtherType*)theOther) {}

  ~JtData_SingleHandle()
  {
    if (myObject)
    {
      delete myObject;
      myObject = 0L;
    }
  }

  JtData_SingleHandle<Type>& operator = (Type* theObject)
  {
    if (myObject)
      delete myObject;
    myObject = theObject;
    return *this;
  }

  JtData_SingleHandle& operator = (JtData_SingleHandle& theOther)
  {
    return *this = (Type*)theOther;
  }

  operator Type*()
  {
    Type* anObject = myObject;
    myObject = 0L;
    return anObject;
  }

  operator bool()      const { return  myObject != 0L; }
  bool  operator !  () const { return  myObject == 0L; }
  Type* operator -> () const { return  myObject; }
  Type& operator *  () const { return *myObject; }

protected:
  Type* myObject;
};

#endif // _JtData_SingleHandle_HeaderFile
