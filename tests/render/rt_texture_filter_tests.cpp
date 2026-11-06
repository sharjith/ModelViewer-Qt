// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "RtTextureSampler.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void expect(const glm::vec4& actual, const glm::vec4& expected, const char* label)
{
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i]) > 0.00001f)
            throw std::runtime_error(label);
}
}

int main()
{
    try
    {
        RtTextureSample texture;
        texture.width = 2; texture.height = 1;
        texture.wrapS = texture.wrapT = 0x812F;
        texture.magFilter = texture.minFilter = 0x2600;
        texture.rgba8 = {255,0,0,64, 0,0,255,192};
        texture.mips = {{texture.rgba8,2,1}, {{0,255,0,255},1,1}};
        glm::vec2 uv[4] = {{.49f,.5f},{.75f,.5f},{.49f,.5f},{.49f,.5f}};
        const glm::vec4 red(1,0,0,64.0f/255), blue(0,0,1,192.0f/255), green(0,1,0,1);
        const glm::vec4 smooth = glm::mix(red,blue,.48f);
        expect(RtTextureSampling::sampleTexture(texture,uv),red,"nearest magnification/alpha");
        texture.magFilter = 0x2601;
        expect(RtTextureSampling::sampleTexture(texture,uv),smooth,"linear magnification");
        texture.magFilter = 0x2600;
        // All six minification filters: distinct source-level and mip-level selection.
        const unsigned filters[] = {0x2600,0x2601,0x2700,0x2701,0x2702,0x2703};
        const glm::vec4 expected[] = {red,smooth,green,green,
            glm::mix(red,green,.6f),glm::mix(smooth,green,.6f)};
        for (int i=0; i<6; ++i)
        {
            texture.minFilter=filters[i];
            expect(RtTextureSampling::sampleTexture(texture,uv,.6f),expected[i],"minification filter");
        }
        texture.minFilter = 0x2700;
        expect(RtTextureSampling::sampleTexture(texture,uv,.49f),red,"nearest mip below midpoint");
        expect(RtTextureSampling::sampleTexture(texture,uv,.5f),green,"nearest mip at midpoint");
        expect(RtTextureSampling::sampleTexture(texture,uv,1000),green,"last mip saturation");
        expect(RtTextureSampling::sampleTexture(texture,uv,std::numeric_limits<float>::infinity()),green,"infinite mip saturation");
        expect(RtTextureSampling::sampleTexture(texture,uv,std::numeric_limits<float>::quiet_NaN()),red,"invalid footprint base fallback");
        texture.minFilter=0x2600;
        expect(RtTextureSampling::sampleTexture(texture,uv,1000),red,"non-mip filter ignores available pyramid");
        texture.mips.clear();texture.minFilter=0x2700;
        expect(RtTextureSampling::sampleTexture(texture,uv,1000),red,"missing mip base fallback");
        texture.texCoordIndex=1;
        expect(RtTextureSampling::sampleTexture(texture,uv),blue,"independent UV channel");
        texture.texCoordIndex=0;texture.uvOffset.x=.5f;
        expect(RtTextureSampling::sampleTexture(texture,uv),blue,"transformed nearest lookup");
        texture.uvOffset.x=0;uv[0].x=1;
        expect(RtTextureSampling::sampleTexture(texture,uv),blue,"clamp endpoint");
        texture.wrapS=0x2901;
        expect(RtTextureSampling::sampleTexture(texture,uv),red,"repeat endpoint");
        texture.wrapS=0x8370;
        expect(RtTextureSampling::sampleTexture(texture,uv),blue,"mirrored endpoint");
        uv[0].x=-.25f;
        expect(RtTextureSampling::sampleTexture(texture,uv),red,"negative mirrored lookup");
        texture.wrapS=0x2901;
        expect(RtTextureSampling::sampleTexture(texture,uv),blue,"negative repeated lookup");
        // Vertical orientation retains the shader's existing pre-transform flip.
        texture.width=1;texture.height=2;texture.wrapT=0x812F;
        texture.rgba8={255,0,0,64, 0,0,255,192};uv[0]={.5f,.75f};
        expect(RtTextureSampling::sampleTexture(texture,uv),red,"single vertical flip");
        texture.width=0;
        expect(RtTextureSampling::sampleTexture(texture,uv),glm::vec4(1),"absent image");
        std::cout << "Texture filter checks passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
