/* Translations.cpp */

#include "Translations.h"

#include "Files.h"
#include "Logger.h"
#include "MoCatalog.h"
#include "text/FontSet.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace std;

namespace {
	struct Language {
		string code;
		string name;
		filesystem::path file;
	};

	// The first language is always English, which needs no catalog.
	vector<Language> languages{{"en", "English", {}}};
	size_t current = 0;
	MoCatalog catalog;

	filesystem::path LanguageDirectory()
	{
		return Files::Resources() / "languages";
	}

	filesystem::path SettingsFile()
	{
		return Files::Config() / "language.txt";
	}

	// Load the text of a language, or of English if it can't be loaded.
	void Load(const string &code)
	{
		current = 0;
		catalog = MoCatalog();

		for(size_t i = 1; i < languages.size(); ++i)
			if(languages[i].code == code)
			{
				ifstream in(languages[i].file, ios::binary);
				string bytes((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
				if(catalog.Load(std::move(bytes)))
					current = i;
				else
					Logger::Log("Unable to read the language file \"" + languages[i].file.string() + "\".",
						Logger::Level::WARNING);
				return;
			}
	}

	// Switch languages without saving the choice. The fonts that were added for the language come with it.
	void Select(const string &code)
	{
		Load(code);
		FontSet::UseLanguage(languages[current].code);
	}
}



void Translations::Init()
{
	languages.resize(1);
	for(const filesystem::path &directory : Files::ListDirectories(LanguageDirectory()))
	{
		const filesystem::path file = directory / "LC_MESSAGES" / "endless-sky.mo";
		if(!filesystem::exists(file))
			continue;

		Language language;
		language.code = directory.filename().string();
		language.file = file;
		// Only the header is read here, so this is fast no matter how large the catalog is.
		language.name = MoCatalog::ReadHeader(file, "X-Language-Name");
		if(language.name.empty())
			language.name = language.code;
		languages.push_back(std::move(language));
	}
	sort(languages.begin() + 1, languages.end(),
		[](const Language &a, const Language &b) { return a.code < b.code; });

	string saved = Files::Read(SettingsFile());
	while(!saved.empty() && (saved.back() == '\n' || saved.back() == '\r' || saved.back() == ' '))
		saved.pop_back();
	Select(saved);
}



const string &Translations::Current()
{
	return languages[current].name;
}



size_t Translations::Count()
{
	return languages.size();
}



void Translations::Set(const string &code)
{
	Select(code);
	Files::Write(SettingsFile(), languages[current].code + "\n");
}



void Translations::Next()
{
	Set(languages[(current + 1) % languages.size()].code);
}



string Translations::Get(string_view msgid)
{
	if(!msgid.empty() && current != 0)
	{
		const string_view translated = catalog.Find(msgid);
		if(!translated.empty())
			return string(translated);
	}
	return string(msgid);
}



string Translations::GetContext(string_view context, string_view msgid)
{
	if(current != 0)
	{
		const string_view translated = catalog.Find(context, msgid);
		if(!translated.empty())
			return string(translated);
	}
	return string(msgid);
}



string Translations::GetPlural(string_view singular, string_view plural, long long n)
{
	if(current != 0)
	{
		const string_view translated = catalog.FindPlural(singular, plural, n);
		if(!translated.empty())
			return string(translated);
	}
	// English: the usual rule.
	return string(n == 1 ? singular : plural);
}
