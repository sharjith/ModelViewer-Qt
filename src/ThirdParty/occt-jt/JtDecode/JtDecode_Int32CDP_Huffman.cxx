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
// see src/ThirdParty/occt-jt/MODIFICATIONS.md and PROVENANCE.json for provenance.

// Bounded tree ownership, declared-bit/OOB limits and single-symbol handling: 2026-10-20.
#include <JtDecode_Int32CDP_Huffman.hxx>
#include <JtData_Limits.hxx>
#include <vector>
#include <limits>

void JtDecode_Int32CDP_Huffman::decode(int32_t* output,int32_t* end,const int32_t* oob) {
  if(myProbContexts.Count()!=1)throw std::runtime_error("Unsupported JT Huffman probability context count");
  const auto& context=myProbContexts[0];
  if(!context.Size() || context.Size()>65536)throw std::runtime_error("Invalid JT Huffman tree size");
  struct Node {uint32_t frequency;int left,right,symbol,value;};
  const size_t capacity=2*context.Size()-1;
  JtData_Limits::allocation(capacity*sizeof(Node)+context.Size()*sizeof(int));
  std::vector<Node> nodes;nodes.reserve(capacity);
  std::vector<int> heap;heap.reserve(context.Size());
  // Retain the upstream JT heap's equal-frequency insertion/removal ordering.
  const auto add=[&](int node){
    heap.push_back(node);size_t i=heap.size();
    while(i!=1 && nodes[heap[i/2-1]].frequency>nodes[node].frequency){heap[i-1]=heap[i/2-1];i/=2;}
    heap[i-1]=node;
  };
  const auto pop=[&](){
    if(heap.empty())throw std::runtime_error("Invalid JT Huffman heap");
    const int result=heap.front(),last=heap.back();size_t size=heap.size()-1,i=1,child=2;
    while(child<=size){
      if(child<size && nodes[heap[child-1]].frequency>nodes[heap[child]].frequency)++child;
      if(nodes[last].frequency<nodes[heap[child-1]].frequency)break;
      heap[i-1]=heap[child-1];i=child;child*=2;
    }
    heap[i-1]=last;heap.pop_back();return result;
  };
  for(size_t i=0;i<context.Size();++i){
    if((i&1023)==0)JtData_Limits::poll();
    const auto& entry=context[i];
    if(!entry.occCount || entry.nextContext)throw std::runtime_error("Invalid JT Huffman probability entry");
    nodes.push_back({entry.occCount,-1,-1,entry.symbol,entry.associatedValue});add(int(nodes.size()-1));
  }
  while(heap.size()>1){
    if((nodes.size()&1023)==0)JtData_Limits::poll();
    const int left=pop(),right=pop();const uint64_t sum=uint64_t(nodes[left].frequency)+nodes[right].frequency;
    if(sum>std::numeric_limits<uint32_t>::max())throw std::runtime_error("JT Huffman frequency overflow");
    nodes.push_back({uint32_t(sum),left,right,0,0});add(int(nodes.size()-1));
  }
  const int root=pop();size_t usedBits=0,values=0;
  while(output<end){
    if((values++&1023)==0)JtData_Limits::poll();
    int node=root;
    while(nodes[node].left!=-1){
      if(usedBits++>=size_t(myCodeTextLength))throw std::runtime_error("Truncated JT Huffman code text");
      node=ReadBit()?nodes[node].left:nodes[node].right;
    }
    if(nodes[node].symbol!=-2)*output++=nodes[node].value;
    else {
      if(!oob || !myOOBEnd || oob>=myOOBEnd)throw std::runtime_error("JT Huffman out-of-band underflow");
      *output++=*oob++;
    }
  }
  if(oob!=myOOBEnd)throw std::runtime_error("JT Huffman unused out-of-band values");
}
