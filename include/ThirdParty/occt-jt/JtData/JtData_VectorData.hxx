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

#ifndef _JtData_VectorData_HeaderFile
#define _JtData_VectorData_HeaderFile

#include <Standard.hxx>

#include <cstring>
#include <JtData_Limits.hxx>

//! Vector data access.
template <class ValT, class SizeT>
class JtData_VectorData
{
private:
  struct ValHolder
  {
    ValT Value;
    DEFINE_STANDARD_ALLOC
  };

protected:
  struct Size
  {
    SizeT         ElemCount;
    Standard_Size ValCount;

    Size (SizeT theCount)
      : ElemCount (theCount), ValCount (theCount) {}

  protected:
    Size (SizeT theElemCount, Standard_Size theValCount)
      : ElemCount (theElemCount), ValCount (theValCount) {}
  };

public:
  class Base
  {
  public:
    typedef ValT  ValType;
    typedef SizeT SizeType;

    SizeT            Count()   const { return myCount; }
    Standard_Boolean IsEmpty() const { return myCount == 0; }
    ValT*            Data()    const { return myData; }

    ValT& operator[] (SizeT theIndex) const {
      if(theIndex<0 || theIndex>=myCount) throw std::runtime_error("JT decoded array index out of range");
      return myData[theIndex];
    }

    ValT& First() const { return (*this)[0]; }
    ValT& Last()  const { return (*this)[myCount-1]; }

    void SafeSet (SizeT theIndex, const ValT& theValue) const
    {
      if (theIndex < myCount)
        (*this)[theIndex] = theValue;
    }

    template <class Functor>
    Standard_Boolean Process (Functor& theFunctor)
    {
      for (SizeT anIdx = 0; anIdx < myCount; anIdx++)
        if (!theFunctor (myData[anIdx]))
          return Standard_False;

      return Standard_True;
    }

  protected:
    Base() {}

    Base (ValT* theData, SizeT theCount) : myData (theData), myCount (theCount) {}

    Standard_Boolean allocate (const Size& theSize)
    {
      if(theSize.ElemCount<0 || theSize.ValCount>256u*1024u*1024u/sizeof(ValHolder)) throw std::runtime_error("Invalid JT array count");
      JtData_Limits::allocation(theSize.ValCount*sizeof(ValHolder));
      if (theSize.ValCount > 0)
      {
        myData = reinterpret_cast <ValT*> (new ValHolder[theSize.ValCount]);
        myCount = myData ? theSize.ElemCount : 0;
        return Standard_True;
      }
      else
      {
        clear();
        return Standard_False;
      }
    }

    Standard_Boolean reallocate (const Size& theSize)
    {
      free();
      return !allocate (theSize) || myData != 0L;
    }

    void copy (const Size& theSize, const ValT* theData)
    {
      if (allocate (theSize) && myData)
        memcpy (myData, theData, theSize.ValCount * sizeof (ValT));
    }

    void copy (const Base& theOther)
    {
      copy (theOther.myCount, theOther.myData);
    }

    void free()
    {
      if (myData)
        delete[] (reinterpret_cast <ValHolder*> (myData));
      clear();
    }

    void clear()
    {
      myCount = 0;
      myData = 0L;
    }

  protected:
    template <class DataT>
    struct detachedT
    {
      DataT Data;
      explicit detachedT (DataT& theVector) : Data (theVector) { theVector.clear(); }
    };

    template <class DataT>
    void attach (const detachedT<DataT>& theDetached)
    {
      *static_cast<DataT*> (this) = theDetached.Data;
    }

  protected:
    ValT* myData;
    SizeT myCount;

  public:
    DEFINE_STANDARD_ALLOC
  };
};

#endif // _JtData_VectorData_HeaderFile
