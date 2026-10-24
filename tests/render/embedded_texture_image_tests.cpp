// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "EmbeddedTextureImage.h"
#include <QCoreApplication>
#include <iostream>
#include <initializer_list>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try
    {
        QImage base(4,4,QImage::Format_RGBA8888);
        base.fill(QColor(20,40,60,128));
        base.setPixelColor(0,0,QColor(255,0,0,0));
        base.setPixelColor(0,3,QColor(0,0,255,255));
        QImage mid(2,2,QImage::Format_RGBA8888); mid.fill(QColor(0,255,0,64));
        QImage tail(1,1,QImage::Format_RGBA8888); tail.fill(Qt::yellow);
        TextureMipChain::attach(base,{mid,tail});
        const auto png = EmbeddedTextureImage::png(base);
        const auto loaded = QImage::fromData(png,"PNG");
        check(loaded.pixelColor(0,0)==QColor(255,0,0,0)
            && loaded.pixelColor(0,3)==QColor(0,0,255,255)
            && loaded.pixelColor(1,1)==QColor(20,40,60,128),
            "Resident PNG changed orientation, RGB or alpha");
        const auto levels = TextureMipChain::decode(loaded);
        check(levels.size()==2 && levels[0].pixelColor(0,0)==QColor(0,255,0,64)
            && levels[1].pixelColor(0,0)==QColor(Qt::yellow),"Resident PNG lost authored mips");
        auto alternative=base;
        tail.fill(Qt::magenta); TextureMipChain::attach(alternative,{mid,tail});
        check(EmbeddedTextureImage::png(alternative)!=png,"Different mip tails have identical payloads");
        QImage bordered(6,6,QImage::Format_RGBA8888); bordered.fill(Qt::red);
        QImage borderTail(3,3,QImage::Format_RGBA8888); borderTail.fill(Qt::cyan);
        bordered.setText(QLatin1String(TextureMipChain::TerminalKey),QLatin1String(TextureMipChain::BorderTerminal));
        TextureMipChain::attach(bordered,{borderTail});
        auto roundTrip=QImage::fromData(EmbeddedTextureImage::png(bordered),"PNG");
        check(roundTrip.text(QLatin1String(TextureMipChain::TerminalKey))==QLatin1String(TextureMipChain::BorderTerminal)
            && TextureMipChain::decode(roundTrip).back().size()==QSize(3,3),"Border mip terminal lost");
        for (auto invalid : {QImage(),base})
        {
            if (!invalid.isNull()) invalid.setText(QLatin1String(TextureMipChain::Key),"not-json");
            bool rejected=false;
            try { EmbeddedTextureImage::png(invalid); } catch (const std::runtime_error&) { rejected=true; }
            check(rejected,"Missing/malformed resident image accepted");
        }
        std::cout << "Resident image packaging checks passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
