#pragma once

#include <map>
#include <glm/glm.hpp>

#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>

// A renderer class for rendering text displayed by a font loaded using the
// FreeType library. A single font is loaded, processed into a list of Character
// items for later rendering.
class TextRenderer : public QOpenGLFunctions_4_5_Core
{
	/// Holds all state information relevant to a character as loaded using FreeType
	struct Character {
		unsigned int TextureID;   // ID handle of the glyph texture
		glm::ivec2 Size;    // Size of glyph
		glm::ivec2 Bearing; // Offset from baseline to left/top of glyph
		unsigned int Advance;     // Horizontal offset to advance to next glyph
	};

public:
	enum class VAlignment { VCENTER, VTOP, VBOTTOM };
	enum class HAlignment { HCENTER, HLEFT, HRIGHT };

public:
	// Constructor
	TextRenderer(QOpenGLShaderProgram* prog, unsigned int width, unsigned int height);
    ~TextRenderer();
	void deleteTextures();
	// Pre-compiles a list of characters from the given font
	void Load(std::string font, unsigned int fontSize);
	// Renders a string of text using the precompiled list of characters
	void RenderText(std::string text, float x, float y, float scale, glm::vec3 color = glm::vec3(1.0f),
		VAlignment vAlignment = VAlignment::VTOP, HAlignment _hAlignment = HAlignment::HLEFT);

	void render()
	{
		//Dummy implementation
	}
	unsigned int width() const;
	void setWidth(const unsigned int& width);

	unsigned int height() const;
	void setHeight(const unsigned int& height);

private:
	// Holds a list of pre-compiled Characters
	std::map<GLchar, Character> _characters;

	// Shader Program
	QOpenGLShaderProgram* _prog;
	// Render state
	QOpenGLVertexArrayObject _charVAO;
	QOpenGLBuffer _charVBO;

	unsigned int _width;
	unsigned int _height;
	unsigned int _fontSize;

	VAlignment _vAlignment;
	HAlignment _hAlignment;
};
