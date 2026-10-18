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

#ifndef _JtData_ClassInfo_HeaderFile
#define _JtData_ClassInfo_HeaderFile

#include <JtData_Types.hxx>

#include <Standard.hxx>
#include <NCollection_Map.hxx>

template <class RootClass>
class JtData_ClassInfo
{
public:
  static const JtData_ClassInfo* Find (const Jt_GUID& theGUID)
  {
    const JtData_ClassInfo* aValue;
    if (JtData_ClassInfo<RootClass>::DerivedClasses().Find (theGUID, aValue))
      return aValue;
    else
      return NULL;
  }

  virtual RootClass*       Create() const = 0;
  virtual Standard_CString Name()   const = 0;

  // D-tor to prevent gcc warnings
  virtual ~JtData_ClassInfo() {};

protected:
  typedef NCollection_DataMap<Jt_GUID, const JtData_ClassInfo*, Jt_GUID> MapOfClasses;
  static MapOfClasses& DerivedClasses()
  {
    static MapOfClasses aMap;
    return aMap;
  }
};

template <class Class, class RootClass>
class JtData_ClassInfoT : public JtData_ClassInfo<RootClass>
{
  Standard_CString myName;

public:
  JtData_ClassInfoT (const Jt_GUID& theGUID, Standard_CString theName,
                     Standard_Boolean theRegister = Standard_False)
  : myName (theName)
  {
    if (theRegister)
      this->DerivedClasses().Bind (theGUID, this);
  }

  virtual RootClass*       Create() const { return new Class(); }
  virtual Standard_CString Name()   const { return myName; }
};

#define DEFINE_CLASS_INFO(theClass, theRootClass) \
  static JtData_ClassInfoT<theClass, theRootClass> ClassInfo;

#define IMPLEMENT_CLASS_INFO(theClass, theRootClass, theName, theGUID) \
  JtData_ClassInfoT<theClass, theRootClass> theClass::ClassInfo (theGUID, theName, Standard_True);

#endif // _JtData_ClassInfo_HeaderFile
