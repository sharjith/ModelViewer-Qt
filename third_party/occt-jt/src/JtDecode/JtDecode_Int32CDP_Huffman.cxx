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

#include <JtDecode_Int32CDP_Huffman.hxx>

#include <algorithm>
#include <vector>
#include <list>
#include <map>

/*
 * The data binded to tree nodes
 */



class HuffCodeData
{
  public:
  HuffCodeData (Standard_Integer theSymbol, Standard_Integer theAssValue)
    : mySymbol (theSymbol), myValue (theAssValue), myCodeLength (0), myBitCode (0), myIndex (0) {}

  HuffCodeData (Standard_Integer theSymbol, Standard_Integer theAssValue,
                Standard_Integer theCodeLen, Standard_Integer theBitCode)
    : mySymbol (theSymbol), myValue (theAssValue),
      myCodeLength (theCodeLen), myBitCode (theBitCode), myIndex (0) {}

  Standard_Integer Symbol()   const {return mySymbol;}
  Standard_Integer CodeLen()  const {return myCodeLength;}
  Standard_Integer BitCode()  const {return myBitCode;}
  Standard_Integer Value()    const {return myValue;}
  Standard_Integer Index()    const {return myIndex;}

  void SetSymbol   (Standard_Integer theSymbol)   {mySymbol = theSymbol;}
  void SetCodeLen  (Standard_Integer theCodeLen)  {myCodeLength = theCodeLen;}
  void SetBitCode  (Standard_Integer theBitCode)  {myBitCode = theBitCode;}
  void SetValue    (Standard_Integer theAssValue) {myValue = theAssValue;}
  void SetIndex    (Standard_Integer theIndex)    {myIndex = theIndex;}

private:
  Standard_Integer mySymbol;
  Standard_Integer myValue;
  Standard_Integer myCodeLength;
  Standard_Integer myBitCode;
  Standard_Integer myIndex;
};

bool compareCodes (const HuffCodeData* theData1, const HuffCodeData* theData2)
{
  int ret = theData2->CodeLen() - theData1->CodeLen();
  if (ret == 0)
    ret = theData2->BitCode() - theData1->BitCode();

  return ret < 0;
}

/*
 * Class intended to bind codes to the nodes of Huffman tree
 */

class HuffCodecContext
{
public:
  HuffCodecContext() : myCode (0), myCodeLength (0) {}

  Standard_Integer Code()    {return myCode;}
  Standard_Integer CodeLen() {return myCodeLength;}

  void LeftShift()       {myCode = (myCode << 1) & 0xFFFF;}
  void RightShift()      {myCode = myCode >> 1;}
  void BitOr (int value) {myCode = (myCode | value) & 0xFFFF;}
  void IncLength()       {myCodeLength++;}
  void DecLength()       {myCodeLength--;}

  void Add (HuffCodeData* data)
  {
    myCodes.push_front (data);
  }

private:

  Standard_Integer myCode;       // Current code which is constructed
  Standard_Integer myCodeLength; // Length of Huffman code currently constructed

  std::list<HuffCodeData*> myCodes;
};

/*
 * Huffman's tree node
 */
class HuffTreeNode
{
public:

  HuffTreeNode()
    : left (NULL), right (NULL), data (NULL), symCount (0) {}

  HuffTreeNode (int theSymCount, HuffCodeData* theData)
    : left (NULL), right (NULL), data (theData), symCount (theSymCount) {}

  HuffTreeNode (HuffTreeNode* theLeft, HuffTreeNode* theRight, int theSymCount, HuffCodeData* theData)
    : left (theLeft), right (theRight), data (theData), symCount (theSymCount) {}

  HuffTreeNode*        Left()                {return left;}
  HuffTreeNode*        Right()               {return right;}
  HuffCodeData*        Data()                {return data;}
  Standard_Integer     SymCount()            {return symCount;}
  Standard_Integer     Symbol()              {return data->Symbol();}
  Standard_Integer     AssociatedValue()     {return data->Value();}

  void SetCode    (Standard_Integer code)    {data->SetBitCode (code);}
  void SetCodeLen (Standard_Integer codeLen) {data->SetCodeLen (codeLen);}

  Standard_Boolean LowerThan (HuffTreeNode* o) {return symCount < o->symCount;}
  Standard_Boolean IsLeaf()                    {return (left == NULL) && (right == NULL);}

  Standard_Integer DesscendantMaxDepth()
  {
    int l = 0;
    int r = 0;

    if (left != NULL)
      l += left->DesscendantMaxDepth();

    if (right != NULL)
      r += right->DesscendantMaxDepth();

    return std::max (l, r) + 1;
  }

private:
  HuffTreeNode* left;
  HuffTreeNode* right;
  HuffCodeData* data;
  int symCount;
};

class HuffHeap
{
public:

  HuffHeap() {}

  //! Returns the size of the heap.
  Standard_Size Size()
  {
    return _heap.size();
  }

