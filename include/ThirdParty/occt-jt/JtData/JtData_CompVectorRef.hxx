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

#ifndef _JtData_CompVectorRef_HeaderFile
#define _JtData_CompVectorRef_HeaderFile

#include <JtData_CompVectorData.hxx>

template <class ValT, class SizeT, class CompCountT> class JtData_CompVector;

//! Reference to a complex vector's data or just to a complex array in memory.
//! Can be used to get or set elements of a vector/array but not to allocate or deallocate it.
//! Intended to be used primarily for function arguments.
template <class ValT, class SizeT = Standard_Size, class CompCountT = SizeT>
class JtData_CompVectorRef : public JtData_CompVectorData<ValT, SizeT, CompCountT>::Base
{
protected:
  typedef typename JtData_CompVectorData<ValT, SizeT, CompCountT>::Base data;

public:
  JtData_CompVectorRef (ValT* theData, SizeT theCount, CompCountT theCompCount)
    : data (theData, theCount, theCompCount) {}

  template <SizeT theValCount>
  JtData_CompVectorRef (ValT (&theData) [theValCount], CompCountT theCompCount)
    : data (theData, theValCount / static_cast<SizeT> (theCompCount), theCompCount) {}

  JtData_CompVectorRef (const JtData_CompVector<ValT, SizeT, CompCountT>& theVector)
    : data (theVector) {}

  JtData_CompVectorRef& operator = (const JtData_CompVectorRef& theOther)
  {
    if (this->myCount > theOther.myCount)
      this->myCount = theOther.myCount;

    if (this->myCompCount == theOther.myCompCount)
    {
      memcpy (this->myData, theOther.myData,
              this->myCount * static_cast <SizeT> (this->myCompCount) * sizeof (ValT));
    }
    else
    {
      for (SizeT i = 0; i < this->myCount; i++)
        (*this)[i] = theOther[i];
    }

    return *this;
  }
};

#endif // _JtData_CompVectorRef_HeaderFile
