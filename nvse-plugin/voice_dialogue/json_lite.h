#pragma once

// Just enough JSON for the helper protocol: we write flat objects and read
// a couple of top-level fields back out of the helper's one-line replies.

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace json
{
	// Game text is Windows-1252; the helper expects UTF-8.
	inline std::string FromGameText(const char *text)
	{
		int wlen = MultiByteToWideChar(1252, 0, text, -1, nullptr, 0);
		if (wlen <= 0) return {};
		std::wstring wide(wlen, L'\0');
		MultiByteToWideChar(1252, 0, text, -1, wide.data(), wlen);
		int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string utf8(len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), len, nullptr, nullptr);
		utf8.resize(len > 0 ? len - 1 : 0);
		return utf8;
	}

	inline std::string Quote(const std::string &s)
	{
		std::string out = "\"";
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
				{
					char esc[8];
					snprintf(esc, sizeof(esc), "\\u%04x", c);
					out += esc;
				}
				else
					out += (char)c;
			}
		}
		return out + "\"";
	}

	// `version` comes back on every answer, so the game can ignore answers for old options.
	inline std::string SetOptions(const std::vector<std::string> &options, unsigned version)
	{
		std::string out = "{\"cmd\":\"set_options\",\"v\":" + std::to_string(version) + ",\"options\":[";
		for (size_t i = 0; i < options.size(); i++)
		{
			if (i) out += ",";
			out += Quote(options[i]);
		}
		return out + "]}";
	}

	// Position just after `"key":` (and any spaces), or npos.
	inline size_t FindValue(const std::string &line, const char *key)
	{
		std::string needle = std::string("\"") + key + "\"";
		size_t pos = line.find(needle);
		if (pos == std::string::npos) return pos;
		pos = line.find(':', pos + needle.size());
		if (pos == std::string::npos) return pos;
		pos++;
		while (pos < line.size() && line[pos] == ' ') pos++;
		return pos;
	}

	// Reads a string value; good enough for the helper's ASCII-escaped output.
	inline std::string GetString(const std::string &line, const char *key)
	{
		size_t pos = FindValue(line, key);
		if (pos == std::string::npos || pos >= line.size() || line[pos] != '"') return {};
		std::string out;
		for (pos++; pos < line.size() && line[pos] != '"'; pos++)
		{
			if (line[pos] == '\\' && pos + 1 < line.size())
			{
				pos++;
				if (line[pos] == 'u') { out += '?'; pos += 4; continue; }
				out += line[pos] == 'n' ? '\n' : line[pos];
			}
			else
				out += line[pos];
		}
		return out;
	}

	inline int GetInt(const std::string &line, const char *key, int fallback)
	{
		size_t pos = FindValue(line, key);
		if (pos == std::string::npos) return fallback;
		char *end = nullptr;
		long v = strtol(line.c_str() + pos, &end, 10);
		return end == line.c_str() + pos ? fallback : (int)v;
	}
}
