#pragma once

#include <map>

#include <QtOpenGL>
#include <QOpenGLFunctions_4_5_Core>
#include <QVector2D>

// A renderer class for rendering text displayed by a font loaded using the
// FreeType library. A single font is loaded, processed into a list of Character
// items for later rendering.
class TextRenderer : public QOpenGLFunctions_4_5_Core
{
	/// Holds all state information relevant to a character as loaded using FreeType
	struct Character {
		unsigned int TextureID;   // ID handle of the glyph texture
		QVector2D Size;    // Size of glyph
		QVector2D Bearing; // Offset from baseline to left/top of glyph
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
	// Pre-compiles a list of characters from the given font - ASCII (0-127),
	// the printable Latin-1 Supplement range (0xA0-0xFF: degree/plus-minus/
	// multiplication sign/etc), and the diameter sign (U+2300, '⌀'). A
	// codepoint the font has no glyph for is silently skipped - RenderText()
	// just won't draw anything for it.
	void Load(std::string font, unsigned int fontSize);
	// Renders a string of text using the precompiled list of characters.
	// Single-line only - any '\n' in `text` looks up a (nonexistent) glyph
	// for it rather than starting a new line, so a caller wanting
	// multi-line text must split on '\n' itself and call this once per
	// line, offsetting y by fontSize() (see ViewportWidget::
	// drawMeasurementOverlay()'s label loop for the pattern).
	void RenderText(std::string text, float x, float y, float scale, QVector3D color = QVector3D(1.0f, 1.0f, 1.0f),
		VAlignment vAlignment = VAlignment::VTOP, HAlignment _hAlignment = HAlignment::HLEFT);

	void render()
	{
		//Dummy implementation
	}
	unsigned int width() const;
	void setWidth(const unsigned int& width);

	unsigned int height() const;
	void setHeight(const unsigned int& height);

	// The loaded font's nominal size in pixels - RenderText() has no
	// built-in concept of a line break (see its doc comment), so a caller
	// that wants multi-line text needs this to space successive
	// RenderText() calls vertically itself.
	unsigned int fontSize() const;

private:
	// Holds a list of pre-compiled Characters, keyed by UNICODE CODEPOINT
	// (not raw byte) - RenderText() decodes its input (always UTF-8, since
	// every caller passes QString::toStdString()) into codepoints before
	// lookup, so a multi-byte glyph like '°' (U+00B0) or '⌀' (U+2300,
	// needed for the Cylindrical/Conical Diameter measurement tool) round-
	// trips correctly instead of being looked up as several bogus single-
	// byte entries (which is what plain byte-indexing would do, and what
	// this class did before - see Load()'s doc comment for the loaded range).
	std::map<char32_t, Character> _characters;

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
