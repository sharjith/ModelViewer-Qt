// JT format reading and visualization tools
// Copyright (C) 2013-2015 OPEN CASCADE SAS
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
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling; the element reader was
// replaced on 2026-11-01 by the JT 10.6 section 8.3 layout (see MODIFICATIONS.md and PROVENANCE.json).

#include <JtElement_MetaData_PMIManager.hxx>
#include <JtData_Reader.hxx>
#include <JtData_Limits.hxx>

#include <limits>
#include <stdexcept>
#include <string>

IMPLEMENT_STANDARD_HANDLE (JtElement_MetaData_PMIManager, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_MetaData_PMIManager, JtData_Object)

IMPLEMENT_OBJECT_CLASS(JtElement_MetaData_PMIManager, "PMI Manager Meta Data Object",
                       "ce357249-38fb-11d1-a5-06-00-60-97-bd-c6-e1")

namespace
{
  constexpr std::uint32_t NoString = 0xFFFFFFFFu;
  constexpr std::size_t MaxEntities = 1u << 20;
  constexpr std::size_t MaxStrings = 1u << 22;
  constexpr std::size_t MaxProperties = 1u << 16;

  // Bounded element-level reads on top of JtData_Reader: counts must be plausible against the bytes the element still has.
  struct Cursor
  {
    JtData_Reader& R;

    [[noreturn]] static void Fail (const char* theWhat)
    {
      throw std::runtime_error (std::string ("Truncated or invalid JT PMI Manager element: ") + theWhat);
    }

    std::uint8_t U8 (const char* theWhat) { Jt_U8 v; if (!R.ReadU8 (v)) Fail (theWhat); return v; }
    std::int16_t I16 (const char* theWhat) { Jt_I16 v; if (!R.ReadI16 (v)) Fail (theWhat); return v; }
    std::uint16_t U16 (const char* theWhat) { Jt_U16 v; if (!R.Read (v)) Fail (theWhat); return v; }
    std::int32_t I32 (const char* theWhat) { Jt_I32 v; if (!R.ReadI32 (v)) Fail (theWhat); return v; }
    std::uint32_t U32 (const char* theWhat) { Jt_U32 v; if (!R.ReadU32 (v)) Fail (theWhat); return v; }
    float F32 (const char* theWhat) { Jt_F32 v; if (!R.ReadF32 (v)) Fail (theWhat); return v; }

    std::size_t Count (std::size_t theItemBytes, std::size_t theLimit, const char* theWhat)
    {
      const std::int32_t aCount = I32 (theWhat);
      if (aCount < 0 || std::size_t (aCount) > theLimit
       || std::size_t (aCount) > R.ElementBytesRemaining() / (theItemBytes ? theItemBytes : 1))
        Fail (theWhat);
      return std::size_t (aCount);
    }

    std::u16string String (const char* theWhat)
    {
      TCollection_ExtendedString aString;
      if (!R.ReadMbString (aString)) Fail (theWhat);
      const Standard_ExtString aData = aString.ToExtString();
      return std::u16string (reinterpret_cast<const char16_t*> (aData), std::size_t (aString.Length()));
    }

    template <class T> std::vector<T> Array (std::size_t theCount, const char* theWhat)
    {
      JtData_Limits::allocation (theCount * sizeof (T));
      std::vector<T> aValues (theCount);
      if (theCount && !R.ReadPrimitiveArray (aValues.data(), theCount)) Fail (theWhat);
      return aValues;
    }
  };

