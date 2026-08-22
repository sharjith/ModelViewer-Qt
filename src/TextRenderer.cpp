#include <algorithm>
#include <iostream>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "TextRenderer.h"

namespace
{
// Decodes a UTF-8 byte string (what QString::toStdString() always
// produces) into Unicode codepoints. Standard 1-4 byte UTF-8 decode; an
// invalid leading byte or a truncated/malformed continuation sequence is
// skipped rather than aborting the whole string, so one bad byte can't
// blank out the rest of a label.
std::vector<char32_t> decodeUtf8(const std::string& text)
{
	std::vector<char32_t> codepoints;
	codepoints.reserve(text.size());
	size_t i = 0;
	while (i < text.size())
	{
		const unsigned char b0 = static_cast<unsigned char>(text[i]);
		char32_t cp = 0;
		int extraBytes = 0;
		if ((b0 & 0x80) == 0x00)      { cp = b0;        extraBytes = 0; }
		else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F;  extraBytes = 1; }
		else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F;  extraBytes = 2; }
		else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07;  extraBytes = 3; }
		else { ++i; continue; }  // invalid leading byte - skip it

		if (i + static_cast<size_t>(extraBytes) >= text.size())
			break;  // truncated multi-byte sequence at the end of the string

		bool valid = true;
		for (int k = 1; k <= extraBytes; ++k)
		{
			const unsigned char bk = static_cast<unsigned char>(text[i + static_cast<size_t>(k)]);
			if ((bk & 0xC0) != 0x80) { valid = false; break; }  // not a continuation byte
			cp = (cp << 6) | (bk & 0x3F);
		}
		if (valid)
			codepoints.push_back(cp);
		i += static_cast<size_t>(extraBytes) + 1;
	}
	return codepoints;
}
}

TextRenderer::TextRenderer(QOpenGLShaderProgram* prog, unsigned int width, unsigned int height) : _prog(prog), _width(width), _height(height)
{
	initializeOpenGLFunctions();
	_charVBO = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	// Load and configure shader
	QMatrix4x4 projection;
	unsigned int ratio = (_width <= _height) ? _height / _width : _width / _height;
	if (_width <= _height)
		projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height) * ratio));
	else
		projection.ortho(QRect(0.0f, 0.0f, static_cast<float>(_width) * ratio, static_cast<float>(_height)));
	_prog->setUniformValue("projection", projection);
	_prog->setUniformValue("text", 30);
	// Configure VAO/VBO for texture quads
	//glGenVertexArrays(1, &this->VAO);
	_charVAO.create();
	//glGenBuffers(1, &this->VBO);
	_charVBO.create();
	//glBindVertexArray(this->VAO);
	_charVAO.bind();
	//glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
	_charVBO.bind();
	//glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	_charVBO.setUsagePattern(QOpenGLBuffer::DynamicDraw);
	_charVBO.allocate(NULL, sizeof(float) * 6 * 4);
	//glEnableVertexAttribArray(0);
	_prog->enableAttributeArray(0);
	//glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	_prog->setAttributeBuffer(0, GL_FLOAT, 0, 4, 4 * sizeof(float));
	//glBindBuffer(GL_ARRAY_BUFFER, 0);
	_charVBO.release();
	//glBindVertexArray(0);
	_charVAO.release();
}

TextRenderer::~TextRenderer()
{
	deleteTextures();
}

void TextRenderer::deleteTextures()
{
	for (const auto& el : _characters)
	{
		unsigned int texture = el.second.TextureID;
		//std::cout << "TextRenderer::~TextRenderer : texture = " << texture << std::endl;
		glDeleteTextures(1, &texture);
	}
}

void TextRenderer::Load(std::string font, unsigned int fontSize)
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
	// clear existing textures if any
	deleteTextures();

	// ASCII (0-127) as before, plus the printable Latin-1 Supplement range
	// (0xA0-0xFF - degree/plus-minus/superscript-2-3/multiplication sign/
	// etc, common in measurement/engineering text) and the diameter sign
	// (U+2300, Miscellaneous Technical block - outside Latin-1, needed for
	// the Cylindrical/Conical Diameter measurement tool's "⌀" summary
	// text). See this class's _characters doc comment (TextRenderer.h) for
	// why the key is a Unicode codepoint, not a raw byte.
	std::vector<char32_t> codepoints;
	codepoints.reserve(128 + 96 + 1);
	for (char32_t c = 0; c < 128; ++c)
		codepoints.push_back(c);
	for (char32_t c = 0xA0; c <= 0xFF; ++c)
		codepoints.push_back(c);
	codepoints.push_back(0x2300);  // '⌀' DIAMETER SIGN

	for (char32_t c : codepoints)
	{
		// FT_Load_Char does NOT fail just because a codepoint isn't in
		// the font's cmap - FT_Get_Char_Index() silently returns glyph
		// index 0 (".notdef", a visible empty box in most fonts) for an
		// unmapped codepoint, and FT_Load_Char happily loads THAT and
		// reports success. Checking the index first is the only reliable
		// way to tell "genuinely not in this font" apart from a real
		// glyph, so an unsupported codepoint is skipped (renders as
		// nothing, same as any other missing glyph) rather than showing
		// a confusing box. Most of this range is GENUINELY optional/
		// best-effort coverage (unlike the original ASCII-only loop,
		// where a failure was always a real problem).
		if (FT_Get_Char_Index(face, static_cast<FT_ULong>(c)) == 0)
			continue;
		if (FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER))
			continue;
		// Generate texture
		unsigned int texture;
		glGenTextures(1, &texture);
		//std::cout << "TextRenderer::Load : _texture = " << texture << std::endl;
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
			QVector2D(face->glyph->bitmap.width, face->glyph->bitmap.rows),
			QVector2D(face->glyph->bitmap_left, face->glyph->bitmap_top),
			static_cast<unsigned int>(face->glyph->advance.x)
		};
		_characters.insert(std::pair<char32_t, Character>(c, character));
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	// Destroy FreeType once we're finished
	FT_Done_Face(face);
	FT_Done_FreeType(ft);
}

