// Original ModelViewer-Qt addition, 2026-10-18; provenance recorded 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// ModelViewer additions, GPLv3. Per-import bounded decoding and cancellation.
// ModelViewer update, 2026-10-26: larger bounded production-assembly budgets
// and distinct single-allocation/cumulative diagnostics with byte counts.
#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace JtData_Limits {
inline thread_local std::function<void()> check;
inline thread_local size_t allocated=0;
inline constexpr size_t defaultAllocationBudget=size_t(16)*1024*1024*1024;
inline constexpr size_t singleAllocationLimit=size_t(512)*1024*1024;
inline thread_local size_t allocationBudget=defaultAllocationBudget;
inline void poll() {if(check) check();}
inline void allocation(size_t bytes) {
  poll();
  const auto details=[&] {
    return ": requested="+std::to_string(bytes)+" bytes; charged before request="+
      std::to_string(allocated)+" bytes; cumulative budget="+std::to_string(allocationBudget)+
      " bytes; single-allocation cap="+std::to_string(singleAllocationLimit)+" bytes";
  };
  if(bytes>singleAllocationLimit)
    throw std::runtime_error("JT decoding single-allocation limit exceeded"+details());
  if(bytes>allocationBudget || allocated>allocationBudget-bytes)
    throw std::runtime_error("JT decoding cumulative allocation budget exceeded"+details());
  allocated+=bytes;
}
struct Scope {
  std::function<void()> previous;size_t previousAllocation,previousBudget;
  explicit Scope(std::function<void()> c,size_t budget=defaultAllocationBudget)
    :previous(std::move(check)),previousAllocation(allocated),previousBudget(allocationBudget)
    {check=std::move(c);allocated=0;allocationBudget=budget;}
  ~Scope(){check=std::move(previous);allocated=previousAllocation;allocationBudget=previousBudget;}
};
}
template<class T> class JtCheckedVector : public std::vector<T> {
public:
  using std::vector<T>::vector;
  T& operator[](size_t i){return this->at(i);}
  const T& operator[](size_t i)const{return this->at(i);}
};
