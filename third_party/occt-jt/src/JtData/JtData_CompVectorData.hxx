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

#ifndef _JtData_CompVectorData_HeaderFile
#define _JtData_CompVectorData_HeaderFile

#include <JtData_VectorData.hxx>
#include <JtData_VectorRef.hxx>

//! Complex vector data access.
template <class ValT, class SizeT, class CompCountT>
class JtData_CompVectorData : public JtData_VectorData<ValT, SizeT>
{
protected:
  struct Size : JtData_VectorData<ValT, SizeT>::Size
  {
    Size (SizeT theCount, CompCountT theCompCount)
      : JtData_VectorData<ValT, SizeT>::Size (theCount, static_cast <Standard_Size> (theCount)
                                              * static_cast <Standard_Size> (theCompCount)) {}
  };

public:
  class Base : public JtData_VectorData<ValT, SizeT>::Base
  {
  public:
    typedef CompCountT CompCountType;
    typedef JtData_VectorRef <ValT, CompCountT> ElemRef;

    SizeT CompCount() const { return myCompCount; }

    ElemRef operator[] (SizeT theIndex) const
      {
        if(theIndex<0 || theIndex>=this->myCount) throw std::runtime_error("JT component index out of range");
        return ElemRef (this->myData + theIndex * myCompCount, myCompCount);
      }

    ElemRef First() const { return (*this)[0]; }
    ElemRef Last()  const { return (*this)[this->myCount-1]; }

    void SafeSet (SizeT theIndex, ElemRef theValue) const
    {
      if (theIndex < this->myCount)
        (*this)[theIndex] = theValue;
    }

  protected:
    typedef typename JtData_VectorData<ValT, SizeT>::Base simpleData;

  protected:
    Base() {}

    Base (ValT* theData, SizeT theCount, CompCountT theCompCount)
      : simpleData (theData, theCount), myCompCount (theCompCount) {}

    Standard_Boolean allocate (const SizeT theCount, const CompCountT theCompCount)
    {
      myCompCount = theCompCount;
      return simpleData::allocate (Size (theCount, theCompCount));
    }

    Standard_Boolean reallocate (const SizeT theCount, const CompCountT theCompCount = 0)
    {
      if (theCompCount)  myCompCount = theCompCount;
      return simpleData::reallocate (Size (theCount, myCompCount));
    }

    void copy (const Base& theOther)
    {
      myCompCount = theOther.myCompCount;
      simpleData::copy (Size (theOther.myCount, theOther.myCompCount), theOther.myData);
    }

  protected:
    CompCountT myCompCount;
  };
};

#endif // _JtData_CompVectorData_HeaderFile
