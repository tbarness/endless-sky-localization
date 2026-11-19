/* MoCatalog.cpp */

#include "MoCatalog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>

using namespace std;

namespace {
	constexpr uint32_t MAGIC = 0x950412de;
	constexpr uint32_t MAGIC_SWAPPED = 0xde120495;
	constexpr size_t HEADER_SIZE = 28;

	uint32_t Swap(uint32_t value)
	{
		return (value >> 24) | ((value >> 8) & 0xff00) | ((value << 8) & 0xff0000) | (value << 24);
	}

	string_view Trim(string_view text)
	{
		while(!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
			text.remove_prefix(1);
		while(!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
			text.remove_suffix(1);
		return text;
	}



	// Evaluates the C-like expression from a "Plural-Forms: plural=..." header, e.g.
	// "(n != 1)" or "n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 ? 1 : 2".
	class PluralExpression {
	public:
		PluralExpression(string_view text, long long n) : text(text), n(n) {}

		long long Evaluate() { return Ternary(); }


	private:
		void Skip()
		{
			while(position < text.size() && (text[position] == ' ' || text[position] == '\t'))
				++position;
		}

		bool Match(string_view token)
		{
			Skip();
			if(text.substr(position, token.size()) != token)
				return false;
			position += token.size();
			return true;
		}

		long long Ternary()
		{
			const long long condition = Or();
			if(!Match("?"))
				return condition;
			const long long yes = Ternary();
			Match(":");
			const long long no = Ternary();
			return condition ? yes : no;
		}

		long long Or()
		{
			long long value = And();
			while(Match("||"))
			{
				const long long right = And();
				value = value || right;
			}
			return value;
		}

		long long And()
		{
			long long value = Equality();
			while(Match("&&"))
			{
				const long long right = Equality();
				value = value && right;
			}
			return value;
		}

		long long Equality()
		{
			long long value = Relational();
			while(true)
			{
				if(Match("=="))
					value = (value == Relational());
				else if(Match("!="))
					value = (value != Relational());
				else
					return value;
			}
		}

		long long Relational()
		{
			long long value = Additive();
			while(true)
			{
				if(Match("<="))
					value = (value <= Additive());
				else if(Match(">="))
					value = (value >= Additive());
				else if(Match("<"))
					value = (value < Additive());
				else if(Match(">"))
					value = (value > Additive());
				else
					return value;
			}
		}

		long long Additive()
		{
			long long value = Multiplicative();
			while(true)
			{
				if(Match("+"))
					value += Multiplicative();
				else if(Match("-"))
					value -= Multiplicative();
				else
					return value;
			}
		}

		long long Multiplicative()
		{
			long long value = Unary();
			while(true)
			{
				if(Match("*"))
					value *= Unary();
				else if(Match("/"))
				{
					const long long divisor = Unary();
					value = divisor ? value / divisor : 0;
				}
				else if(Match("%"))
				{
					const long long divisor = Unary();
					value = divisor ? value % divisor : 0;
				}
				else
					return value;
			}
		}

		long long Unary()
		{
			Skip();
			// A "!" here is a logical not. ("!=" only ever appears between operands.)
			if(Match("!"))
				return !Unary();
			if(Match("("))
			{
				const long long value = Ternary();
				Match(")");
				return value;
			}
			if(position < text.size() && text[position] == 'n')
			{
				++position;
				return n;
			}
			long long value = 0;
			while(position < text.size() && text[position] >= '0' && text[position] <= '9')
				value = value * 10 + (text[position++] - '0');
			return value;
		}


	private:
		string_view text;
		long long n;
		size_t position = 0;
	};
}



bool MoCatalog::Fail()
{
	data.clear();
	order.clear();
	bare.clear();
	swapped = false;
	count = originals = translations = 0;
	pluralCount = 2;
	pluralExpression.clear();
	return false;
}



bool MoCatalog::Load(string bytes)
{
	Fail();
	data = std::move(bytes);
	if(data.size() < HEADER_SIZE)
		return Fail();

	uint32_t magic;
	memcpy(&magic, data.data(), sizeof(magic));
	if(magic == MAGIC)
		swapped = false;
	else if(magic == MAGIC_SWAPPED)
		swapped = true;
	else
		return Fail();

	// The high 16 bits of the revision are the major version. Only 0 and 1 exist.
	if((Read32(4) >> 16) > 1)
		return Fail();

	count = Read32(8);
	originals = Read32(12);
	translations = Read32(16);
	if(!Fits(originals, 8ull * count) || !Fits(translations, 8ull * count))
		return Fail();

	// Validate every string up front so that lookups never need to check again.
	for(uint32_t i = 0; i < count; ++i)
		for(uint32_t table : {originals, translations})
		{
			const size_t entry = table + 8ull * i;
			if(!Fits(Read32(entry + 4), Read32(entry)))
				return Fail();
		}

	// Files written by msgfmt are sorted, which allows a plain binary search. If
	// some other tool wrote this file, build the sorted order ourselves.
	bool sorted = true;
	for(uint32_t i = 1; i < count && sorted; ++i)
		sorted = Original(i - 1) < Original(i);
	if(!sorted)
	{
		order.resize(count);
		iota(order.begin(), order.end(), 0u);
		stable_sort(order.begin(), order.end(),
			[this](uint32_t a, uint32_t b) { return Original(a) < Original(b); });
	}

	// A message that has a context is stored as "context<EOT>text". Note which ones, so that they can also
	// be found by their text. The sort is stable, so of those with the same text the first stays first.
	for(uint32_t i = 0; i < count; ++i)
		if(Original(i).find('\x04') != string_view::npos)
			bare.push_back(i);
	stable_sort(bare.begin(), bare.end(), [this](uint32_t a, uint32_t b) { return Bare(a) < Bare(b); });

	// Read the plural rule, if any.
	const string rule = Header("Plural-Forms");
	const size_t forms = rule.find("nplurals=");
	if(forms != string::npos)
		pluralCount = max(1, min(8, atoi(rule.c_str() + forms + 9)));
	const size_t expression = rule.find("plural=");
	if(expression != string::npos)
	{
		const size_t end = rule.find(';', expression);
		pluralExpression = rule.substr(expression + 7, end == string::npos ? string::npos : end - expression - 7);
	}
	return true;
}



bool MoCatalog::Empty() const
{
	return count == 0;
}



size_t MoCatalog::Size() const
{
	return count;
}



string_view MoCatalog::Find(string_view msgid) const
{
	const string_view exact = FindExact(msgid);
	if(!exact.empty() || bare.empty())
		return exact;

	// No message has exactly this text, so look at those that have a context, without it.
	size_t low = 0;
	size_t high = bare.size();
	while(low < high)
	{
		const size_t middle = low + (high - low) / 2;
		if(Bare(bare[middle]) < msgid)
			low = middle + 1;
		else
			high = middle;
	}
	if(low < bare.size() && Bare(bare[low]) == msgid)
		return Translation(bare[low]);
	return string_view();
}



string_view MoCatalog::FindExact(string_view msgid) const
{
	size_t low = 0;
	size_t high = count;
	while(low < high)
	{
		const size_t middle = low + (high - low) / 2;
		const size_t index = Sorted(middle);
		const int result = msgid.compare(Original(index));
		if(result == 0)
			return Translation(index);
		if(result < 0)
			high = middle;
		else
			low = middle + 1;
	}
	return string_view();
}



string_view MoCatalog::Find(string_view context, string_view msgid) const
{
	// Contexts are stored as "context<EOT>msgid".
	string key;
	key.reserve(context.size() + 1 + msgid.size());
	key.append(context);
	key.push_back('\x04');
	key.append(msgid);
	// With a context given, only the message with that context will do.
	return FindExact(key);
}



string_view MoCatalog::FindPlural(string_view singular, string_view plural, long long n) const
{
	// Plurals are stored as "singular<NUL>plural" with the translated forms
	// separated by NULs in the same way.
	string key;
	key.reserve(singular.size() + 1 + plural.size());
	key.append(singular);
	key.push_back('\0');
	key.append(plural);

	string_view forms = Find(key);
	if(forms.empty())
		return forms;

	int wanted = PluralIndex(n);
	while(wanted-- > 0)
	{
		const size_t separator = forms.find('\0');
		// If the file has fewer forms than the rule claims, use the last one.
		if(separator == string_view::npos)
			break;
		forms.remove_prefix(separator + 1);
	}
	return forms.substr(0, forms.find('\0'));
}



string MoCatalog::Header(string_view field) const
{
	return HeaderField(FindExact(string_view()), field);
}



string MoCatalog::ReadHeader(const filesystem::path &path, string_view field)
{
	ifstream in(path, ios::binary);
	char head[HEADER_SIZE];
	if(!in.read(head, HEADER_SIZE))
		return string();

	uint32_t values[7];
	memcpy(values, head, sizeof(values));
	bool swap;
	if(values[0] == MAGIC)
		swap = false;
	else if(values[0] == MAGIC_SWAPPED)
		swap = true;
	else
		return string();
	for(uint32_t &value : values)
		if(swap)
			value = Swap(value);

	// The empty msgid sorts first, so the header is the first entry of each table.
	if(values[2] == 0)
		return string();
	uint32_t entry[2];
	in.seekg(values[3]);
	if(!in.read(reinterpret_cast<char *>(entry), sizeof(entry)))
		return string();
	if(swap)
		for(uint32_t &value : entry)
			value = Swap(value);
	if(entry[0] != 0)
	{
		// Not sorted, so the header is somewhere else. Fall back to a full load.
		in.clear();
		in.seekg(0);
		MoCatalog catalog;
		catalog.Load(string(istreambuf_iterator<char>(in), istreambuf_iterator<char>()));
		return catalog.Header(field);
	}

	in.seekg(values[4]);
	if(!in.read(reinterpret_cast<char *>(entry), sizeof(entry)))
		return string();
	if(swap)
		for(uint32_t &value : entry)
			value = Swap(value);
	// Headers are tiny. Refuse anything suspicious rather than allocating it.
	if(entry[0] > 65536)
		return string();

	string header(entry[0], '\0');
	in.seekg(entry[1]);
	if(!in.read(header.data(), header.size()))
		return string();
	return HeaderField(header, field);
}



string MoCatalog::HeaderField(string_view header, string_view field)
{
	while(!header.empty())
	{
		const size_t end = header.find('\n');
		const string_view line = header.substr(0, end);
		header = (end == string_view::npos) ? string_view() : header.substr(end + 1);

		if(line.size() > field.size() && line.substr(0, field.size()) == field && line[field.size()] == ':')
			return string(Trim(line.substr(field.size() + 1)));
	}
	return string();
}



uint32_t MoCatalog::Read32(size_t offset) const
{
	uint32_t value;
	memcpy(&value, data.data() + offset, sizeof(value));
	return swapped ? Swap(value) : value;
}



bool MoCatalog::Fits(uint64_t offset, uint64_t length) const
{
	return offset + length <= data.size();
}



string_view MoCatalog::Original(size_t index) const
{
	const size_t entry = originals + 8ull * index;
	return string_view(data).substr(Read32(entry + 4), Read32(entry));
}



string_view MoCatalog::Translation(size_t index) const
{
	const size_t entry = translations + 8ull * index;
	return string_view(data).substr(Read32(entry + 4), Read32(entry));
}



string_view MoCatalog::Bare(size_t index) const
{
	const string_view original = Original(index);
	const size_t separator = original.find('\x04');
	return separator == string_view::npos ? original : original.substr(separator + 1);
}



size_t MoCatalog::Sorted(size_t position) const
{
	return order.empty() ? position : order[position];
}



int MoCatalog::PluralIndex(long long n) const
{
	if(pluralExpression.empty())
		return n != 1;
	const long long index = PluralExpression(pluralExpression, n).Evaluate();
	return static_cast<int>(max(0ll, min<long long>(index, pluralCount - 1)));
}
