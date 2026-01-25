#include "profileapi.h"

#include "common.h"
#include "context.h"
#include "errors.h"
#include "files.h"
#include "internal.h"
#include "strutil.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct IniEntry {
	std::string name;
	std::string nameLower;
	std::string value;
};

struct IniSection {
	std::string name;
	std::string nameLower;
	std::vector<IniEntry> entries;
};

bool isWhitespace(char ch) {
	return ch == ' ' || ch == '\t';
}

std::string_view trimWhitespace(std::string_view value) {
	size_t start = 0;
	while (start < value.size() && isWhitespace(value[start])) {
		++start;
	}
	size_t end = value.size();
	while (end > start && isWhitespace(value[end - 1])) {
		--end;
	}
	return value.substr(start, end - start);
}

void trimTrailingBlanks(std::string &value) {
	while (!value.empty() && isWhitespace(value.back())) {
		value.pop_back();
	}
}

std::string normalizeValue(std::string_view value) {
	std::string out(trimWhitespace(value));
	trimTrailingBlanks(out);
	if (out.size() >= 2) {
		const char first = out.front();
		const char last = out.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			out = out.substr(1, out.size() - 2);
		}
	}
	return out;
}

bool isWindowsAbsolutePath(std::string_view path) {
	if (path.size() >= 2 && path[1] == ':') {
		return true;
	}
	if (path.rfind("\\\\", 0) == 0 || path.rfind("//", 0) == 0) {
		return true;
	}
	return false;
}

bool parseIniFile(const std::filesystem::path &path, std::vector<IniSection> &sections) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return false;
	}

	IniSection *current = nullptr;
	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		std::string_view view = trimWhitespace(line);
		if (view.empty()) {
			continue;
		}
		if (view[0] == ';' || view[0] == '#') {
			continue;
		}
		if (view[0] == '[') {
			size_t close = view.find(']');
			if (close != std::string_view::npos) {
				std::string_view nameView = trimWhitespace(view.substr(1, close - 1));
				std::string name(nameView);
				std::string nameLower = stringToLower(nameView);
				auto it = std::find_if(sections.begin(), sections.end(),
									   [&](const IniSection &section) { return section.nameLower == nameLower; });
				if (it != sections.end()) {
					current = &(*it);
				} else {
					IniSection section;
					section.name = std::move(name);
					section.nameLower = std::move(nameLower);
					sections.push_back(std::move(section));
					current = &sections.back();
				}
				continue;
			}
		}

		if (!current) {
			continue;
		}
		size_t equals = view.find('=');
		if (equals == std::string_view::npos) {
			continue;
		}
		std::string_view keyView = trimWhitespace(view.substr(0, equals));
		if (keyView.empty()) {
			continue;
		}
		std::string_view valueView = view.substr(equals + 1);

		IniEntry entry;
		entry.name.assign(keyView);
		entry.nameLower = stringToLower(keyView);
		entry.value = normalizeValue(valueView);
		current->entries.push_back(std::move(entry));
	}
	return true;
}

DWORD copyStringToBuffer(const std::string &value, LPSTR buffer, DWORD nSize) {
	if (nSize == 0) {
		return 0;
	}
	const size_t len = value.size();
	const size_t copyLen = std::min(len, static_cast<size_t>(nSize - 1));
	std::memcpy(buffer, value.c_str(), copyLen);
	buffer[copyLen] = '\0';
	return (copyLen == len) ? static_cast<DWORD>(copyLen) : (nSize - 1);
}

