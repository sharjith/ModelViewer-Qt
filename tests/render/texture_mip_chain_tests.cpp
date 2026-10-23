// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "TextureMipChain.h"
#include "RtTextureSampler.h"
#include "Utils.h"
#include <QCoreApplication>
#include <iostream>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void reject(F action){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,"Invalid authored mip accepted");}
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);
    try {
        QImage base(4,4,QImage::Format_RGBA8888);base.fill(Qt::red);
        QImage green(2,2,QImage::Format_RGBA8888);green.fill(QColor(0,255,0,128));
        QImage yellow(1,1,QImage::Format_RGBA8888);yellow.fill(Qt::yellow);
        TextureMipChain::attach(base,{green,yellow});
        QByteArray png;QBuffer output(&png);output.open(QIODevice::WriteOnly);check(base.save(&output,"PNG"),"Cannot save carrier");
        auto loaded=convertToGLFormat(QImage::fromData(png,"PNG"));
        auto levels=TextureMipChain::decode(loaded);
        check(loaded.pixelColor(0,0)==QColor(Qt::red)&&levels.size()==2,"PNG/conversion lost carrier");
        check(levels[0].pixelColor(0,0)==QColor(0,255,0,128)&&levels[1].pixelColor(0,0)==QColor(Qt::yellow),"Authored pixels or alpha changed");
        RtTextureSample sample;sample.width=4;sample.height=4;sample.minFilter=0x2700;sample.magFilter=0x2600;
        auto bytes=[](const QImage& image){std::vector<uint8_t> result;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);for(auto v:{c.red(),c.green(),c.blue(),c.alpha()})result.push_back(uint8_t(v));}return result;};
        sample.rgba8=bytes(loaded);sample.mips.push_back({sample.rgba8,4,4});
        for(const auto& level:levels)sample.mips.push_back({bytes(level),level.width(),level.height()});
        glm::vec2 uv[4]={{.5f,.5f},{},{},{}};
        check(RtTextureSampling::sampleTexture(sample,uv,1)==glm::vec4(0,1,0,128.f/255),"Authored level was regenerated from red base");
        check(RtTextureSampling::sampleTexture(sample,uv,100)==glm::vec4(1,1,0,1),"Authored tail not sampled");
        reject([&]{TextureMipChain::attach(base,{green});});
        reject([&]{TextureMipChain::attach(base,{yellow,yellow});});
        reject([&]{TextureMipChain::attach(base,{green,yellow,yellow});});
        for(const auto& text:{QString("[]"),QString("not-json"),QString("[12]"),QString("[\"not base64!\"]")}){
            auto bad=base;bad.setText(QLatin1String(TextureMipChain::Key),text);reject([&]{TextureMipChain::decode(bad);});
        }
        QImage bordered(12,12,QImage::Format_RGBA8888);bordered.fill(Qt::red);
        QImage borderMid(6,6,QImage::Format_RGBA8888);borderMid.fill(Qt::green);
        QImage borderEnd(3,3,QImage::Format_RGBA8888);borderEnd.fill(Qt::yellow);
        bordered.setText(QLatin1String(TextureMipChain::TerminalKey),QLatin1String(TextureMipChain::BorderTerminal));
        TextureMipChain::attach(bordered,{borderMid,borderEnd});
        check(TextureMipChain::decode(bordered).back().size()==QSize(3,3),"Border terminal chain rejected");
        auto tailPixels=borderEnd;tailPixels.fill(Qt::magenta);tailPixels.setPixelColor(1,1,Qt::yellow);
        RtTextureSample borderSample;borderSample.width=borderSample.height=12;borderSample.rgba8=bytes(bordered);
        borderSample.minFilter=0x2700;borderSample.magFilter=0x2600;
        borderSample.mips={{bytes(bordered),12,12},{bytes(borderMid),6,6},{bytes(tailPixels),3,3}};
        check(RtTextureSampling::sampleTexture(borderSample,uv,100)==glm::vec4(1,1,0,1),"Terminal 3x3 interior was not sampled at max LOD");
        glm::vec2 outside[4]={{0,0},{},{},{}};
        check(RtTextureSampling::sampleTexture(borderSample,outside,100)==glm::vec4(1,0,1,1),"Terminal 3x3 border disappeared at max LOD");
        auto unflagged=bordered;unflagged.setText(QLatin1String(TextureMipChain::TerminalKey),QString());
        reject([&]{TextureMipChain::decode(unflagged);});
        auto unknown=bordered;unknown.setText(QLatin1String(TextureMipChain::TerminalKey),"unknown");
        reject([&]{TextureMipChain::decode(unknown);});
        reject([&]{TextureMipChain::attach(bordered,{borderMid});});
        QByteArray borderPng;QBuffer borderOut(&borderPng);borderOut.open(QIODevice::WriteOnly);
        check(bordered.save(&borderOut,"PNG"),"Cannot save border chain");
        check(TextureMipChain::decode(convertToGLFormat(QImage::fromData(borderPng,"PNG"))).back().size()==QSize(3,3),"Border terminal metadata lost in PNG/format conversion");
        auto ordinary=base;ordinary.setText(QLatin1String(TextureMipChain::Key),QString());check(TextureMipChain::decode(ordinary).empty(),"Ordinary image acquired authored levels");
        std::cout<<"Authored mip PNG/conversion, literal alpha/level sampling and malformed-chain checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
