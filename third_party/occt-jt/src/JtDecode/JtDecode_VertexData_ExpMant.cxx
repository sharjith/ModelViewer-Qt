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

#include <JtDecode_VertexData_ExpMant.hxx>

//#define NO_JT_MULTITHREADING
#include <JtData_Parallel.hxx>

JtDecode_VertexData_ExpMant::JtDecode_VertexData_ExpMant (const Standard_Size theNbComponents,
                                                                JtDecode_Unpack& theUnpacker)
  : JtDecode_VertexData (theNbComponents * 2, theUnpacker) {}

Standard_Integer JtDecode_VertexData_ExpMant::getOutCompCount (Standard_Size thePackageCount)
{
  return static_cast<Standard_Integer>(thePackageCount) / 2;
}

void JtDecode_VertexData_ExpMant::decode (Decoded::Ref theResults)
{
  JtData_Parallel::TaskGroup aDecodeTasks;
  for (Decoded::CompCountType j = 0; j < theResults.CompCount(); j++)
  {
    Jt_VecU32 anExp, aMant;
    aDecodeTasks.Run (getDecodingFunctor (j * 2 + 0, anExp));
    aDecodeTasks.Run (getDecodingFunctor (j * 2 + 1, aMant));
    aDecodeTasks.Wait();

    for (Decoded::SizeType i = 0; i < theResults.Count(); i++)
    {
      uint32_t aValue = (anExp[i] << 23) | aMant[i];
      reinterpret_cast <uint32_t&> (theResults[i][j]) = aValue;
    }
  }
}
