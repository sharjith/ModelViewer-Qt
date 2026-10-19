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

#ifndef _JtData_Parallel_HeaderFile
#define _JtData_Parallel_HeaderFile

#ifdef __ANDROID__
  #define NO_JT_MULTITHREADING
#endif

#ifndef NO_JT_MULTITHREADING
#include <tbb/task_group.h>
#include <tbb/parallel_for.h>
#endif

//! Wrappers for TBB interface providing an option to disable multithreading
//! by defining NO_JT_MULTITHREADING symbol and some enhancements.
namespace JtData_Parallel
{
  //! A wrapper for tbb::task_group class providing correct destruction.
#ifndef NO_JT_MULTITHREADING
  class TaskGroup
  {
  public:
    template <typename F> void Run (const F& theTask) { myTBBGroup.run (theTask); }
    void Wait() { myTBBGroup.wait(); }

    ~TaskGroup()
    {
      myTBBGroup.cancel();
      myTBBGroup.wait();
    }

  protected:
    tbb::task_group myTBBGroup;
  };
#else
  class TaskGroup
  {
  public:
    template <typename F> void Run (const F& theTask) { theTask(); }
    void Wait() {}
  };
#endif

  //! A wrapper for tbb::parallel_for template function.
  template <typename Index, typename F>
  void For (Index theFirst, Index theLast, const F& theTask)
  {
#ifndef NO_JT_MULTITHREADING
    tbb::parallel_for (theFirst, theLast, theTask);
#else
    for (Index i = theFirst; i < theLast; i++)
      theTask (i);
#endif
  }
};

#endif // _JtData_Parallel_HeaderFile