void TextRenderer::RenderText(std::string text, float x, float y, float scale, QVector3D color,
	VAlignment vAlignment, HAlignment hAlignment)
{
	// Activate corresponding updateMatrix state
	_prog->bind();
	_prog->setUniformValue("textColor", color);
	glActiveTexture(GL_TEXTURE30);
	//glBindVertexArray(this->VAO);
	_charVAO.bind();

	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	unsigned int voffset, hoffset;
	if (vAlignment == VAlignment::VTOP)
		voffset = 0;
	else if (vAlignment == VAlignment::VBOTTOM)
		voffset = _fontSize * scale;
	else
		voffset = _fontSize / 2 * scale;

	// Decode the incoming UTF-8 bytes (QString::toStdString() always
	// produces UTF-8) into actual Unicode codepoints - _characters is
	// keyed by codepoint (see its doc comment in TextRenderer.h), and the
	// alignment estimates below need the GLYPH count, not the raw BYTE
	// count (a multi-byte glyph like '°'/'⌀' would otherwise overcount).
	const std::vector<char32_t> codepoints = decodeUtf8(text);

	if (hAlignment == HAlignment::HLEFT)
		hoffset = 0;
	else if (hAlignment == HAlignment::HRIGHT)
		hoffset = static_cast<unsigned int>(_width - (codepoints.size() * this->_characters['H'].Size.x()));
	else
		hoffset = static_cast<unsigned int>(_width / 2 - (codepoints.size() * this->_characters['H'].Size.x()) / 2);

	// Iterate through all characters
	for (char32_t cp : codepoints)
	{
		const auto it = _characters.find(cp);
		if (it == _characters.end())
			continue;  // font has no glyph for this codepoint - skip silently
		const Character& ch = it->second;

		float xpos = x + hoffset + ch.Bearing.x() * scale;
		float ypos = y - voffset + (this->_characters['H'].Bearing.y() - ch.Bearing.y()) * scale;

		float w = ch.Size.x() * scale;
		float h = ch.Size.y() * scale;
		// Update VBO for each character
		float vertices[6][4] = {
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
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

float TextRenderer::textWidth(const std::string& text, float scale) const
{
	float width = 0.0f;
	for (char32_t cp : decodeUtf8(text))
	{
		const auto it = _characters.find(cp);
		if (it == _characters.end())
			continue;
		width += static_cast<float>(it->second.Advance >> 6) * scale;
	}
	return width;
}

void TextRenderer::textVerticalExtentVBottom(const std::string& text, float scale,
	float& outAscentAboveY, float& outDescentBelowY) const
{
	outAscentAboveY = 0.0f;
	outDescentBelowY = 0.0f;

	const auto hIt = _characters.find(static_cast<char32_t>('H'));
	if (hIt == _characters.end())
		return;
	const float hBearingY = hIt->second.Bearing.y();
	// Same voffset RenderText() itself uses for VAlignment::VBOTTOM.
	const float voffset = static_cast<float>(_fontSize) * scale;

	for (char32_t cp : decodeUtf8(text))
	{
		const auto it = _characters.find(cp);
		if (it == _characters.end())
			continue;
		const Character& ch = it->second;

		// Identical to RenderText()'s own ypos computation with x/y both
		// zero (i.e. relative to the anchor the caller will pass) - top of
		// the glyph's quad is at `top`, bottom at `top + h`, in this
		// class's "smaller is higher on screen" convention.
		const float top = -voffset + (hBearingY - ch.Bearing.y()) * scale;
		const float h = ch.Size.y() * scale;
		const float bottom = top + h;

		outAscentAboveY = std::max(outAscentAboveY, -top);
		outDescentBelowY = std::max(outDescentBelowY, bottom);
	}
	outDescentBelowY = std::max(outDescentBelowY, 0.0f);
}

unsigned int TextRenderer::width() const
{
	return _width;
}

void TextRenderer::setWidth(const unsigned int& width)
{
	_width = width;
}

unsigned int TextRenderer::height() const
{
	return _height;
}

void TextRenderer::setHeight(const unsigned int& height)
{
	_height = height;
}

unsigned int TextRenderer::fontSize() const
{
	return _fontSize;
}