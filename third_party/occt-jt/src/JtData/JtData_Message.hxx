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

#ifndef _JtData_Message_HeaderFile
#define _JtData_Message_HeaderFile

#include <Message.hxx>
#include <Message_Messenger.hxx>

#ifdef OCCT_DEBUG
  #define MESSAGE(msg, type) \
    ::Message::DefaultMessenger()->Send (TCollection_ExtendedString() + msg, type)
#else
  #define MESSAGE(msg, type)
#endif

#define TRACE(msg)   MESSAGE(msg, Message_Trace)
#define INFO(msg)    MESSAGE(msg, Message_Info)
#define WARNING(msg) MESSAGE(msg, Message_Warning)
#define ALARM(msg)   ::Message::DefaultMessenger()->Send (TCollection_ExtendedString() + msg, Message_Alarm)
#define FAIL(msg)    MESSAGE(msg, Message_Fail)

#endif // _JtData_Message_HeaderFile
