// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <JtData_Limits.hxx>
#include <QImage>
#include <QRect>
#include <algorithm>
#include <cstdint>
#include <new>
#include <stdexcept>

// Original canonical RGBA8 operations. Rings/edges are reduced separately from
// the interior; generated levels use a deterministic straight-component area
// box filter, rather than relying on driver-specific automatic mip generation.
namespace Jt::TextureBorderImage {
inline QImage allocate(int width,int height) {
    if(width<1||height<1||uint64_t(width)*height*4>64u*1024u*1024u)
        throw std::runtime_error("border display image exceeds decoded byte limit");
    JtData_Limits::allocation(size_t(width)*height*4);
    QImage image(width,height,QImage::Format_RGBA8888);
    if(image.isNull())throw std::bad_alloc();
    return image;
}
inline void qualify(int width,int height,bool sampleS,bool sampleT) {
    const int longest=std::max(width,height);
    // A sampled dimension needs its factor-three footprint until the last
    // supplied level. Once it shrinks below 3, fixed normalized UVs cannot
    // retain both an interior and its two border texels.
    if(longest<2||longest>32768||(longest&(longest-1))
       ||(sampleS&&width!=longest)||(sampleT&&height!=longest))
        throw std::runtime_error("border mips require each sampled axis to be a longest power-of-two dimension (2..32768)");
}
template<class Poll>
QImage expand(const QImage& source,int width,int height,bool sampleS,bool sampleT,Poll poll) {
    if(source.size()!=QSize(width+2*int(sampleS),height+2*int(sampleT)))
        throw std::runtime_error("border display source dimensions mismatch");
    auto display=allocate((sampleS?3:1)*width,(sampleT?3:1)*height);
    for(int y=0;y<display.height();++y){
        poll();const int sy=sampleT?std::clamp(y-height+1,0,height+1):y;
        const auto* row=source.constScanLine(sy);auto* out=display.scanLine(y);
        for(int x=0;x<display.width();++x){
            if((x&1023)==0)poll();
            const int sx=sampleS?std::clamp(x-width+1,0,width+1):x;
            for(int c=0;c<4;++c)out[4*x+c]=row[4*sx+c];
        }
    }
    return display;
}
template<class Poll>
QImage reduce(const QImage& source,int width,int height,bool sampleS,bool sampleT,Poll poll) {
    if(source.size()!=QSize(width+2*int(sampleS),height+2*int(sampleT)))
        throw std::runtime_error("border reduction source dimensions mismatch");
    const int nextW=std::max(1,width/2),nextH=std::max(1,height/2);
    auto target=allocate(nextW+2*int(sampleS),nextH+2*int(sampleT));
    const auto region=[&](QRect from,QRect to){
        for(int y=0;y<to.height();++y){
            poll();auto* dst=target.scanLine(to.y()+y);
            for(int x=0;x<to.width();++x){
                if((x&1023)==0)poll();
                uint64_t total[4]={};
                const int x0=int(int64_t(x)*from.width()/to.width());
                const int x1=int((int64_t(x+1)*from.width()+to.width()-1)/to.width());
                const int y0=int(int64_t(y)*from.height()/to.height());
                const int y1=int((int64_t(y+1)*from.height()+to.height()-1)/to.height());
                for(int sy=y0;sy<y1;++sy){
                    const auto* src=source.constScanLine(from.y()+sy);
                    const int64_t wy=std::min(int64_t(sy+1)*to.height(),int64_t(y+1)*from.height())
                        -std::max(int64_t(sy)*to.height(),int64_t(y)*from.height());
                    for(int sx=x0;sx<x1;++sx){
                        const int64_t wx=std::min(int64_t(sx+1)*to.width(),int64_t(x+1)*from.width())
                            -std::max(int64_t(sx)*to.width(),int64_t(x)*from.width());
                        for(int c=0;c<4;++c)total[c]+=uint64_t(wx*wy)*src[4*(from.x()+sx)+c];
                    }
                }
                const uint64_t weight=uint64_t(from.width())*from.height();
                for(int c=0;c<4;++c)dst[4*(to.x()+x)+c]=uchar((total[c]+weight/2)/weight);
            }
        }
    };
    const int s=int(sampleS),t=int(sampleT);
    region(QRect(s,t,width,height),QRect(s,t,nextW,nextH));
    if(sampleS)for(int edge=0;edge<2;++edge)
        region(QRect(edge*(width+1),t,1,height),QRect(edge*(nextW+1),t,1,nextH));
    if(sampleT)for(int edge=0;edge<2;++edge)
        region(QRect(s,edge*(height+1),width,1),QRect(s,edge*(nextH+1),nextW,1));
    if(sampleS&&sampleT)for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        target.setPixelColor(x*(nextW+1),y*(nextH+1),source.pixelColor(x*(width+1),y*(height+1)));
    return target;
}
}
