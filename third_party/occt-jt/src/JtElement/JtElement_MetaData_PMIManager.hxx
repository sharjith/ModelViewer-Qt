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

#ifndef _JtElement_MetaData_PMIManager_HeaderFile
#define _JtElement_MetaData_PMIManager_HeaderFile

#include <JtData_Object.hxx>

#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>

class JtElement_MetaData_PMIManager : public JtData_Object
{
  class EntitiesSorter;
  class PMIReader;

public:
  // PMI collection types
  struct RefFrame2D
  {
    Jt_CoordF32 Origin;
    Jt_CoordF32 XAxisPoint;
    Jt_CoordF32 YAxisPoint;
  };

  struct TextBox
  {
    Jt_F32 OriginX, OriginY;
    Jt_F32 Right, Bottom, Left, Top;
  };

  struct PolylineData
  {
    JtData_Vector<Jt_I16, Jt_I32> SegmentIndices;
    Jt_VecF32 VertexCoords;
  };

  struct TextPolylineData
  {
  };

  struct NonTextPolylineData
  {
  };

  struct Entity
  {
  };

  struct PMIBaseData : Entity
  {
    Jt_I32 UserLabel;
    Jt_F32 TextHeight;
    Jt_U8  ValidFlag;
  };

  struct TextData2D
  {
    Jt_I32           StringID;
    Jt_I32           Font;
    struct TextBox   TextBox;
    TextPolylineData PolylineData;
  };

  struct PMI2DData : PMIBaseData
  {
    JtData_Vector <TextData2D, Jt_I32> TextEntities;
    NonTextPolylineData PolylineData;
  };

  struct PMI3DData : PMIBaseData
  {
    Jt_I32 StringID;
  };

  // Specific PMI entity classes
  struct Note : PMI2DData
  {
  };

  struct SpotWeld : PMI3DData
  {
  };

  struct MeasurePoint : PMI3DData
  {
  };

  struct DesignGroupAttribute
  {
    Jt_I32 Type;
    union
    {
      Jt_I32 Int;
      Jt_F64 Double;
      Jt_I32 StringID;
    };
    Jt_I32 LabelStringID;
    Jt_I32 DescrStringID;
  };

  struct DesignGroup : Entity
  {
    Jt_I32 NameStringID;
  };

  struct CoordSystem : Entity
  {
    Jt_I32 NameStringID;
  };

  struct PMIEntities
  {
    JtData_Vector <PMI2DData   , Jt_I32> DimensionEntities;
    JtData_Vector <Note        , Jt_I32> NoteEntities;
    JtData_Vector <PMI2DData   , Jt_I32> DFSEntities;
    JtData_Vector <PMI2DData   , Jt_I32> DatumTargetEntities;
    JtData_Vector <PMI2DData   , Jt_I32> FCFEntities;
    JtData_Vector <PMI2DData   , Jt_I32> LineWeldEntities;
    JtData_Vector <SpotWeld    , Jt_I32> SpotWeldEntities;
    JtData_Vector <PMI2DData   , Jt_I32> SFEntities;
    JtData_Vector <MeasurePoint, Jt_I32> MPEntities;
    JtData_Vector <PMI2DData   , Jt_I32> LocatorEntities;
    JtData_Vector <PMI3DData   , Jt_I32> RefGeometryEntities;
    JtData_Vector <DesignGroup , Jt_I32> DesignGroupEntities;
    JtData_Vector <CoordSystem , Jt_I32> CoordSystemEntities;
  };

  struct Association
  {
    union
    {
      struct { Jt_I32 ID : 24, Type : 7, Indirect : 1; } Fwd;
      struct { Jt_I32 Indirect : 1, Type : 7, ID : 24; } Rev;
    } SourceData, DestinationData;
    Jt_I32 ReasonCode;
    Jt_I32 SrcOwnerStringID;
    Jt_I32 DstOwnerStringID;
  };

  struct UserAttribute
  {
    Jt_I32 KeyStringID;
    Jt_I32 ValueStringID;
  };

  struct ModelView : Entity
  {
    Jt_I32 NameStringID;
  };

  struct PropertyAtom : TCollection_ExtendedString
  {
  };

  struct Property
  {
    PropertyAtom Key;
    PropertyAtom Value;
  };

  struct GenericEntity : PMI2DData
  {
    JtData_Vector <Property, Jt_I32> Properties;
    Jt_I32 EntityTypeStringID;
    Jt_I32 ParentTypeStringID;
    Jt_U16 EntityType;
    Jt_U16 ParentType;
  };

  struct Entities : PMIEntities
  {
    JtData_Vector <GenericEntity, Jt_I32> GenericEntities;
  };

public:
  //! Read this entity from a JT file.
  Standard_EXPORT virtual Standard_Boolean Read (JtData_Reader& theReader);

  //! Dump this entity.
  Standard_EXPORT virtual Standard_Integer Dump (Standard_OStream& theStream) const;

  //! Entities.
  const Entities& GetEntities() const { return myEntities; }

  //! Obtain string by ID.
  const TCollection_AsciiString& String (Jt_I32 theID) const { return myStringTable[theID]; }

  DEFINE_STANDARD_RTTIEXT(JtElement_MetaData_PMIManager, JtData_Object)
  DEFINE_OBJECT_CLASS (JtElement_MetaData_PMIManager)

protected:
  Standard_Boolean SortEntities (EntitiesSorter theSorter) const;

protected:
  Entities myEntities;
  JtData_Vector <UserAttribute          , Jt_I32> myUserAttributes;
  JtData_Vector <TCollection_AsciiString, Jt_I32> myStringTable;
  JtData_Vector <ModelView              , Jt_I32> myModelViews;
};

DEFINE_STANDARD_HANDLE(JtElement_MetaData_PMIManager, JtData_Object)

#endif // _JtElement_MetaData_PMIManager_HeaderFile
