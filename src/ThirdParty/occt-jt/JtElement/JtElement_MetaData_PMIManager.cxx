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
// Changes: modern OCCT compatibility, JT 10.0 decoding and bounded input handling;
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

#include <JtElement_MetaData_PMIManager.hxx>
#include <JtData_Reader.hxx>

IMPLEMENT_STANDARD_HANDLE (JtElement_MetaData_PMIManager, JtData_Object)
IMPLEMENT_STANDARD_RTTIEXT(JtElement_MetaData_PMIManager, JtData_Object)

IMPLEMENT_OBJECT_CLASS(JtElement_MetaData_PMIManager, "PMI Manager Meta Data Object",
                       "ce357249-38fb-11d1-a5-06-00-60-97-bd-c6-e1")

class JtElement_MetaData_PMIManager::EntitiesSorter
{
  Jt_I32 myIdxIdx;
  Jt_VecI32::Ref myIndices;
  JtData_Vector <Entity*, Jt_I32>::Ref myEntities;

public:
  EntitiesSorter (Jt_VecI32::Ref theIndices, JtData_Vector <Entity*, Jt_I32>::Ref theEntities)
    : myIdxIdx (0), myIndices (theIndices), myEntities (theEntities) {}

  template <class Entity, class SizeT>
  Standard_Boolean operator() (const JtData_Vector<Entity, SizeT>& theEntities)
  {
    for (SizeT i = 0; i < theEntities.Count(); i++)
    {
      if (myIdxIdx > myIndices.Count())
        return Standard_False;

      Jt_I32 anIdx = myIndices[myIdxIdx];
      if (anIdx > myEntities.Count())
        return Standard_False;

      myEntities[anIdx] = &theEntities[i];
      myIdxIdx++;
    }

    return Standard_True;
  }
};

class JtElement_MetaData_PMIManager::PMIReader : public JtData_Reader::Wrapper<PMIReader>
{
public:
  PMIReader (JtData_Reader& theReader) : JtData_Reader::Wrapper<PMIReader> (theReader) {}

  Standard_Boolean ReadObject (TextPolylineData&)
  {
    PolylineData aData;
    if (!Read (aData.SegmentIndices))
      return Standard_False;

    return aData.SegmentIndices.IsEmpty() || Read (aData.VertexCoords);
  }

  Standard_Boolean ReadObject (NonTextPolylineData&)
  {
    PolylineData aData;
    if (!Read (aData.SegmentIndices))
      return Standard_False;

    if (myVersion > 4)
    {
      JtData_Vector<Jt_I16, Jt_I32> aTypes;
      if (!Read (aTypes))
        return Standard_False;
    }

    return Read (aData.VertexCoords);
  }

  Standard_Boolean ReadObject (PMIBaseData& theData)
  {
    Jt_U8 a2DFrameFlag;
    if (!Read (theData.UserLabel)
     || !Read (a2DFrameFlag))
      return Standard_False;

    if (a2DFrameFlag)
    {
      RefFrame2D a2DRefFrame;
      if (!ReadUniformStruct<Jt_F32> (a2DRefFrame))
        return Standard_False;
    }

    if (!Read (theData.TextHeight))
      return Standard_False;

    if (myVersion > 4)
    {
      if (!Read (theData.ValidFlag))
        return Standard_False;
    }
    else
      theData.ValidFlag = 1;

    return Standard_True;
  }

  Standard_Boolean ReadObject (TextData2D& theData)
  {
    Jt_I32 aReservedI32;
    Jt_F32 aReservedF32;
    return Read (theData.StringID)
        && Read (theData.Font)
        && Read (aReservedI32)
        && Read (aReservedF32)
        && ReadUniformStruct<Jt_F32> (theData.TextBox)
        && Read (theData.PolylineData);
  }

  Standard_Boolean ReadObject (PMI2DData& theData)
  {
    return Read (static_cast <PMIBaseData&> (theData))
        && Read (theData.TextEntities)
        && Read (theData.PolylineData);
  }

  Standard_Boolean ReadObject (PMI3DData& theData)
  {
    Jt_I16 aPolylineDimensionality;
    PolylineData aPolylineData;
    return Read (static_cast <PMIBaseData&> (theData))
        && Read (theData.StringID)
        && Read (aPolylineDimensionality)
        && Read (aPolylineData.SegmentIndices)
        && Read (aPolylineData.VertexCoords);
  }

