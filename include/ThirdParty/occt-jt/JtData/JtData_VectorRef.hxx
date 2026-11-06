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

#ifndef _JtData_VectorRef_HeaderFile
#define _JtData_VectorRef_HeaderFile

#include <JtData_VectorData.hxx>

template <class ValT, class SizeT> class JtData_Vector;

//! Reference to a vector's data or just to an array in memory.
//! Can be used to get or set elements of a vector/array but not to allocate or deallocate it.
//! Intended to be used primarily for function arguments.
//! Also used to reference an element of a complex vector.
template <class ValT, class SizeT = Standard_Size>
class JtData_VectorRef : public JtData_VectorData<ValT, SizeT>::Base
{
protected:
  typedef typename JtData_VectorData<ValT, SizeT>::Base data;

public:
  JtData_VectorRef (ValT* theData, SizeT theCount) : data (theData, theCount) {}

  template <SizeT theCount>
  JtData_VectorRef (ValT (&theData) [theCount])    : data (theData, theCount) {}

  JtData_VectorRef (const JtData_Vector<ValT, SizeT>& theVector) : data (theVector) {}

  template <class Struct>
  explicit JtData_VectorRef (Struct& theStruct)
    : data (reinterpret_cast <ValT*> (&theStruct), sizeof (Struct) / sizeof (ValT)) {}

  JtData_VectorRef& operator = (const JtData_VectorRef& theOther)
  {
    if (this->myCount > theOther.myCount)
      this->myCount = theOther.myCount;
    memcpy (this->myData, theOther.myData, this->myCount * sizeof (ValT));
    return *this;
  }
};

#endif // _JtData_VectorRef_HeaderFile
