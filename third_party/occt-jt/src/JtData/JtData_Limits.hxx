// Original ModelViewer-Qt addition, 2026-10-18; provenance recorded 2026-10-19.
// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// ModelViewer additions, GPLv3. Per-import bounded decoding and cancellation.
#pragma once
#include <functional>
#include <stdexcept>
#include <vector>
namespace JtData_Limits {
inline thread_local std::function<void()> check;
inline thread_local size_t allocated=0;
inline void poll() {if(check) check();}
inline void allocation(size_t bytes) {
  poll();
  if(bytes>256u*1024u*1024u || allocated>size_t(1024u*1024u*1024u)-bytes)
    throw std::runtime_error("JT decoding memory limit exceeded");
  allocated+=bytes;
}
struct Scope {
  std::function<void()> previous;size_t previousAllocation;
  explicit Scope(std::function<void()> c):previous(std::move(check)),previousAllocation(allocated) {check=std::move(c);allocated=0;}
  ~Scope(){check=std::move(previous);allocated=previousAllocation;}
};
}
template<class T> class JtCheckedVector : public std::vector<T> {
public:
  using std::vector<T>::vector;
  T& operator[](size_t i){return this->at(i);}
  const T& operator[](size_t i)const{return this->at(i);}
};
