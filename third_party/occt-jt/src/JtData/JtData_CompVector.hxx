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

#ifndef _JtData_CompVector_HeaderFile
#define _JtData_CompVector_HeaderFile

#include <JtData_CompVectorData.hxx>
#include <JtData_CompVectorRef.hxx>
#include <JtData_VectorMover.hxx>

//! Class for complex vector creation and destruction,
//! allocation and deallocation of its storage,
//! and transferring data from/to other objects.
//! Intended to be used for local/global/member variables.
//! Do not use for function arguments or return values.
//! For arguments, use:
//! - Ref class if the function just reads or changes values of the vector's elements;
//! - Mover class if the function moves or removes the vector's contents.
//! For return values, use:
//! - Mover class to return contents of a locally created vector;
//! - Ref class to return a reference to an external vector's data.
template <class ValT, class SizeT = Standard_Size, class CompCountT = SizeT>
class JtData_CompVector : public JtData_CompVectorData<ValT, SizeT, CompCountT>::Base
{
  friend class JtData_VectorMover <JtData_CompVector>;

public:
  typedef JtData_CompVectorRef <ValT, SizeT, CompCountT> Ref;
  typedef JtData_VectorMover   <JtData_CompVector>       Mover;

protected:
  typedef typename JtData_CompVectorData<ValT, SizeT, CompCountT>::Base data;
  typedef typename data::template detachedT<data> detached;

public:
  JtData_CompVector() { data::clear(); }

  JtData_CompVector (const SizeT theCount, const CompCountT theCompCount)
    { data::allocate (theCount, theCompCount); }

  JtData_CompVector (const Ref&               theRef)   { data::copy   (theRef);   }
  JtData_CompVector (const JtData_CompVector& theOther) { data::copy   (theOther); }
  JtData_CompVector (const detached&          theMover) { data::attach (theMover); }

  JtData_CompVector& operator = (const Ref& theRef)
  {
    data::free();
    data::copy (theRef);
    return *this;
  }

  JtData_CompVector& operator = (const JtData_CompVector& theOther)
  {
    data::free();
    data::copy (theOther);
    return *this;
  }

  JtData_CompVector& operator = (const detached& theMover)
  {
    data::free();
    data::attach (theMover);
    return *this;
  }

  JtData_CompVector& operator << (JtData_CompVector& theOther)
  {
    data::free();
    data::attach (detached (theOther));
    return theOther;
  }

  Mover Move() { return Mover (detached (*this)); }

  Standard_Boolean Allocate (const SizeT theCount, const CompCountT theCompCount = 0)
    { return data::reallocate (theCount, theCompCount); }

  void Free() { data::free(); data::clear(); }

  ~JtData_CompVector() { data::free(); }
};

#endif // _JtData_CompVector_HeaderFile
