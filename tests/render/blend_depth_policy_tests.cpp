// Copyright (C) 2026 ModelViewer-Qt contributors. SPDX-License-Identifier: GPL-3.0-only
#include "BlendDepthPolicy.h"
#include <QOpenGLFunctions_4_5_Core>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QImage>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace { void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);} }

int main(int argc,char** argv)
{
    QGuiApplication app(argc,argv);
    QSurfaceFormat format;format.setVersion(4,5);format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;context.setFormat(format);
    if(!context.create()){std::cout<<"SKIP: OpenGL context unavailable\n";return 77;}
    QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
    if(!surface.isValid()||!context.makeCurrent(&surface)){std::cout<<"SKIP: offscreen surface unavailable\n";return 77;}
    QOpenGLFunctions_4_5_Core gl;
    if(!gl.initializeOpenGLFunctions()){std::cout<<"SKIP: OpenGL 4.5 unavailable\n";return 77;}
    try {
        QOpenGLShaderProgram program;
        check(program.addShaderFromSourceFile(QOpenGLShader::Vertex,QStringLiteral(MV_REPO_DIR "/shaders/main_scene.vert")),"Production vertex shader failed");
        check(program.addShaderFromSourceFile(QOpenGLShader::Fragment,QStringLiteral(MV_REPO_DIR "/shaders/main_scene.frag")),"Production fragment shader failed");
        if(!program.link())throw std::runtime_error(program.log().toStdString());
        check(program.bind(),"Cannot bind production shader");
        // Active cube and 2D samplers cannot alias a unit, even on branches
        // not exercised by this offscreen check.
        GLint uniforms=0;gl.glGetProgramiv(program.programId(),GL_ACTIVE_UNIFORMS,&uniforms);
        for(GLint i=0;i<uniforms;++i){GLchar name[512];GLsizei length=0;GLint count=0;GLenum type=0;
            gl.glGetActiveUniform(program.programId(),GLuint(i),sizeof(name),&length,&count,&type,name);
            if(type==GL_SAMPLER_CUBE)program.setUniformValue(name,1);
        }
        QMatrix4x4 identity;identity.setToIdentity();
        for(const char* name:{"modelMatrix","viewMatrix","modelViewMatrix","projectionMatrix","lightSpaceMatrix"})program.setUniformValue(name,identity);
        program.setUniformValue("normalMatrix",identity.normalMatrix());
        program.setUniformValue("worldNormalMatrix",identity.normalMatrix());
        program.setUniformValue("twoSided",true);
        program.setUniformValue("renderingMode",1);
        program.setUniformValue("blendMode",2);
        program.setUniformValue("opacity",1.0f);
        program.setUniformValue("hasAlbedoMap",true);
        program.setUniformValue("albedoMap",0);
        program.setUniformValue("hasVertexColors",false);
        program.setUniformValue("debugChannelOutput",0);
        program.setUniformValue("pbrLighting.unlit",true);
        program.setUniformValue("pbrLighting.albedo",QVector3D(1,1,1));
        program.setUniformValue("cameraPos",QVector3D(0,0,2));
        GLuint vao=0,vbo=0,texture=0;
        gl.glGenVertexArrays(1,&vao);gl.glBindVertexArray(vao);
        gl.glGenBuffers(1,&vbo);gl.glBindBuffer(GL_ARRAY_BUFFER,vbo);
        const GLfloat vertices[]={-.8f,-.8f,-.5f,.8f,-.8f,-.5f,0,.8f,-.5f,
                                   -.8f,-.8f,.5f,.8f,-.8f,.5f,0,.8f,.5f};
        gl.glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
        gl.glEnableVertexAttribArray(0);gl.glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
        gl.glVertexAttrib3f(1,0,0,1);gl.glVertexAttrib4f(2,1,1,1,1);
        gl.glVertexAttrib3f(7,1,0,0);gl.glVertexAttrib3f(8,0,1,0);
        gl.glGenTextures(1,&texture);gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,texture);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        gl.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        auto alpha=[&](unsigned char a){gl.glActiveTexture(GL_TEXTURE0);gl.glBindTexture(GL_TEXTURE_2D,texture);const unsigned char pixel[]={255,255,255,a};gl.glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);};
        QOpenGLFramebufferObjectFormat fbFormat;fbFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(64,64,fbFormat);check(framebuffer.isValid()&&framebuffer.bind(),"Depth framebuffer unavailable");
        gl.glViewport(0,0,64,64);gl.glEnable(GL_DEPTH_TEST);gl.glDisable(GL_BLEND);gl.glDisable(GL_CULL_FACE);
        auto clear=[&]{framebuffer.bind();program.bind();gl.glBindVertexArray(vao);gl.glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);gl.glDepthMask(GL_TRUE);gl.glClearColor(0,0,0,1);gl.glClearDepth(1);gl.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);};
        auto depth=[&]{framebuffer.bind();GLfloat value=0;gl.glReadPixels(32,24,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&value);return value;};
        // Colours without transparent alpha (including loader fallback white)
        // must not take the vertex-transparency depth path.
        const std::vector<float> rgbOnly={1,0,0,1,0,1,0,1};
        const std::vector<float> vertexPartial={1,1,1,1,1,1,1,.5f};
        check(!BlendDepthPolicy::hasTransparentVertexAlpha({})&&
            !BlendDepthPolicy::hasTransparentVertexAlpha(rgbOnly)&&
            BlendDepthPolicy::hasTransparentVertexAlpha(vertexPartial),"Vertex alpha classification mismatch");

        auto draw=[&](float scalar,float vertexAlpha,unsigned char textureAlpha){
            alpha(textureAlpha);
            program.setUniformValue("opacity",scalar);program.setUniformValue("hasVertexColors",true);
            gl.glVertexAttrib4f(2,1,1,1,vertexAlpha);
            const bool vertexTransparency=BlendDepthPolicy::hasTransparentVertexAlpha({1,1,1,vertexAlpha});
            const bool depthOff=BlendDepthPolicy::disableDepthWrites(scalar,true,vertexTransparency);
            gl.glDepthMask(depthOff?GL_FALSE:GL_TRUE);gl.glDrawArrays(GL_TRIANGLES,0,3);
        };
        for(unsigned char a:{255,128,0}){
            clear();gl.glDepthFunc(GL_LESS);draw(1,1,a);
            check(std::abs(depth()-.25f)<.001f,"Texture-only BLEND lost legacy surface depth");
        }
        clear();draw(.5f,1,255);
        check(depth()>.999f,"Scalar transparency unexpectedly wrote depth");
        clear();draw(1,.5f,255);
        check(depth()>.999f,"Vertex transparency unexpectedly wrote depth");
        clear();draw(1,0,255);
        check(depth()>.999f,"Zero vertex alpha unexpectedly wrote depth");
        clear();draw(1,1,255);
        check(std::abs(depth()-.25f)<.001f,"Opaque vertex colour disabled depth writes");
        check(!BlendDepthPolicy::disableDepthWrites(1,false,true),"OPAQUE/MASK vertex colour changed depth policy");

        // Texture-only shell: a later far triangle in the same mesh must not
        // show through the closer surface. This fails with blanket BLEND depth-off.
        clear();alpha(128);program.setUniformValue("opacity",1.0f);
        gl.glClearColor(1,0,0,1);gl.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        gl.glEnable(GL_BLEND);gl.glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        gl.glDepthMask(BlendDepthPolicy::disableDepthWrites(1,true,false)?GL_FALSE:GL_TRUE);
        gl.glVertexAttrib4f(2,0,0,1,1);gl.glDrawArrays(GL_TRIANGLES,0,3);
        gl.glVertexAttrib4f(2,0,1,0,1);gl.glDrawArrays(GL_TRIANGLES,3,3);
        const auto pixel=framebuffer.toImage().pixelColor(32,39);
        check(std::abs(pixel.red()-127)<=2&&pixel.green()<3&&std::abs(pixel.blue()-128)<=2,
            "Later far surface showed through texture-only BLEND shell");
        check(std::abs(depth()-.25f)<.001f,"Shell failed to retain front depth");
        check(gl.glGetError()==GL_NO_ERROR,"Depth-policy check generated an OpenGL error");
        gl.glDeleteTextures(1,&texture);gl.glDeleteBuffers(1,&vbo);gl.glDeleteVertexArrays(1,&vao);
        std::cout<<"Production-shader texture depth compatibility and vertex/scalar alpha checks passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
