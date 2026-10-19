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

#ifndef _JtData_VectorMover_HeaderFile
#define _JtData_VectorMover_HeaderFile

//! An auxiliary class for moving data between vectors instead of copying.
//! Intended to be used for function return values and possibly arguments.
template <class Vector> class JtData_VectorMover
{
protected:
  typedef typename Vector::detached detached;

public:
  JtData_VectorMover (JtData_VectorMover& theOther) : myVector (theOther) {}
  JtData_VectorMover (const detached&  theDetached) : myVector (theDetached) {}

  operator detached() { return detached (myVector); }

  const Vector* operator -> () const { return &myVector; }
  const Vector& operator *  () const { return  myVector; }

protected:
  Vector myVector;
};

#endif // _JtData_VectorMover_HeaderFile
