/* FontFiles.h
Copyright (c) 2026 by contributors

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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>



// A list of font files (TrueType, OpenType, collections, ...), from which glyphs are
// drawn when they are first needed. FreeType does the drawing. A file is not opened
// until a glyph is wanted, so an unused list costs nothing.
class FontFiles {
public:
	// A glyph, drawn into a cell of the size that was given to Set().
	class Glyph {
	public:
		// The pixels of the cell: white, with the glyph's coverage as the alpha.
		std::vector<uint32_t> pixels;
		// How far the pen moves after this glyph, in pixels of the font image's size (not doubled).
		int advance = 0;
		// Where the ink ends and begins in the cell, in pixels of the doubled font image.
		int inkWidth = 0;
		int inkLeft = 0;
	};


public:
	FontFiles() noexcept;
	~FontFiles();
	FontFiles(const FontFiles &) = delete;
	FontFiles &operator=(const FontFiles &) = delete;

	// Replace the list of fonts. A collection (.ttc) uses its first font, unless the file's name
	// ends with "#2" before the extension, which asks for the font with that number (counting from 0).
	// The glyphs are drawn at a size whose x-height is "xHeight", into cells of the given size, with
	// the baseline "baseline" rows down. All of these are in pixels of the doubled font image.
	void Set(std::vector<std::filesystem::path> files, int cellWidth, int cellHeight, int baseline, int xHeight);
	bool Empty() const noexcept;

	// Draw the character with the first font in the list that has it. Returns false if none does.
	bool Render(char32_t c, Glyph &glyph);


private:
	class Face;


private:
	std::vector<std::unique_ptr<Face>> faces;
	int cellWidth = 0;
	int cellHeight = 0;
	int baseline = 0;
	int xHeight = 0;
};