  void ReadEntity (Cursor& C, const std::vector<std::u16string>& theStrings, JtElement_MetaData_PMIManager::Entity& E)
  {
    // PMI 2D Data: base data, then text entities, then non-text polylines.
    E.UserLabel = C.I32 ("user label");
    const std::uint8_t aFrameFlag = C.U8 ("frame flag");
    if (aFrameFlag)
    {
      E.HasFrame = true;
      for (float& aValue : E.Frame) aValue = C.F32 ("reference frame");
    }
    E.TextHeight = C.F32 ("text height");
    E.SymbolValid = C.U8 ("symbol valid flag");
    if (C.I32 ("text entity count") != 0)
      throw std::runtime_error ("JT PMI 2D text entities are not supported (no authentic sample has been decoded)");

    const std::size_t anIndexCount = C.Count (4, 1u << 24, "polyline index count");
    E.PolylineIndices = C.Array<std::int32_t> (anIndexCount, "polyline indices");
    const std::size_t aTypeCount = C.Count (2, 1u << 24, "polyline type count");
    E.PolylineTypes = C.Array<std::int16_t> (aTypeCount, "polyline types");
    const std::size_t aWidthCount = C.Count (2, 1u << 24, "polyline width count");
    E.PolylineWidths = C.Array<std::int16_t> (aWidthCount, "polyline widths");
    const std::size_t aCoordCount = C.Count (4, std::size_t (3) << 24, "vertex coordinate count");
    if (aCoordCount % 3) throw std::runtime_error ("JT PMI polyline coordinates are not 3D triples");
    E.Vertices = C.Array<float> (aCoordCount, "vertex coordinates");
    for (std::int32_t anIndex : E.PolylineIndices)
      if (anIndex < 0 || std::size_t (anIndex) > aCoordCount / 3)
        throw std::runtime_error ("JT PMI polyline index exceeds the vertex array");

    // Generic entity: properties (each atom is a string and one flag byte in version 5), type names, type codes, user flags.
    const std::size_t aPropertyCount = C.Count (10, MaxProperties, "property count");
    JtData_Limits::allocation (aPropertyCount * sizeof (JtElement_MetaData_PMIManager::Property));
    E.Properties.reserve (aPropertyCount);
    for (std::size_t i = 0; i < aPropertyCount; ++i)
    {
      JtElement_MetaData_PMIManager::Property aProperty;
      aProperty.Key = C.String ("property key");
      C.U8 ("property key flag");
      aProperty.Value = C.String ("property value");
      C.U8 ("property value flag");
      E.Properties.push_back (std::move (aProperty));
    }
    const std::uint32_t aTypeName = C.U32 ("type name id"), aParentName = C.U32 ("parent type name id");
    if (aTypeName != NoString && aTypeName < theStrings.size()) E.TypeName = theStrings[aTypeName];
    if (aParentName != NoString && aParentName < theStrings.size()) E.ParentTypeName = theStrings[aParentName];
    E.EntityType = C.U16 ("entity type");
    E.ParentType = C.U16 ("parent type");
    E.UserFlags = C.U16 ("user flags");
  }
}

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtElement_MetaData_PMIManager::Read (JtData_Reader& theReader)
{
  if (!JtData_Object::Read (theReader))
    return Standard_False;

  Cursor C{theReader};
  myVersion = C.U8 ("version");
  if (myVersion != 5)
    throw std::runtime_error ("Unsupported JT PMI Manager element version " + std::to_string (int (myVersion)));
  C.I16 ("empty field");

  const std::uint32_t aDesignGroups = C.U32 ("design group count");
  const std::uint32_t anAssociations = C.U32 ("association count");
  const std::uint32_t aUserAttributes = C.U32 ("user attribute count");
  if (aDesignGroups || anAssociations || aUserAttributes)
    throw std::runtime_error ("JT PMI design groups, associations and user attributes are not supported (no authentic sample has been decoded)");

  const std::size_t aStringCount = C.Count (4, MaxStrings, "string count");
  myStrings.reserve (aStringCount);
  for (std::size_t i = 0; i < aStringCount; ++i)
    myStrings.push_back (C.String ("string table"));

  if (C.U32 ("model view count") != 0)
    throw std::runtime_error ("JT PMI model views are not supported (no authentic sample has been decoded)");

  const std::size_t anEntityCount = C.Count (30, MaxEntities, "entity count");
  JtData_Limits::allocation (anEntityCount * sizeof (Entity));
  myEntities.reserve (anEntityCount);
  for (std::size_t i = 0; i < anEntityCount; ++i)
  {
    myEntities.emplace_back();
    ReadEntity (C, myStrings, myEntities.back());
  }
  // The trailing sections (version overlays, sort orders, association properties) are left to the caller to skip.
  return Standard_True;
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtElement_MetaData_PMIManager::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_MetaData_PMIManager version " << int (myVersion) << ", " << myEntities.size() << " entities ";
  return JtData_Object::Dump (theStream);
}
