// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
// Original literal tests of the skipped-display-content counter (C3 stage A). It checks the counter only; it is not
// evidence about any authentic polyline, point or wireframe JT file.
#include <JtData_SkippedContent.hxx>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
}
void skippedContentTests(){
 JtData_SkippedContent::take(); // start from a known empty state
 check(JtData_SkippedContent::take().empty(),"Skipped-content counter must start empty");
 JtData_SkippedContent::record("polyline shape node");
 JtData_SkippedContent::record("polyline shape node");
 JtData_SkippedContent::record("point shape LOD element");
 auto first=JtData_SkippedContent::take();
 check(first.size()==2,"Two distinct kinds must be counted separately");
 check(first.at("polyline shape node")==2,"Repeated kind must accumulate");
 check(first.at("point shape LOD element")==1,"Single kind must count once");
 check(JtData_SkippedContent::take().empty(),"take() must clear the counter");
 // Records arrive from the prefetch pool as well as the build thread.
 std::vector<std::thread> threads;
 for(int t=0;t<8;++t)threads.emplace_back([]{for(int i=0;i<1000;++i)JtData_SkippedContent::record("polyline shape LOD element");});
 for(auto& thread:threads)thread.join();
 auto concurrent=JtData_SkippedContent::take();
 check(concurrent.size()==1 && concurrent.at("polyline shape LOD element")==8000,"Concurrent records must not be lost");
}
