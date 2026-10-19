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

#ifndef _JtData_Vector_HeaderFile
#define _JtData_Vector_HeaderFile

#include <JtData_VectorData.hxx>
#include <JtData_VectorRef.hxx>
#include <JtData_VectorMover.hxx>

//! Vector creation and destruction, allocation and deallocation of its storage,
//! and transferring data from/to other objects.
//! Intended to be used for local/global/member variables.
//! Do not use for function arguments or return values.
//! For arguments, use:
//! - Ref class if the function just reads or changes values of the vector's elements;
//! - Mover class if the function moves or removes the vector's contents.
//! For return values, use:
//! - Mover class to return contents of a locally created vector;
//! - Ref class to return a reference to an external vector's data.
template <class ValT, class SizeT = Standard_Size>
class JtData_Vector : public JtData_VectorData<ValT, SizeT>::Base
{
  friend class JtData_VectorMover <JtData_Vector>;

public:
  typedef JtData_VectorRef   <ValT, SizeT>   Ref;
  typedef JtData_VectorMover <JtData_Vector> Mover;

protected:
  typedef typename JtData_VectorData<ValT, SizeT>::Base data;
  typedef typename data::template detachedT<data> detached;

public:
  JtData_Vector() { data::clear(); }

  JtData_Vector (const SizeT theCount) { data::allocate (theCount); }

  JtData_Vector (const Ref&           theRef)   { data::copy   (theRef);   }
  JtData_Vector (const JtData_Vector& theOther) { data::copy   (theOther); }
  JtData_Vector (const detached&      theMover) { data::attach (theMover); }

  template <class Collection>
  static Mover FromCollection (const Collection& theCollection)
  {
    JtData_Vector aVector (theCollection.Size());
    if (!aVector.IsEmpty())
    {
      ValT* apElem = &aVector.First();
      for (typename Collection::Iterator it (theCollection); it.More(); it.Next())
        *apElem++ = it.Value();
    }
    return aVector.Move();
  }

  JtData_Vector& operator = (const Ref& theRef)
  {
    data::free();
    data::copy (theRef);
    return *this;
  }

  JtData_Vector& operator = (const JtData_Vector& theOther)
  {
    data::free();
    data::copy (theOther);
    return *this;
  }

  JtData_Vector& operator = (const detached& theMover)
  {
    data::free();
    data::attach (theMover);
    return *this;
  }

  JtData_Vector& operator << (JtData_Vector& theOther)
  {
    data::free();
    data::attach (detached (theOther));
    return theOther;
  }

  Mover Move() { return Mover (detached (*this)); }

  Standard_Boolean Allocate (const SizeT theCount) { return data::reallocate (theCount); }

  void Free() { data::free(); data::clear(); }

  ~JtData_Vector() { data::free(); }
};

#endif // _JtData_Vector_HeaderFile
