/* Font.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Font.h"

#include "Alignment.h"
#include "../Color.h"
#include "DisplayText.h"
#include "../GameData.h"
#include "../image/ImageBuffer.h"
#include "../image/ImageFileData.h"
#include "../Logger.h"
#include "../Point.h"
#include "../Preferences.h"
#include "../Screen.h"
#include "Truncate.h"
#include "Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace {
	bool showUnderlines = false;
	const int KERN = 2;
	// The cells that hold glyphs from font files are this many rows (of the doubled image, so keep it
	// even) taller than the font, with the extra above, to leave room for accents and tall glyphs.
	const int EXT_TOP = 8;
	// Cells are gathered into pages, each of which is an image. This many fit in one.
	const int PAGE_COLUMNS = 32;
	const int PAGE_ROWS = 16;
	const int PAGE_CELLS = PAGE_COLUMNS * PAGE_ROWS;
	const int MAX_PAGES = 32;
	// A value for the page in use, for when it must be chosen again.
	const int UNKNOWN_PAGE = -2;

	/// Shared VAO and VBO quad (0,0) -> (1,1)
	GLuint vao = 0;
	GLuint vbo = 0;

	GLint colorI = 0;
	GLint scaleI = 0;
	GLint glyphSizeI = 0;
	GLint glyphI = 0;
	GLint gridI = 0;
	GLint aspectI = 0;
	GLint positionI = 0;

	GLint vertI;
	GLint cornerI;

	// Find how wide the ink of a glyph cell is, and how far from the left of the cell it begins.
	// A pixel counts as ink if it is at least 3/4 opaque, as when the kerning is calculated.
	void Measure(const ImageBuffer &image, int x, int y, int w, int h, int &width, int &left)
	{
		width = 0;
		left = w;
		for(int row = y; row < y + h; ++row)
		{
			const uint32_t *pixels = image.Pixels() + row * image.Width() + x;
			for(int i = 0; i < w; ++i)
				if((pixels[i] & 0xFF000000) >= 0xC0000000)
				{
					left = min(left, i);
					width = max(width, i + 1);
				}
		}
	}

	void EnableAttribArrays()
	{
		// Connect the xy to the "vert" attribute of the vertex shader.
		constexpr auto stride = 4 * sizeof(GLfloat);
		glEnableVertexAttribArray(vertI);
		glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);

		glEnableVertexAttribArray(cornerI);
		glVertexAttribPointer(cornerI, 2, GL_FLOAT, GL_FALSE,
			stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
	}
}



Font::Font(const filesystem::path &imagePath)
{
	Load(imagePath);
}



void Font::Load(const filesystem::path &imagePath)
{
	// Load the texture.
	ImageBuffer image;
	if(!image.Read(ImageFileData(imagePath)))
		return;

	texture = CreateTexture(image.Width(), image.Height(), image.Pixels());
	CalculateAdvances(image);
	SetUpShader(image.Width() / GLYPHS, image.Height());

	// Measure the ink of each glyph, which is how glyphs from font files are spaced after them.
	const int glyphPixels = image.Width() / GLYPHS;
	inkWidth.assign(GLYPHS, 0);
	inkLeft.assign(GLYPHS, 0);
	fileAdvance.assign(GLYPHS, 0);
	for(int i = 0; i < GLYPHS; ++i)
		Measure(image, i * glyphPixels, 0, glyphPixels, image.Height(), inkWidth[i], inkLeft[i]);

	// Glyphs from font files are drawn into cells that are as wide as the font is tall, with room above
	// for accents. They are lined up with the font image by the baseline and the x-height of its "x".
	cellWidth = image.Height();
	cellHeight = image.Height() + EXT_TOP;
	int top = image.Height();
	int bottom = -1;
	for(int row = 0; row < image.Height(); ++row)
		for(int i = 0; i < glyphPixels; ++i)
			if((image.Pixels()[row * image.Width() + ('x' - 32) * glyphPixels + i] & 0xFF000000) >= 0xC0000000)
			{
				top = min(top, row);
				bottom = max(bottom, row);
			}
	baseline = bottom >= 0 ? bottom + 1 : image.Height() * 4 / 5;
	xHeight = bottom >= 0 ? bottom - top + 1 : image.Height() / 2;

	widthEllipses = WidthRawString("...");
}



void Font::Draw(const DisplayText &text, const Point &point, const Color &color) const
{
	DrawAliased(text, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const DisplayText &text, double x, double y, const Color &color) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	const auto &layout = text.GetLayout();
	if(width >= 0)
	{
		if(layout.align == Alignment::CENTER)
			x += (layout.width - width) / 2;
		else if(layout.align == Alignment::RIGHT)
			x += layout.width - width;
	}
	DrawAliased(truncText, x, y, color);
}



void Font::Draw(const string &str, const Point &point, const Color &color) const
{
	DrawAliased(str, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const string &str, double x, double y, const Color &color) const
{
	glUseProgram(shader->Object());
	glBindTexture(GL_TEXTURE_2D, texture);
	if(OpenGL::HasVaoSupport())
		glBindVertexArray(vao);
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		EnableAttribArrays();
	}

	glUniform4fv(colorI, 1, color.Get());

	// Update the scale, only if the screen size has changed.
	if(Screen::Width() != screenWidth || Screen::Height() != screenHeight)
	{
		screenWidth = Screen::Width();
		screenHeight = Screen::Height();
		scale[0] = 2.f / screenWidth;
		scale[1] = -2.f / screenHeight;
	}
	glUniform2fv(scaleI, 1, scale);
	glUniform2f(glyphSizeI, glyphWidth, glyphHeight);
	glUniform2f(gridI, GLYPHS, 1.f);

	GLfloat textPos[2] = {
		static_cast<float>(x - 1.),
		static_cast<float>(y)};
	int previous = 0;
	bool isAfterSpace = true;
	bool underlineChar = false;
	const int underscoreGlyph = max(0, min(GLYPHS - 1, '_' - 32));

	// Switch between the font image (page -1) and the pages of glyphs that come from font files.
	int page = -1;
	const auto Use = [&](int wanted)
	{
		if(wanted == page)
			return;
		page = wanted;
		if(wanted < 0)
		{
			glBindTexture(GL_TEXTURE_2D, texture);
			glUniform2f(glyphSizeI, glyphWidth, glyphHeight);
			glUniform2f(gridI, GLYPHS, 1.f);
		}
		else
		{
			glBindTexture(GL_TEXTURE_2D, pages[wanted]);
			glUniform2f(glyphSizeI, cellWidth * .5f, cellHeight * .5f);
			glUniform2f(gridI, PAGE_COLUMNS, PAGE_ROWS);
		}
	};

	for(size_t pos = 0; pos < str.size(); )
	{
		const char32_t c = Utf8::DecodeCodePoint(str, pos);
		if(c == '_')
		{
			underlineChar = showUnderlines;
			continue;
		}

		const int glyph = Glyph(c, isAfterSpace);
		// A glyph that was only just drawn must be put in its page before it can be used.
		if(!pending.empty())
		{
			FlushGlyphs();
			page = UNKNOWN_PAGE;
		}
		if(c != '"' && c != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
		{
			if(c >= 0x80 && c != 0xA0)
				ReportMissing(c);
			textPos[0] += space;
			continue;
		}

		// Glyphs from font files are cells of a page, which is chosen first.
		const bool fromFile = glyph >= GLYPHS;
		Use(fromFile ? (glyph - GLYPHS) / PAGE_CELLS : -1);
		glUniform1i(glyphI, fromFile ? (glyph - GLYPHS) % PAGE_CELLS : glyph);
		glUniform1f(aspectI, 1.f);

		textPos[0] += Advance(previous, glyph) + KERN;
		// Those cells have extra room above them, so they are drawn that much higher.
		glUniform2f(positionI, textPos[0], textPos[1] - (fromFile ? EXT_TOP / 2 : 0));

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if(underlineChar)
		{
			Use(-1);
			glUniform1i(glyphI, underscoreGlyph);
			glUniform1f(aspectI, static_cast<float>(Advance(glyph, 0) + KERN)
				/ (advance[underscoreGlyph * GLYPHS] + KERN));

			glUniform2f(positionI, textPos[0], textPos[1]);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			underlineChar = false;
		}

		previous = glyph;
	}

	if(OpenGL::HasVaoSupport())
		glBindVertexArray(0);
	else
	{
		glDisableVertexAttribArray(vertI);
		glDisableVertexAttribArray(cornerI);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
	}
	glUseProgram(0);
}



int Font::Width(const string &str, char after) const
{
	return WidthRawString(str, after);
}



int Font::FormattedWidth(const DisplayText &text, char after) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	return width < 0 ? WidthRawString(truncText, after) : width;
}



int Font::Height() const noexcept
{
	return height;
}



int Font::Space() const noexcept
{
	return space;
}



void Font::ShowUnderlines(bool show) noexcept
{
	showUnderlines = show || Preferences::Has("Always underline shortcuts");
}



// Get the glyph number for a character. Glyphs from font files are numbered from GLYPHS.
int Font::Glyph(char32_t c, bool isAfterSpace) const noexcept
{
	// Preferred fonts are asked first, for everything except spaces and control characters.
	if(!preferredFiles.Empty() && c > ' ' && c != 0x7F && c != 0xA0)
	{
		// A quote after a space opens a quotation, so it has its curly form, if the font has one.
		const char32_t opening = (c == '\'') ? 0x2018 : (c == '"') ? 0x201C : 0;
		int glyph = (opening && isAfterSpace) ? FindGlyph(opening, false) : 0;
		if(!glyph)
			glyph = FindGlyph(c, false);
		if(glyph)
			return glyph;
	}

	if(c >= 0x80)
	{
		// Curly quotes and the no-break space use glyphs that the font image already has.
		switch(c)
		{
			case 0x00A0: return 0;
			case 0x2018: return 96;
			case 0x2019: return '\'' - 32;
			case 0x201C: return 97;
			case 0x201D: return '"' - 32;
		}
		// Anything else can only come from a fallback font. If there is none, it is a space.
		return FindGlyph(c, true);
	}

	// Curly quotes.
	if(c == '\'' && isAfterSpace)
		return 96;
	if(c == '"' && isAfterSpace)
		return 97;

	return max(0, min(GLYPHS - 3, static_cast<int>(c) - 32));
}



// Get the glyph for a character from the preferred or the fallback fonts, drawing it if this is the
// first time it was wanted. This is 0 if no font has it.
int Font::FindGlyph(char32_t c, bool fallback) const noexcept
{
	unordered_map<char32_t, int> &known = fallback ? fallbackGlyphs : preferredGlyphs;
	const auto it = known.find(c);
	if(it != known.end())
		return it->second;

	FontFiles &files = fallback ? fallbackFiles : preferredFiles;
	int glyph = 0;
	FontFiles::Glyph drawn;
	if(!files.Empty() && cells < MAX_PAGES * PAGE_CELLS && files.Render(c, drawn))
	{
		glyph = GLYPHS + cells++;
		inkWidth.push_back(drawn.inkWidth);
		inkLeft.push_back(drawn.inkLeft);
		fileAdvance.push_back(drawn.advance);
		pending.emplace_back(glyph - GLYPHS, std::move(drawn.pixels));
	}
	known.emplace(c, glyph);
	return glyph;
}



// Put the glyphs that were drawn into their pages. Pages are made when the first of their glyphs is.
void Font::FlushGlyphs() const
{
	for(const auto &cell : pending)
	{
		const size_t page = cell.first / PAGE_CELLS;
		if(page >= pages.size())
			pages.resize(page + 1, 0);
		if(!pages[page])
		{
			const vector<uint32_t> blank(static_cast<size_t>(PAGE_COLUMNS) * cellWidth * PAGE_ROWS * cellHeight, 0);
			pages[page] = CreateTexture(PAGE_COLUMNS * cellWidth, PAGE_ROWS * cellHeight, blank.data());
		}
		const int index = cell.first % PAGE_CELLS;
		glBindTexture(GL_TEXTURE_2D, pages[page]);
		glTexSubImage2D(GL_TEXTURE_2D, 0, (index % PAGE_COLUMNS) * cellWidth, (index / PAGE_COLUMNS) * cellHeight,
			cellWidth, cellHeight, GL_RGBA, GL_UNSIGNED_BYTE, cell.second.data());
	}
	pending.clear();
}



// Say in the log (once for each character) that no font can draw a character. It is drawn as a space,
// so without this it is hard to tell that a font is missing.
void Font::ReportMissing(char32_t c) const
{
	// A language that has no font at all would otherwise fill the log.
	static const size_t MAX_REPORTED = 20;
	if(missing.size() >= MAX_REPORTED || !missing.insert(c).second)
		return;

	char code[16];
	snprintf(code, sizeof(code), "U+%04X", static_cast<unsigned>(c));
	Logger::Log(string("No font has a glyph for ") + code + ". Put a font file that has it in the \"fonts\" folder.",
		Logger::Level::WARNING);
	if(missing.size() == MAX_REPORTED)
		Logger::Log("More characters than these have no font. They are not listed.", Logger::Level::WARNING);
}



// How far to move after drawing the glyph "previous" before drawing the glyph "next".
int Font::Advance(int previous, int next) const noexcept
{
	// The font image has a table of pairs, which is tuned to look good.
	if(previous < GLYPHS && next < GLYPHS)
		return advance[previous * GLYPHS + next];
	if(!previous)
		return 0;
	// A glyph from a font file moves by its own advance, less the KERN that follows every glyph.
	if(previous >= GLYPHS)
		return fileAdvance[previous] - KERN;
	// A glyph of the font image, then one from a file. There is no table for this, but this is within
	// a pixel of what the table says: the width of the first, less the space at the left of the next.
	return (inkWidth[previous] - (next ? min(inkLeft[next], 4) : 0)) / 2;
}



GLuint Font::CreateTexture(int width, int height, const void *pixels)
{
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	return id;
}



void Font::UseFiles(vector<filesystem::path> preferred, vector<filesystem::path> fallback)
{
	// The font image says how big the glyphs should be.
	if(!texture)
		return;

	// Forget the glyphs that came from the previous fonts, and the pages that hold them.
	if(!pages.empty())
		glDeleteTextures(static_cast<GLsizei>(pages.size()), pages.data());
	pages.clear();
	pending.clear();
	preferredGlyphs.clear();
	fallbackGlyphs.clear();
	missing.clear();
	cells = 0;
	inkWidth.resize(GLYPHS);
	inkLeft.resize(GLYPHS);
	fileAdvance.resize(GLYPHS);

	preferredFiles.Set(std::move(preferred), cellWidth, cellHeight, baseline + EXT_TOP, xHeight);
	fallbackFiles.Set(std::move(fallback), cellWidth, cellHeight, baseline + EXT_TOP, xHeight);
}



void Font::CalculateAdvances(ImageBuffer &image)
{
	// Get the format and size of the surface.
	int width = image.Width() / GLYPHS;
	height = image.Height();
	unsigned mask = 0xFF000000;
	unsigned half = 0xC0000000;
	int pitch = image.Width();

	// advance[previous * GLYPHS + next] is the x advance for each glyph pair.
	// There is no advance if the previous value is 0, i.e. we are at the very
	// start of a string.
	memset(advance, 0, GLYPHS * sizeof(advance[0]));
	for(int previous = 1; previous < GLYPHS; ++previous)
		for(int next = 0; next < GLYPHS; ++next)
		{
			int maxD = 0;
			int glyphWidth = 0;
			uint32_t *begin = image.Pixels();
			for(int y = 0; y < height; ++y)
			{
				// Find the last non-empty pixel in the previous glyph.
				uint32_t *pend = begin + previous * width;
				uint32_t *pit = pend + width;
				while(pit != pend && (*--pit & mask) < half) {}
				int distance = (pit - pend) + 1;
				glyphWidth = max(distance, glyphWidth);

				// Special case: if "next" is zero (i.e. end of line of text),
				// calculate the full width of this character. Otherwise:
				if(next)
				{
					// Find the first non-empty pixel in this glyph.
					uint32_t *nit = begin + next * width;
					uint32_t *nend = nit + width;
					while(nit != nend && (*nit++ & mask) < half) {}

					// How far apart do you want these glyphs drawn? If drawn at
					// an advance of "width", there would be:
					// pend + width - pit   <- pixels after the previous glyph.
					// nit - (nend - width) <- pixels before the next glyph.
					// So for zero kerning distance, you would want:
					distance += 1 - (nit - (nend - width));
				}
				maxD = max(maxD, distance);

				// Update the pointer to point to the beginning of the next row.
				begin += pitch;
			}
			// This is a fudge factor to avoid over-kerning, especially for the
			// underscore and for glyph combinations like AV.
			advance[previous * GLYPHS + next] = max(maxD, glyphWidth - 4) / 2;
		}

	// Set the space size based on the character width.
	width /= 2;
	height /= 2;
	space = (width + 3) / 6 + 1;
}



void Font::SetUpShader(float glyphW, float glyphH)
{
	glyphWidth = glyphW * .5f;
	glyphHeight = glyphH * .5f;

	shader = GameData::Shaders().Get("font");
	// Initialize the shared parameters only once
	if(!vbo)
	{
		vertI = shader->Attrib("vert");
		cornerI = shader->Attrib("corner");

		glUseProgram(shader->Object());
		glUniform1i(shader->Uniform("tex"), 0);
		glUseProgram(0);

		// Create the VAO and VBO.
		if(OpenGL::HasVaoSupport())
		{
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);
		}

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);

		GLfloat vertices[] = {
				0.f, 0.f, 0.f, 0.f,
				0.f, 1.f, 0.f, 1.f,
				1.f, 0.f, 1.f, 0.f,
				1.f, 1.f, 1.f, 1.f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		if(OpenGL::HasVaoSupport())
			EnableAttribArrays();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(0);

		colorI = shader->Uniform("color");
		scaleI = shader->Uniform("scale");
		glyphSizeI = shader->Uniform("glyphSize");
		gridI = shader->Uniform("grid");
		glyphI = shader->Uniform("glyph");
		aspectI = shader->Uniform("aspect");
		positionI = shader->Uniform("position");
	}

	// We must update the screen size next time we draw.
	screenWidth = 0;
	screenHeight = 0;
}



int Font::WidthRawString(const string &str, char after) const noexcept
{
	int width = 0;
	int previous = 0;
	bool isAfterSpace = true;

	for(size_t pos = 0; pos < str.size(); )
	{
		const char32_t c = Utf8::DecodeCodePoint(str, pos);
		// Like a C string, the text ends at a null character.
		if(!c)
			break;
		if(c == '_')
			continue;

		const int glyph = Glyph(c, isAfterSpace);
		if(c != '"' && c != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
			width += space;
		else
		{
			width += Advance(previous, glyph) + KERN;
			previous = glyph;
		}
	}
	width += Advance(previous, max(0, min(GLYPHS - 1, after - 32)));

	return width;
}



// Param width will be set to the width of the return value, unless the layout width is negative.
string Font::TruncateText(const DisplayText &text, int &width) const
{
	width = -1;
	const auto &layout = text.GetLayout();
	const string &str = text.GetText();
	if(layout.width < 0 || (layout.align == Alignment::LEFT && layout.truncate == Truncate::NONE))
		return str;
	width = layout.width;
	switch(layout.truncate)
	{
		case Truncate::NONE:
			width = WidthRawString(str);
			return str;
		case Truncate::FRONT:
			return TruncateFront(str, width);
		case Truncate::MIDDLE:
			return TruncateMiddle(str, width);
		case Truncate::BACK:
		default:
			return TruncateBack(str, width);
	}
}



string Font::TruncateBack(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return str.substr(0, Utf8::CodePointStart(str, charCount)) + "...";
		});
}



string Font::TruncateFront(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return "..." + str.substr(Utf8::CodePointStart(str, str.size() - charCount));
		});
}



string Font::TruncateMiddle(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return str.substr(0, Utf8::CodePointStart(str, (charCount + 1) / 2)) + "..."
				+ str.substr(Utf8::CodePointStart(str, str.size() - charCount / 2));
		});
}



string Font::TruncateEndsOrMiddle(const string &str, int &width,
	function<string(const string &, int)> getResultString) const
{
	int firstWidth = WidthRawString(str);
	if(firstWidth <= width)
	{
		width = firstWidth;
		return str;
	}

	int workingChars = 0;
	int workingWidth = 0;

	int low = 0, high = str.size() - 1;
	while(low <= high)
	{
		// Think "how many chars to take from both ends, omitting in the middle".
		int nextChars = (low + high) / 2;
		int nextWidth = WidthRawString(getResultString(str, nextChars));
		if(nextWidth <= width)
		{
			if(nextChars > workingChars)
			{
				workingChars = nextChars;
				workingWidth = nextWidth;
			}
			low = nextChars + (nextChars == low);
		}
		else
			high = nextChars - 1;
	}
	width = workingWidth;
	return getResultString(str, workingChars);
}
