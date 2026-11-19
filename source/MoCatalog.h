/* MoCatalog.h
A read-only reader for GNU gettext binary catalogs (.mo files).

The file is kept in memory exactly as it was read and is searched in place with a
binary search, so loading is instant and no per-string memory is allocated, no
matter how many thousands of strings the catalog holds. Contexts (msgctxt) and
plural forms (msgid_plural) are supported.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>



class MoCatalog {
public:
	// Parse a complete .mo file. Returns false, leaving the catalog empty, if the
	// data is not a valid catalog. All offsets are bounds-checked, so a corrupt or
	// truncated file can never read outside of the buffer.
	bool Load(std::string bytes);
	bool Empty() const;
	size_t Size() const;

	// Look up a message by its text. A message that was made with a context (msgctxt) is also found
	// by its text alone, if there is no message that has exactly this text without a context. This
	// is for catalogs that give every message an ID as its context. If several messages have the
	// same text, the first in the catalog is used. Returns an empty view if there is no translation.
	// The returned view stays valid until the catalog is loaded again or destroyed.
	std::string_view Find(std::string_view msgid) const;
	std::string_view Find(std::string_view context, std::string_view msgid) const;
	// Look up the correct plural form of the translation for the number n.
	std::string_view FindPlural(std::string_view singular, std::string_view plural, long long n) const;

	// A field from the catalog's header entry, e.g. "Language" or "X-Language-Name".
	std::string Header(std::string_view field) const;
	// Read only the header field from a .mo file on disk, without loading the
	// whole file. This is how the list of languages is built at startup.
	static std::string ReadHeader(const std::filesystem::path &path, std::string_view field);

	// Parse the value of a field from the text of a header entry.
	static std::string HeaderField(std::string_view header, std::string_view field);


private:
	bool Fail();
	uint32_t Read32(size_t offset) const;
	bool Fits(uint64_t offset, uint64_t length) const;
	std::string_view Original(size_t index) const;
	std::string_view Translation(size_t index) const;
	size_t Sorted(size_t position) const;
	std::string_view FindExact(std::string_view key) const;
	std::string_view Bare(size_t index) const;
	int PluralIndex(long long n) const;


private:
	std::string data;
	bool swapped = false;
	uint32_t count = 0;
	uint32_t originals = 0;
	uint32_t translations = 0;
	// Only filled in if the file's strings are not already sorted.
	std::vector<uint32_t> order;
	// The messages that have a context, in the order of their text without it.
	std::vector<uint32_t> bare;

	int pluralCount = 2;
	std::string pluralExpression;
};
