// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "ScopedMaterialWireframe.h"
#include "Material.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool modeIs(QOpenGLFunctions_4_5_Core& gl,GLenum mode) {
    GLint modes[2]={0,0};gl.glGetIntegerv(GL_POLYGON_MODE,modes);
    if(modes[0]!=GLint(mode))
        std::cerr<<"Polygon modes "<<std::hex<<modes[0]<<'/'<<modes[1]<<", expected "<<mode<<", GL error "<<gl.glGetError()<<std::dec<<'\n';
    return modes[0]==GLint(mode);
}
int whitePixels(const QImage& image) {
    int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) if(image.pixelColor(x,y).red()>127) ++count;
    return count;
}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    QSurfaceFormat format;format.setVersion(4,5);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);
    if(!context.create()) {std::cout<<"SKIP: desktop OpenGL context unavailable\n";return 77;}
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
    if(!surface.isValid() || !context.makeCurrent(&surface)) {std::cout<<"SKIP: offscreen OpenGL context unavailable\n";return 77;}
    QOpenGLFunctions_4_5_Core gl;
    if(!gl.initializeOpenGLFunctions()) {std::cout<<"SKIP: OpenGL 4.5 core unavailable\n";return 77;}
    try {
        for(bool wire:{false,true}) {
            Material source;source.setWireframe(wire);
            const auto map=source.toVariantMap();check(map.contains("wireframe") && map["wireframe"].toBool()==wire,"Wireframe missing from material serialization");
            const auto json=QJsonDocument(QJsonObject::fromVariantMap(map)).toJson();
            const auto restored=Material::fromVariantMap(QJsonDocument::fromJson(json).object().toVariantMap());
            check(restored.wireframe()==wire && restored.twoSided()==source.twoSided(),"Material runtime JSON lost wireframe");
        }
        check(!Material::fromVariantMap(QVariantMap()).wireframe(),"Legacy materials unexpectedly enable wireframe");
        std::cout<<"OpenGL "<<gl.glGetString(GL_VERSION)<<" / "<<gl.glGetString(GL_RENDERER)<<'\n';
        for(GLenum incoming:{GL_FILL,GL_LINE,GL_POINT}) {
            gl.glPolygonMode(GL_FRONT_AND_BACK,incoming);
            {ScopedMaterialWireframe style(gl,true);check(modeIs(gl,GL_LINE),"Wire material did not select line mode");}
            check(modeIs(gl,incoming),"Wire material leaked polygon mode");
            {ScopedMaterialWireframe style(gl,false);check(modeIs(gl,incoming),"Disabled style changed caller mode");}
            check(modeIs(gl,incoming),"Disabled style leaked polygon mode");
        }
        gl.glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        {
            ScopedMaterialWireframe outer(gl,true);
            {ScopedMaterialWireframe inner(gl,true);check(modeIs(gl,GL_LINE),"Nested style lost line mode");}
            check(modeIs(gl,GL_LINE),"Nested style reset outer mode");
        }
        check(modeIs(gl,GL_FILL),"Nested style did not restore fill");
        try {ScopedMaterialWireframe style(gl,true);throw std::runtime_error("deliberate unwind");}catch(const std::runtime_error&) {}
        check(modeIs(gl,GL_FILL),"Exception leaked wireframe state");

        QOpenGLShaderProgram program;
        check(program.addShaderFromSourceCode(QOpenGLShader::Vertex,"#version 450 core\nlayout(location=0) in vec2 position;void main(){gl_Position=vec4(position,0,1);}"),"Vertex shader failed");
        check(program.addShaderFromSourceCode(QOpenGLShader::Fragment,"#version 450 core\nout vec4 color;void main(){color=vec4(1);}"),"Fragment shader failed");
        check(program.link() && program.bind(),"Shader program failed");
        GLuint vao=0,vbo=0;gl.glGenVertexArrays(1,&vao);gl.glBindVertexArray(vao);gl.glGenBuffers(1,&vbo);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo);
        const GLfloat vertices[]={-0.75f,-0.75f,0.75f,-0.75f,0,0.75f};
        gl.glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);
        QOpenGLFramebufferObject framebuffer(128,128);check(framebuffer.isValid(),"Framebuffer unavailable");
        const auto draw=[&](bool wire) {
            check(framebuffer.bind(),"Cannot bind framebuffer");gl.glViewport(0,0,128,128);gl.glDisable(GL_DEPTH_TEST);gl.glDisable(GL_BLEND);gl.glDisable(GL_CULL_FACE);
            gl.glClearColor(0,0,0,1);gl.glClear(GL_COLOR_BUFFER_BIT);program.bind();gl.glBindVertexArray(vao);
            {ScopedMaterialWireframe style(gl,wire);gl.glDrawArrays(GL_TRIANGLES,0,3);gl.glFinish();}
            check(modeIs(gl,GL_FILL),"Triangle draw leaked polygon mode");return framebuffer.toImage();
        };
        const auto wire=draw(true),solidAfterWire=draw(false),wireAgain=draw(true),solidAgain=draw(false);
        const int lineCoverage=whitePixels(wire),fillCoverage=whitePixels(solidAfterWire);
        check(lineCoverage>100 && lineCoverage<1000 && fillCoverage>3000,"Unexpected line/fill raster coverage");
        check(wire.pixelColor(64,64).red()==0 && solidAfterWire.pixelColor(64,64).red()==255,"Wireframe did not leave the triangle interior open");
        check(whitePixels(wireAgain)==lineCoverage && whitePixels(solidAgain)==fillCoverage,"Material switching leaked raster state");
        if(app.arguments().size()==3 && app.arguments()[1]=="--artifacts") {
            const QDir out(app.arguments()[2]);check(QDir().mkpath(out.path()),"Cannot create artifact folder");
            check(wire.save(out.filePath("wireframe.png")) && solidAfterWire.save(out.filePath("solid-after-wireframe.png")),"Cannot save raster artifacts");
        }
        check(gl.glGetError()==GL_NO_ERROR,"OpenGL error during material wireframe tests");
        gl.glDeleteBuffers(1,&vbo);gl.glDeleteVertexArrays(1,&vao);
        std::cout<<"PASS: wire/fill coverage "<<lineCoverage<<"/"<<fillCoverage<<", caller modes, nesting, unwind and alternating materials\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
