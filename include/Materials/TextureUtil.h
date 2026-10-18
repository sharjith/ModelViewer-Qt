#pragma once
#include <QOpenGLFunctions>
#include <stb_image.h>
#include <iostream>

namespace TextureUtil
{

    // Load a 2D texture from disk into the CURRENT OpenGL context.
    // - path: file path
    // - srgb: true for color maps (albedo/emissive), false for data maps (metal/rough/AO/etc.)
    // - genMips: generate mipmaps
    // - wrapS/T: GL_REPEAT / GL_CLAMP_TO_EDGE
    // - minFilter/magFilter: typical pair is GL_LINEAR_MIPMAP_LINEAR / GL_LINEAR
    // - aniso: desired anisotropy level (0 = off); will be clamped to hardware max if supported
    // - flipY: whether to vertically flip the decoded image before upload
    inline GLuint loadTexture2DFromFile(const char* path,
        bool srgb = true,
        bool genMips = true,
        GLint wrapS = GL_REPEAT,
        GLint wrapT = GL_REPEAT,
        GLint minFilter = GL_LINEAR_MIPMAP_LINEAR,
        GLint magFilter = GL_LINEAR,
        float aniso = 0.0f,
        bool flipY = false)
    {
        // stb_image is global-state; set flipping once per call
        stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

        int w = 0, h = 0, comp = 0;
        // Probe the channel count without decoding.
        stbi_info(path, &w, &h, &comp);

        // Grayscale images (comp==1) would be uploaded as GL_RED, leaving G and B
        // at 0 and making the texture appear red.  Force expansion to RGBA so all
        // three colour channels are populated with the grey value.
        int reqComp = (comp == 1) ? STBI_rgb_alpha : 0;
        w = 0; h = 0;
        unsigned char* data = stbi_load(path, &w, &h, &comp, reqComp);
        if (!data)
        {
            std::cerr << "Texture failed to load: " << path << "\n";
            return 0;
        }
        if (reqComp != 0)
            comp = reqComp; // stb_image reports original comp; update to actual

        GLenum externalFmt = GL_RGBA;
        GLenum internalFmt = GL_RGBA8;

        switch (comp)
        {
        case 1: externalFmt = GL_RED; internalFmt = GL_R8; break; // never reached for grayscale
        case 2: externalFmt = GL_RG;  internalFmt = GL_RG8; break;
        case 3:
            externalFmt = GL_RGB;
            internalFmt = srgb ? GL_SRGB8 : GL_RGB8;
            break;
        case 4:
            externalFmt = GL_RGBA;
            internalFmt = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            break;
        default:
            externalFmt = GL_RGBA; internalFmt = GL_RGBA8; break;
        }

        auto* f = QOpenGLContext::currentContext()->functions();

        GLuint tex = 0;
        f->glGenTextures(1, &tex);
        f->glBindTexture(GL_TEXTURE_2D, tex);

        // Ensure tight rows
        f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        f->glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, externalFmt, GL_UNSIGNED_BYTE, data);

        if (genMips)
        {

            f->glGenerateMipmap(GL_TEXTURE_2D);
        }

        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, genMips ? minFilter : GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

        // Optional anisotropy (guard the extension)
        if (aniso > 0.0f)
        {
            GLfloat maxAniso = 0.0f;
            if (f->glGetString(GL_EXTENSIONS) && strstr((const char*)f->glGetString(GL_EXTENSIONS), "GL_EXT_texture_filter_anisotropic"))
            {
                f->glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
                GLfloat setAniso = std::min<GLfloat>(aniso, maxAniso > 0.0f ? maxAniso : 1.0f);
                f->glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, setAniso);
            }
        }

        f->glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);
        return tex;
    }

} // namespace TextureUtil
