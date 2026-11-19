/* Translations.h
Language selection and text lookup, backed by gettext .mo catalogs.

Catalogs live in:   languages/<code>/LC_MESSAGES/endless-sky.mo
where <code> is a locale code such as "es", "it" or "pt_BR". English is built in:
it is simply the original text, so it needs no catalog.
*/

#pragma once

#include <string>
#include <string_view>



class Translations {
public:
	// Scan the "languages" folder and restore the player's last choice.
	static void Init();

	// The name of the active language, e.g. "English" or "Espanol".
	static const std::string &Current();
	// The number of languages available, including English.
	static size_t Count();

	// Switch to the language with this code ("en" or an empty string is English)
	// and remember it for the next launch.
	static void Set(const std::string &code);
	// Switch to the next language in the list, wrapping around to English.
	static void Next();

	// gettext("text"): the translation, or the text itself if there isn't one.
	static std::string Get(std::string_view msgid);
	// pgettext("context", "text"): for identical English words with different meanings.
	static std::string GetContext(std::string_view context, std::string_view msgid);
	// ngettext("one", "many", n): picks the right plural form for the active language.
	static std::string GetPlural(std::string_view singular, std::string_view plural, long long n);
};