DWORD copyMultiString(const std::vector<std::string> &items, LPSTR buffer, DWORD nSize) {
	if (nSize == 0) {
		return 0;
	}
	if (items.empty()) {
		buffer[0] = '\0';
		if (nSize > 1) {
			buffer[1] = '\0';
		}
		return 0;
	}

	size_t pos = 0;
	bool truncated = false;
	for (const auto &item : items) {
		if (nSize <= pos + 2) {
			truncated = true;
			break;
		}
		const size_t availableForData = static_cast<size_t>(nSize) - pos - 2;
		const size_t copyLen = std::min(item.size(), availableForData);
		std::memcpy(buffer + pos, item.data(), copyLen);
		pos += copyLen;
		if (copyLen < item.size()) {
			truncated = true;
			break;
		}
		buffer[pos++] = '\0';
	}

	if (truncated) {
		if (nSize >= 1) {
			buffer[nSize - 1] = '\0';
		}
		if (nSize >= 2) {
			buffer[nSize - 2] = '\0';
			return nSize - 2;
		}
		return 0;
	}

	buffer[pos++] = '\0';
	return static_cast<DWORD>(pos - 1);
}

std::vector<std::string> gatherSectionNames(const std::vector<IniSection> &sections) {
	std::vector<std::string> names;
	names.reserve(sections.size());
	for (const auto &section : sections) {
		names.push_back(section.name);
	}
	return names;
}

const IniSection *findSection(const std::vector<IniSection> &sections, std::string_view name) {
	std::string lower = stringToLower(name);
	for (const auto &section : sections) {
		if (section.nameLower == lower) {
			return &section;
		}
	}
	return nullptr;
}

std::vector<std::string> gatherKeyNames(const IniSection *section) {
	std::vector<std::string> keys;
	if (!section) {
		return keys;
	}
	keys.reserve(section->entries.size());
	for (const auto &entry : section->entries) {
		keys.push_back(entry.name);
	}
	return keys;
}

std::optional<std::string> findValue(const IniSection *section, std::string_view keyName) {
	if (!section) {
		return std::nullopt;
	}
	std::string keyLower = stringToLower(keyName);
	std::optional<std::string> value;
	for (const auto &entry : section->entries) {
		if (entry.nameLower == keyLower) {
			value = entry.value;
		}
	}
	return value;
}

} // namespace

namespace kernel32 {

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("STUB: QueryPerformanceCounter(%p)\n", lpPerformanceCount);
	if (!lpPerformanceCount) {
		kernel32::setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	lpPerformanceCount->QuadPart = 0;
	return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("STUB: QueryPerformanceFrequency(%p)\n", lpFrequency);
	if (!lpFrequency) {
		kernel32::setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	lpFrequency->QuadPart = 1;
	return TRUE;
}

DWORD WINAPI GetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString,
									  DWORD nSize, LPCSTR lpFileName) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetPrivateProfileStringA(%s, %s, %s, %p, %u, %s)\n", lpAppName ? lpAppName : "(null)",
			  lpKeyName ? lpKeyName : "(null)", lpDefault ? lpDefault : "(null)", lpReturnedString, nSize,
			  lpFileName ? lpFileName : "(null)");

	if (!lpReturnedString) {
		setLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (nSize == 0) {
		setLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	if (!lpFileName) {
		setLastError(ERROR_INVALID_PARAMETER);
		lpReturnedString[0] = '\0';
		return 0;
	}

	std::string windowsPath(lpFileName);
	if (!isWindowsAbsolutePath(windowsPath)) {
		windowsPath = std::string("C:\\Windows\\") + windowsPath;
	}

	std::filesystem::path hostPath = files::pathFromWindows(windowsPath.c_str());
	std::vector<IniSection> sections;
	if (!parseIniFile(hostPath, sections)) {
		setLastError(ERROR_FILE_NOT_FOUND);
	}

	if (!lpAppName) {
		auto names = gatherSectionNames(sections);
		return copyMultiString(names, lpReturnedString, nSize);
	}

	const IniSection *section = findSection(sections, lpAppName);
	if (!lpKeyName) {
		auto keys = gatherKeyNames(section);
		return copyMultiString(keys, lpReturnedString, nSize);
	}

	auto value = findValue(section, lpKeyName);
	if (!value.has_value()) {
		std::string fallback = lpDefault ? std::string(lpDefault) : std::string();
		trimTrailingBlanks(fallback);
		return copyStringToBuffer(fallback, lpReturnedString, nSize);
	}

	return copyStringToBuffer(value.value(), lpReturnedString, nSize);
}

} // namespace kernel32
