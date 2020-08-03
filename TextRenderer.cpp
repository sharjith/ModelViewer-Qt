
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "TextRenderer.h"


TextRenderer::TextRenderer(QOpenGLShaderProgram* prog, GLuint width, GLuint height) : _prog(prog), _width(width), _height(height)
{
    initializeOpenGLFunctions();
    _charVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    // Load and configure shader
    QMatrix4x4 projection;
    GLuint ratio = (_width <= _height) ? _height / _width : _width / _height;
    if(_width <= _height)
        projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height)*ratio));
    else
        projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(_width)*ratio, static_cast<float>(_height)));
    _prog->setUniformValue("projection", projection);
    _prog->setAttributeValue("text", 0);
    // Configure VAO/VBO for texture quads
    //glGenVertexArrays(1, &this->VAO);
    _charVAO.create();
    //glGenBuffers(1, &this->VBO);
    _charVBO.create();
    //glBindVertexArray(this->VAO);
    _charVAO.bind();
    //glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
    _charVBO.bind();
    //glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    _charVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    _charVBO.allocate(NULL, sizeof(GLfloat) * 6 * 4);
    //glEnableVertexAttribArray(0);
    _prog->enableAttributeArray(0);
    //glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
    _prog->setAttributeBuffer(0, GL_FLOAT, 0, 4, 4 * sizeof(GLfloat));
    //glBindBuffer(GL_ARRAY_BUFFER, 0);
    _charVBO.release();
    //glBindVertexArray(0);
    _charVAO.release();
}

void TextRenderer::Load(std::string font, GLuint fontSize)
{
    _fontSize = fontSize;
    // First clear the previously loaded Characters
    this->_characters.clear();
    // Then initialize and load the FreeType library
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) // All functions return a value different than 0 whenever an error occurred
        std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
    // Load font as face
    FT_Face face;
    if (FT_New_Face(ft, font.c_str(), 0, &face))
        std::cout << "ERROR::FREETYPE: Failed to load font" << std::endl;
    // Set size to load glyphs as
    FT_Set_Pixel_Sizes(face, 0, _fontSize);
    // Disable byte-alignment restriction
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Then for the first 128 ASCII characters, pre-load/compile their characters and store them
    for (GLubyte c = 0; c < 128; c++)
    {
        // Load character glyph
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            std::cout << "Error in FreeType: Failed to load Glyph" << std::endl;
            continue;
        }
        // Generate texture
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    GL_RED,
                    face->glyph->bitmap.width,
                    face->glyph->bitmap.rows,
                    0,
                    GL_RED,
                    GL_UNSIGNED_BYTE,
                    face->glyph->bitmap.buffer
                    );
        // Set texture options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Now store character for later use
        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<GLuint>(face->glyph->advance.x)
        };
        _characters.insert(std::pair<GLchar, Character>(c, character));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // Destroy FreeType once we're finished
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

void TextRenderer::RenderText(std::string text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color,
                              VAlignment vAlignment, HAlignment hAlignment)
{
    // Activate corresponding updateMatrix state
    _prog->bind();
    _prog->setUniformValue("textColor", QVector3D(color.x, color.y, color.z));
    glActiveTexture(GL_TEXTURE0);
    //glBindVertexArray(this->VAO);
    _charVAO.bind();

    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLuint voffset, hoffset;
    if(vAlignment == VAlignment::VTOP)
        voffset = 0;
    else if(vAlignment == VAlignment::VBOTTOM)
        voffset = _fontSize;
    else
        voffset = _fontSize/2;

    if(hAlignment == HAlignment::HLEFT)
        hoffset = 0;
    else if(hAlignment == HAlignment::HRIGHT)
        hoffset = static_cast<GLuint>(_width - (text.length()*this->_characters['H'].Size.x));
    else
        hoffset = static_cast<GLuint>(_width/2 - (text.length()*this->_characters['H'].Size.x)/2);

    // Iterate through all characters
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++)
    {
        Character ch = _characters[*c];

        GLfloat xpos = x + hoffset + ch.Bearing.x * scale;
        GLfloat ypos = y - voffset + (this->_characters['H'].Bearing.y - ch.Bearing.y) * scale;

        GLfloat w = ch.Size.x * scale;
        GLfloat h = ch.Size.y * scale;
        // Update VBO for each character
        GLfloat vertices[6][4] = {
            { xpos,     ypos + h,   0.0, 1.0 },
            { xpos + w, ypos,       1.0, 0.0 },
            { xpos,     ypos,       0.0, 0.0 },

            { xpos,     ypos + h,   0.0, 1.0 },
            { xpos + w, ypos + h,   1.0, 1.0 },
            { xpos + w, ypos,       1.0, 0.0 }
        };
        // Render glyph texture over quad
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        // Update content of VBO memory
        //glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
        _charVBO.bind();
        //glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); // Be sure to use glBufferSubData and not glBufferData
        _charVBO.write(0, vertices, sizeof(vertices));

        //glBindBuffer(GL_ARRAY_BUFFER, 0);
        _charVBO.release();
        // Render quad
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Now advance cursors for next glyph
        x += (ch.Advance >> 6) * scale; // Bitshift by 6 to get value in pixels (1/64th times 2^6 = 64)
    }
    //glBindVertexArray(0);
    _charVAO.release();
    glBindTexture(GL_TEXTURE_2D, 0);

    _prog->release();
}

GLuint TextRenderer::width() const
{
    return _width;
}

void TextRenderer::setWidth(const GLuint &width)
{
    _width = width;
}

GLuint TextRenderer::height() const
{
    return _height;
}

void TextRenderer::setHeight(const GLuint &height)
{
    _height = height;
}
