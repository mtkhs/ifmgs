#ifndef MGS_PATH_UTILS_H
#define MGS_PATH_UTILS_H

#include <windows.h>
#include <string>

namespace Path {

// Plugin uses UTF-8 internally; entry points convert in, file APIs convert out.
// Returns "" on failure (NULL input, conversion error, empty result).
std::string AnsiToUtf8(const char* ansi);
std::string WideToUtf8(LPCWSTR wide);
std::wstring Utf8ToWide(const char* utf8);
std::wstring Utf8ToWide(const std::string& utf8);

}  // namespace Path

#endif
