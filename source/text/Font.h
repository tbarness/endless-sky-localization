/* Font.h
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

#pragma once

#include "FontFiles.h"
#include "../shader/Shader.h"

#include "../opengl.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Color;
class DisplayText;
class ImageBuffer;
class Point;



// Class for drawing text in OpenGL. Each font is based on a single image with
// glyphs for each character in ASCII order (not counting control characters).
// The kerning between characters is automatically adjusted to look good.
//
// Text is UTF-8. Other characters (accents, CJK, ...), or all characters, can be
// drawn from font files (TrueType, OpenType, ...) that the player adds. See UseFiles().
class Font {
public:
	Font() noexcept = default;
	explicit Font(const std::filesystem::path &imagePath);

	void Load(const std::filesystem::path &imagePath);

	// Draw glyphs from these font files, which are tried in order. A preferred font is used for every
	// character that it has, in place of the font image. A fallback font is only used for characters
	// that the font image does not have. A glyph is drawn when it is first needed. This must be called
	// after Load(), and while there is an OpenGL context if it replaces fonts that were in use.
	void UseFiles(std::vector<std::filesystem::path> preferred, std::vector<std::filesystem::path> fallback);

	// Draw a text string, subject to the given layout and truncation strategy.
	void Draw(const DisplayText &text, const Point &point, const Color &color) const;
	void DrawAliased(const DisplayText &text, double x, double y, const Color &color) const;
	// Draw the given text string, e.g. post-formatting (or without regard to formatting).
	void Draw(const std::string &str, const Point &point, const Color &color) const;
	void DrawAliased(const std::string &str, double x, double y, const Color &color) const;

	// Determine the string's width, without considering formatting.
	int Width(const std::string &str, char after = ' ') const;
	// Get the width of the text while accounting for the desired layout and truncation strategy.
	int FormattedWidth(const DisplayText &text, char after = ' ') const;

	int Height() const noexcept;

	int Space() const noexcept;

	static void ShowUnderlines(bool show) noexcept;


private:
	int Glyph(char32_t c, bool isAfterSpace) const noexcept;
	int Advance(int previous, int next) const noexcept;
	int FindGlyph(char32_t c, bool fallback) const noexcept;
	void FlushGlyphs() const;
	void ReportMissing(char32_t c) const;
	static GLuint CreateTexture(int width, int height, const void *pixels);
	void CalculateAdvances(ImageBuffer &image);
	void SetUpShader(float glyphW, float glyphH);

	int WidthRawString(const std::string &str, char after = ' ') const noexcept;

	std::string TruncateText(const DisplayText &text, int &width) const;
	std::string TruncateBack(const std::string &str, int &width) const;
	std::string TruncateFront(const std::string &str, int &width) const;
	std::string TruncateMiddle(const std::string &str, int &width) const;

	std::string TruncateEndsOrMiddle(const std::string &str, int &width,
		std::function<std::string(const std::string &, int)> getResultString) const;

private:
	const Shader *shader;
	GLuint texture = 0;

	int height = 0;
	int space = 0;
	mutable int screenWidth = 0;
	mutable int screenHeight = 0;
	mutable GLfloat scale[2]{0.f, 0.f};
	GLfloat glyphWidth = 0.f;
	GLfloat glyphHeight = 0.f;

	static const int GLYPHS = 98;
	int advance[GLYPHS * GLYPHS] = {};
	int widthEllipses = 0;

	// Glyphs that are drawn from font files are numbered from GLYPHS. Each is a cell of an image
	// that is made when the glyph is first needed: a "page" holds a grid of cells.
	mutable FontFiles preferredFiles;
	mutable FontFiles fallbackFiles;
	// The glyph number of each character that was asked for, or 0 if there is none.
	mutable std::unordered_map<char32_t, int> preferredGlyphs;
	mutable std::unordered_map<char32_t, int> fallbackGlyphs;
	mutable int cells = 0;
	// Glyphs that are drawn but not yet in a page: the cell, and its pixels.
	mutable std::vector<std::pair<int, std::vector<uint32_t>>> pending;
	mutable std::vector<GLuint> pages;
	// For every glyph, including those of the font image: the width of the ink, the space before it,
	// and (for the glyphs from files) how far to move after it. The first two are in pixels of the
	// doubled image, the last is in pixels of the font.
	mutable std::vector<int> inkWidth;
	mutable std::vector<int> inkLeft;
	mutable std::vector<int> fileAdvance;
	// The size of a cell, and where the baseline and the x-height of the font image are. These
	// are in pixels of the doubled image.
	int cellWidth = 0;
	int cellHeight = 0;
	int baseline = 0;
	int xHeight = 0;
	// The characters that have no glyph, which have been reported.
	mutable std::unordered_set<char32_t> missing;
};
