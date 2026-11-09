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
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling; the element reader was
// replaced on 2026-11-01 by the JT 10.6 section 8.3 layout (see MODIFICATIONS.md and PROVENANCE.json).

#ifndef _JtElement_MetaData_PMIManager_HeaderFile
#define _JtElement_MetaData_PMIManager_HeaderFile

#include <JtData_Object.hxx>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// PMI Manager Meta Data Element, as written into the PMI data segment (segment type 3) by JT 10.x writers.
//
// Reads the layout of the JT 10.6 format description, section 8.3, for element version 5 (the only version observed in
// authentic files, Solid Edge 2026 JT 10.10): every PMI item is a Generic PMI Entity with a typed 2D-data block, non-text
// polylines in the part's frame and key/value string properties. Associations (counted, their five words skipped) and model views
// (name, camera and id kept) are read since 7 November 2026, from a Solid Edge export with two named PMI model views; design groups,
// user attributes, 2D text entities, CAD tags and the version 3 additions are not read: a file that uses them is rejected with an
// explicit diagnostic instead of being misread, and the unread trailing bytes of the element are skipped by the caller. Replaces the
// upstream reader, which modelled an older layout (typed per-kind collections) that these files do not use.
class JtElement_MetaData_PMIManager : public JtData_Object
{
public:
  struct Property
  {
    std::u16string Key;
    std::u16string Value;
  };

  // A named PMI model view (JT 10.6, 8.3.5). Which PMI belongs to it is in the associations, which are not decoded.
  struct ModelView
  {
    std::u16string Name;
    std::int32_t Id = 0;
    std::int32_t Active = 0;
    float EyeDirection[3] = {};
    float Angle = 0.f;
    float EyePosition[3] = {};
    float TargetPoint[3] = {};
  };

  struct Entity
  {
    std::int32_t UserLabel = 0;
    bool HasFrame = false;
    float Frame[9] = {}; // origin, X-axis point, Y-axis point
    float TextHeight = 0.f;
    std::uint8_t SymbolValid = 0;
    // Non-text polylines: vertices are 3D; indices, types and widths are as stored.
    std::vector<std::int32_t> PolylineIndices;
    std::vector<std::int16_t> PolylineTypes;
    std::vector<std::int16_t> PolylineWidths;
    std::vector<float> Vertices;
    std::vector<Property> Properties;
    std::u16string TypeName;
    std::u16string ParentTypeName;
    std::uint16_t EntityType = 0;
    std::uint16_t ParentType = 0;
    std::uint16_t UserFlags = 0;
  };

  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  std::uint8_t LocalVersion() const { return myVersion; }
  const std::vector<std::u16string>& Strings() const { return myStrings; }
  const std::vector<Entity>& Entities() const { return myEntities; }
  const std::vector<ModelView>& ModelViews() const { return myModelViews; }
  std::uint32_t AssociationCount() const { return myAssociationCount; }

  DEFINE_STANDARD_RTTIEXT(JtElement_MetaData_PMIManager, JtData_Object)
  DEFINE_OBJECT_CLASS (JtElement_MetaData_PMIManager)

protected:
  std::uint8_t myVersion = 0;
  std::vector<std::u16string> myStrings;
  std::vector<Entity> myEntities;
  std::vector<ModelView> myModelViews;
  std::uint32_t myAssociationCount = 0;
};

DEFINE_STANDARD_HANDLE(JtElement_MetaData_PMIManager, JtData_Object)

#endif // _JtElement_MetaData_PMIManager_HeaderFile