  //! Add a new tree node in a sorted way.
  void Add (HuffTreeNode* theHuffTreeNode)
  {
    _heap.push_back (theHuffTreeNode);

    Standard_Size i = _heap.size();

    while ((i != 1) && (_heap[(i / 2) - 1]->SymCount() > theHuffTreeNode->SymCount()))
    {
      // overwrite i with the parent of i
      _heap[i - 1] = _heap[(i / 2) - 1];

      // Parent of i is a new i
      i = i / 2;
    }

    // Overwrite current position (i) with the new element
    _heap[i - 1] = theHuffTreeNode;
  }

  //! Remove the root node.
  void Remove()
  {
    if (_heap.size() == 0)
    {
      return;
    }

    Standard_Size size = _heap.size();
    HuffTreeNode* y = _heap[size - 1];
    Standard_Size i = 1;                // i is current "parent", which shall be removed / overwritten
    Standard_Size ci = 2;                // ci is current "child"
    size -= 1;                // The new size is decremented by one

    while (ci <= size)
    {
      // Go to the left or to the right? Use the "smaller" element
      if ((ci < size) && (_heap[ci - 1]->SymCount() > _heap[ci]->SymCount()))
      {
        ci += 1;
      }

      // If the new "last" element already fits (it has to be smaller than the smallest
      // childs of i), than break the loop
      if (y->SymCount() < _heap[ci - 1]->SymCount())
        break;

      // Otherwise move the "child" up to the "parent" and continue with i at ci
      _heap[i - 1] = _heap[ci - 1];
      i = ci;
      ci *= 2;
    }

    // Set "last" element to the current position i
    _heap[i - 1] = y;

    // Resize node list by -1
    _heap.pop_back();
  }

  //! Returns the "smallest" node (root node) and decrease the heap.
  HuffTreeNode* Top()
  {
    if(_heap.size() == 0)
    {
      return NULL;
    }

    HuffTreeNode* huffTreeNode = _heap.front();
    Remove();
    return huffTreeNode;
  }

private:

  //! Internal list of nodes.
  std::vector<HuffTreeNode*> _heap;
};

/*
 * Create tree from the probability table
 */
static HuffTreeNode* BuildHuffmanTree (const JtDecode_ProbContextI32& theProbContext)
{
  HuffHeap aHuffHeap;

  // Initialize all the nodes and add them to the heap.
  unsigned i = 0;

  while (i < theProbContext.Size())
  {
    const JtDecode_ProbContextI32::Entry& anEntry = theProbContext[i];

    HuffCodeData* data = new HuffCodeData (anEntry.symbol, anEntry.associatedValue);
    data->SetIndex (i++);

    HuffTreeNode* node = new HuffTreeNode (anEntry.occCount, data);
    aHuffHeap.Add (node);
  }

  // Build a tree of the nodes
  while (aHuffHeap.Size() > 1)
  {
    // Get the two lowest-frequency nodes
    HuffTreeNode* node1 = aHuffHeap.Top();
    HuffTreeNode* node2 = aHuffHeap.Top();

    HuffCodeData* data = new HuffCodeData (0xdeadbeef, 0);
    data->SetIndex (i++);

    // Combine the low-freq nodes into one node
    HuffTreeNode* newNode = new HuffTreeNode (node1, node2,
            node1->SymCount() + node2->SymCount(), data);

    // Add the new node to the heap
    aHuffHeap.Add (newNode);
  }

  return aHuffHeap.Top();
}

/*
 *Performs traversing of the tree in order to bind a code to each leaf.
 *
 */
static void AssignCodeToTree (HuffTreeNode* theNode, HuffCodecContext* theContext)
{
  if (theNode->Left() != NULL)
  {
    theContext->LeftShift();
    theContext->BitOr (1);
    theContext->IncLength();

    AssignCodeToTree (theNode->Left(), theContext);

    theContext->DecLength();
    theContext->RightShift();
  }

  if (theNode->Right() != NULL)
  {
    theContext->LeftShift();
    theContext->IncLength();

    AssignCodeToTree (theNode->Right(), theContext);

    theContext->DecLength();
    theContext->RightShift();
  }

  if (theNode->Right() != NULL)
    return;

  // Set the code and its length for the node
  theNode->SetCode (theContext->Code());
  theNode->SetCodeLen (theContext->CodeLen());

  theContext->Add (theNode->Data());
}

void JtDecode_Int32CDP_Huffman::decode (int32_t* theResultPtr,
                                        int32_t* theResultEnd,
                                  const int32_t* theOOBDataPtr)
{
  // Build Huffman tree based on the context
  HuffTreeNode* aHuffTree = BuildHuffmanTree (myProbContexts[0]);

  // Make canonical
  HuffCodecContext aCodecContext;
  AssignCodeToTree (aHuffTree, &aCodecContext);

  // Decoding
  while (theResultPtr < theResultEnd)
  {
    // walk down the tree to a leaf node
    HuffTreeNode* aNode = aHuffTree;
    do
    {
      uint32_t aCurBit = ReadBit();
      aNode = aCurBit ? aNode->Left() : aNode->Right();
    }
    while (!aNode->IsLeaf());

    // construct the final value taking into account OOB data
    if (aNode->Symbol() != -2)
      *theResultPtr++ = aNode->AssociatedValue();
    else
      *theResultPtr++ = *theOOBDataPtr++;
  }
}
