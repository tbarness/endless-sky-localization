/* FontSet.cpp
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

#include "FontSet.h"

#include "Font.h"
#include "../Files.h"

#include <algorithm>
#include <map>
#include <vector>

using namespace std;

namespace {
	map<int, Font> fonts;
	// The font files that are in use, so that they can be given to fonts that are added later.
	vector<filesystem::path> preferred;
	vector<filesystem::path> fallback;

	// Add the font files in a folder to a list, in order of their names.
	void Scan(const filesystem::path &directory, vector<filesystem::path> &list)
	{
		vector<filesystem::path> files = Files::List(directory);
		sort(files.begin(), files.end());
		for(const filesystem::path &file : files)
		{
			string extension = file.extension().string();
			transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return tolower(c); });
			if(extension == ".ttf" || extension == ".otf" || extension == ".ttc" || extension == ".otc")
				list.push_back(file);
		}
	}
}



void FontSet::Add(const filesystem::path &path, int size)
{
	if(!fonts.contains(size))
	{
		fonts[size].Load(path);
		fonts[size].UseFiles(preferred, fallback);
	}
}



const Font &FontSet::Get(int size)
{
	return fonts[size];
}



void FontSet::UseLanguage(const string &language)
{
	preferred.clear();
	fallback.clear();

	const filesystem::path root = Files::Resources() / "fonts";
	if(!language.empty() && language != "en")
	{
		Scan(root / language, preferred);
		Scan(root / language / "fallback", fallback);
	}
	Scan(root, preferred);
	Scan(root / "fallback", fallback);

	for(auto &it : fonts)
		it.second.UseFiles(preferred, fallback);
}