  Standard_Boolean ReadObject (Note& theData)
  {
    if (!Read (static_cast <PMI2DData&> (theData)))
      return Standard_False;

    if (myVersion > 5)
    {
      Jt_U32 anURLFlag;
      if (!Read (anURLFlag))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (SpotWeld& theData)
  {
    if (!Read (static_cast <PMI3DData&> (theData)))
      return Standard_False;

    if (myVersion >= 4)
    {
      Jt_CoordF32 aWeldPoint;
      Jt_DirF32 anApproachDirection, aClampingDirection, aNormalDirection;
      if (!Read (aWeldPoint)
       || !Read (anApproachDirection)
       || !Read (aClampingDirection)
       || !Read (aNormalDirection))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (MeasurePoint& theData)
  {
    if (!Read (static_cast <PMI3DData&> (theData)))
      return Standard_False;

    if (myVersion >= 4)
    {
      Jt_CoordF32 aLocation;
      Jt_DirF32 aMeasurementDirection, aCoordinateDirection, aNormalDirection;
      if (!Read (aLocation)
       || !Read (aMeasurementDirection)
       || !Read (aCoordinateDirection)
       || !Read (aNormalDirection))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (DesignGroupAttribute& theData)
  {
    if (!Read (theData.Type))
      return Standard_False;

    Standard_Boolean aResult = Standard_False;
    switch (theData.Type)
    {
    case 1: aResult = Read (theData.Int);      break;
    case 2: aResult = Read (theData.Double);   break;
    case 3: aResult = Read (theData.StringID); break;
    }

    return aResult && Read (theData.LabelStringID)
                   && Read (theData.DescrStringID);
  }

  Standard_Boolean ReadObject (DesignGroup& theData)
  {
    if (!Read (theData.NameStringID))
      return Standard_False;

    if (myVersion >= 3)
    {
      JtData_Vector <DesignGroupAttribute, Jt_I32> anAttributes;
      if (!Read (anAttributes))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (CoordSystem& theData)
  {
    Jt_CoordF32 anOrigin, anXAxisPoint, anYAxisPoint;
    return Read (theData.NameStringID)
        && Read (anOrigin)
        && Read (anXAxisPoint)
        && Read (anYAxisPoint);
  }

  Standard_Boolean ReadObject (PMIEntities& theData)
  {
    return Read (theData.DimensionEntities)
        && Read (theData.NoteEntities)
        && Read (theData.DFSEntities)
        && Read (theData.DatumTargetEntities)
        && Read (theData.FCFEntities)
        && Read (theData.LineWeldEntities)
        && Read (theData.SpotWeldEntities)
        && Read (theData.SFEntities)
        && Read (theData.MPEntities)
        && Read (theData.LocatorEntities)
        && Read (theData.RefGeometryEntities)
        && Read (theData.DesignGroupEntities)
        && Read (theData.CoordSystemEntities);
  }

  Standard_Boolean ReadObject (Association& theData)
  {
    return ReadUniformStruct<Jt_I32> (theData);
  }

  Standard_Boolean ReadObject (UserAttribute& theData)
  {
    return Read (theData.KeyStringID) && Read (theData.ValueStringID);
  }

  Standard_Boolean ReadObject (ModelView& theData)
  {
    Jt_DirF32   anEyeDirection;
    Jt_F32      anAngle;
    Jt_CoordF32 anEyePosition;
    Jt_CoordF32 aTargetPoint;
    Jt_CoordF32 aViewAngle;
    Jt_F32      aViewportDiameter;
    Jt_F32      aReservedF32;
    Jt_I32      aReservedI32;
    Jt_I32      anActiveFlag;
    Jt_I32      aViewID;

    return Read (anEyeDirection)
        && Read (anAngle)
        && Read (anEyePosition)
        && Read (aTargetPoint)
        && Read (aViewAngle)
        && Read (aViewportDiameter)
        && Read (aReservedF32)
        && Read (aReservedI32)
        && Read (anActiveFlag)
        && Read (aViewID)
        && Read (theData.NameStringID);
  }

  Standard_Boolean ReadObject (PropertyAtom& theData)
  {
    if (!Read (static_cast <TCollection_ExtendedString&> (theData)))
      return Standard_False;

    if (myVersion > 6)
    {
      Jt_U32 aHiddenFlag;
      if (!Read (aHiddenFlag))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (Property& theData)
  {
    return Read (theData.Key) && Read (theData.Value);
  }

  Standard_Boolean ReadObject (GenericEntity& theData)
  {
    if (!Read (static_cast <PMI2DData&> (theData)))
      return Standard_False;

    if (!Read (theData.Properties)
     || !Read (theData.EntityTypeStringID)
     || !Read (theData.ParentTypeStringID)
     || !Read (theData.EntityType)
     || !Read (theData.ParentType))
        return Standard_False;

    if (myVersion > 6)
    {
      Jt_U16 aUserFlags;
      if (!Read (aUserFlags))
        return Standard_False;
    }

    return Standard_True;
  }

  Standard_Boolean ReadObject (JtElement_MetaData_PMIManager& theData)
  {
    // Read additional version if JT version is 9.x or higher
    if (Model()->MajorVersion() > 8)
    {
      Jt_I16 aVersion;
      if (!Read (aVersion))
        return Standard_False;
    }

    // Read permanent data
    Jt_I16 aReserved;
    JtData_Vector <Association, Jt_I32> aPMIAssociations;
    if (!ReadI16 (myVersion)
     || !Read    (aReserved)
     || !Read    (static_cast <PMIEntities&> (theData.myEntities))
     || !Read    (aPMIAssociations)
     || !Read    (theData.myUserAttributes)
     || !Read    (theData.myStringTable))
      return Standard_False;

    // Read Model Views and Generic PMI Entities if present
    if (myVersion > 5)
    {
      if (!Read (theData.myModelViews)
       || !Read (theData.myEntities.GenericEntities))
        return Standard_False;
    }

    // Read CAD Tag Data if present
    JtData_Vector <Entity*, Jt_I32> anEntitiesSortedByCADTagIndices;
    if (myVersion > 7)
    {
      Jt_U32 aCADTagsFlag;
      if (!Read (aCADTagsFlag))
        return Standard_False;

      if (aCADTagsFlag == 1)
      {
        Jt_VecI32 aCADTagIndices;
        if (!Read (aCADTagIndices))
          return Standard_False;

        // read Compressed CAD Tag Data collection
        Standard_Size aStartPos = GetPosition();
        if (Model()->MajorVersion() > 8)
        {
          Jt_I16 aVersion;
          if (!Read (aVersion))
            return Standard_False;
        }

        Jt_I32 aDataLength, aVersionNumber, aCADTagCount;
        if (!Read (aDataLength) || !Read (aVersionNumber) || !Read (aCADTagCount))
          return Standard_False;

        anEntitiesSortedByCADTagIndices.Allocate (aCADTagCount);
        if (!theData.SortEntities (EntitiesSorter (aCADTagIndices, anEntitiesSortedByCADTagIndices)))
          return Standard_False;

        // skip reading the remaining Compressed CAD Tag Data
        if (!Skip (aDataLength - (GetPosition() - aStartPos)))
          return Standard_False;
      }
    }

    return Standard_True;
  }

protected:
  Standard_Integer myVersion;
};

//=======================================================================
//function : Read
//purpose  : Read this entity from a translate file
//=======================================================================
Standard_Boolean JtElement_MetaData_PMIManager::Read (JtData_Reader& theReader)
{
  return JtData_Object::Read (theReader)
    && PMIReader (theReader).Read (*this);
}

//=======================================================================
//function : Dump
//purpose  :
//=======================================================================
Standard_Integer JtElement_MetaData_PMIManager::Dump (Standard_OStream& theStream) const
{
  theStream << "JtElement_MetaData_PMIManager ";
  return JtData_Object::Dump (theStream);
}

Standard_Boolean JtElement_MetaData_PMIManager::SortEntities (EntitiesSorter theSorter) const
{
  return theSorter (myEntities.LineWeldEntities)
      && theSorter (myEntities.SpotWeldEntities)
      && theSorter (myEntities.SFEntities)
      && theSorter (myEntities.MPEntities)
      && theSorter (myEntities.RefGeometryEntities)
      && theSorter (myEntities.DatumTargetEntities)
      && theSorter (myEntities.FCFEntities)
      && theSorter (myEntities.LocatorEntities)
      && theSorter (myEntities.DimensionEntities)
      && theSorter (myEntities.DFSEntities)
      && theSorter (myEntities.NoteEntities)
      && theSorter (myModelViews)
      && theSorter (myEntities.DesignGroupEntities)
      && theSorter (myEntities.CoordSystemEntities)
      && theSorter (myEntities.GenericEntities);
}
