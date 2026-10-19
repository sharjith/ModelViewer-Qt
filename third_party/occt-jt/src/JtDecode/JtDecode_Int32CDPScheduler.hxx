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

#ifndef _JtDecode_Int32CDPScheduler_HeaderFile
#define _JtDecode_Int32CDPScheduler_HeaderFile

#include <JtDecode_Int32CDP.hxx>
#include <JtData_Parallel.hxx>
#include <list>

template <Jt_I32 theMinValCount>
class JtDecode_Int32CDPScheduler
{
  typedef JtDecode_Int32CDP::DecodingFunctor Task;

  std::list <Task>           mySerialTasks;
  JtData_Parallel::TaskGroup myParallelTasks;

public:
  template <class Result>
  void Run (JtDecode_Int32CDP& thePackage, Result& theResult,
            JtDecode_Unpack& theUnpacker = JtDecode_Unpack_Null)
  {
    Task aTask = thePackage.GetDecodingFunctor (theResult, theUnpacker);

    if (thePackage.GetOutValCount() < theMinValCount)
      mySerialTasks.push_back (aTask);
    else
      myParallelTasks.Run (aTask);
  }

  void Wait()
  {
    while (!mySerialTasks.empty())
    {
      mySerialTasks.front()();
      mySerialTasks.pop_front();
    }

    myParallelTasks.Wait();
  }
};

#endif
