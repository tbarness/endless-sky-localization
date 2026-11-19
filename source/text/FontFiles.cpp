/* FontFiles.cpp
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

#include "FontFiles.h"

#include "../Logger.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_set>

using namespace std;

namespace {
	// One library serves every font. It is never shut down: that would free faces that are
	// still owned by fonts that are destroyed after it, when the game exits.
	FT_Library Library()
	{
		static FT_Library library = []
		{
			FT_Library result = nullptr;
			return FT_Init_FreeType(&result) ? nullptr : result;
		}();
		return library;
	}

	// Every size of every font has its own copy of the list, but a file that can't be used can't be used
	// by any of them, so it is only reported once.
	void Warn(const string &message)
	{
		static unordered_set<string> reported;
		if(reported.insert(message).second)
			Logger::Log(message, Logger::Level::WARNING);
	}

	// Read from a file stream. This, instead of letting FreeType open the file by name, is
	// what allows paths that contain characters that are not in the system's code page.
	unsigned long ReadFile(FT_Stream stream, unsigned long offset, unsigned char *buffer, unsigned long count)
	{
		auto *file = static_cast<ifstream *>(stream->descriptor.pointer);
		file->clear();
		file->seekg(static_cast<streamoff>(offset));
		// A count of 0 asks only to move to the offset. The result is 0 if that worked.
		if(!count)
			return file->good() ? 0 : 1;
		file->read(reinterpret_cast<char *>(buffer), static_cast<streamsize>(count));
		return static_cast<unsigned long>(file->gcount());
	}
}



class FontFiles::Face {
public:
	explicit Face(filesystem::path path, int index) : path(std::move(path)), index(index) {}
	~Face()
	{
		if(face)
			FT_Done_Face(face);
	}

	bool Open(int cellWidth, int xHeight);


public:
	filesystem::path path;
	int index = 0;
	bool failed = false;
	FT_Face face = nullptr;


private:
	// FreeType reads through these, so they must stay where they are while the face is open.
	ifstream file;
	FT_StreamRec stream{};
};



bool FontFiles::Face::Open(int cellWidth, int xHeight)
{
	failed = true;
	FT_Library library = Library();
	if(!library)
		return false;

	file.open(path, ios::binary);
	if(!file)
	{
		Warn("Unable to open the font file \"" + path.string() + "\".");
		return false;
	}
	file.seekg(0, ios::end);
	stream.size = static_cast<unsigned long>(file.tellg());
	file.seekg(0);
	stream.descriptor.pointer = &file;
	stream.read = ReadFile;

	FT_Open_Args args{};
	args.flags = FT_OPEN_STREAM;
	args.stream = &stream;
	if(FT_Open_Face(library, &args, index, &face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE))
	{
		Warn("\"" + path.string() + "\" is not a font that can be used.");
		if(face)
			FT_Done_Face(face);
		face = nullptr;
		return false;
	}

	// Choose the size that makes the x-height match the one in the font image, so that the
	// text looks the same size. A font that has no "x" is about 7/8 of the cell.
	int pixels = max(8, cellWidth * 7 / 8);
	if(!FT_Set_Pixel_Sizes(face, 0, 100) && FT_Get_Char_Index(face, 'x')
			&& !FT_Load_Char(face, 'x', FT_LOAD_RENDER | FT_LOAD_NO_BITMAP) && face->glyph->bitmap.rows > 0)
		pixels = clamp<int>((100 * xHeight + face->glyph->bitmap.rows / 2) / face->glyph->bitmap.rows, 8, cellWidth);
	if(FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixels)))
	{
		Warn("\"" + path.string() + "\" is a font that has no usable size.");
		FT_Done_Face(face);
		face = nullptr;
		return false;
	}

	failed = false;
	return true;
}



FontFiles::FontFiles() noexcept = default;



FontFiles::~FontFiles() = default;



void FontFiles::Set(vector<filesystem::path> files, int cellWidth, int cellHeight, int baseline, int xHeight)
{
	this->cellWidth = cellWidth;
	this->cellHeight = cellHeight;
	this->baseline = baseline;
	this->xHeight = xHeight;

	faces.clear();
	for(filesystem::path &path : files)
	{
		// "Name#2.ttc" is the font numbered 2 in a collection.
		int index = 0;
		const string stem = path.stem().string();
		const size_t hash = stem.rfind('#');
		if(hash != string::npos && hash + 1 < stem.size()
				&& all_of(stem.begin() + hash + 1, stem.end(), [](char c) { return c >= '0' && c <= '9'; }))
			index = stoi(stem.substr(hash + 1));
		faces.push_back(make_unique<Face>(std::move(path), min(index, 9999)));
	}
}



bool FontFiles::Empty() const noexcept
{
	return faces.empty();
}



bool FontFiles::Render(char32_t c, Glyph &glyph)
{
	for(const unique_ptr<Face> &face : faces)
	{
		if(face->failed || (!face->face && !face->Open(cellWidth, xHeight)))
			continue;
		const FT_UInt index = FT_Get_Char_Index(face->face, c);
		// Bitmaps embedded in a font are skipped: they come in fixed sizes, which would not match.
		if(!index || FT_Load_Glyph(face->face, index, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP))
			continue;

		const FT_GlyphSlot slot = face->face->glyph;
		const FT_Bitmap &bitmap = slot->bitmap;
		if(bitmap.width && bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
			continue;

		glyph.pixels.assign(static_cast<size_t>(cellWidth) * cellHeight, 0);
		// The glyph is placed relative to the pen, which is at the left edge of the cell, on the baseline.
		const int left = slot->bitmap_left;
		const int top = baseline - slot->bitmap_top;
		for(int row = 0; row < static_cast<int>(bitmap.rows); ++row)
		{
			const int y = top + row;
			if(y < 0 || y >= cellHeight)
				continue;
			const unsigned char *source = bitmap.buffer + (bitmap.pitch >= 0 ? row * bitmap.pitch
				: (static_cast<int>(bitmap.rows) - 1 - row) * -bitmap.pitch);
			for(int column = 0; column < static_cast<int>(bitmap.width); ++column)
			{
				const int x = left + column;
				if(x < 0 || x >= cellWidth)
					continue;
				// Like the font image, the colors are multiplied by the alpha.
				const uint32_t a = source[column];
				glyph.pixels[static_cast<size_t>(y) * cellWidth + x] = (a << 24) | (a << 16) | (a << 8) | a;
			}
		}
		// The advance is in 26.6 fixed point, in pixels of the doubled image.
		glyph.advance = static_cast<int>((slot->advance.x + 64) / 128);
		glyph.inkLeft = clamp(left, 0, cellWidth);
		glyph.inkWidth = clamp(left + static_cast<int>(bitmap.width), glyph.inkLeft, cellWidth);
		return true;
	}
	return false;
}
