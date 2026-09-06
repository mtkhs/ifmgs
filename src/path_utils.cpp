#include <path_utils.h>

namespace Path {

// MultiByteToWideChar / WideCharToMultiByte called with -1 (auto-len) returns
// a length that includes the trailing NUL; size strings to n-1 to exclude it.

std::string WideToUtf8(LPCWSTR wide) {
	if (!wide) return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (n <= 1) return "";
	std::string out(n - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], n, NULL, NULL);
	return out;
}

std::wstring Utf8ToWide(const char* utf8) {
	if (!utf8 || !*utf8) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (n <= 1) return L"";
	std::wstring out(n - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
	return out;
}

std::wstring Utf8ToWide(const std::string& utf8) {
	return Utf8ToWide(utf8.c_str());
}

// CP_ACP -> Wide -> UTF-8 (no direct ANSI->UTF-8 API).
std::string AnsiToUtf8(const char* ansi) {
	if (!ansi) return "";
	int wn = MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0);
	if (wn <= 1) return "";
	std::wstring w(wn - 1, 0);
	MultiByteToWideChar(CP_ACP, 0, ansi, -1, &w[0], wn);
	return WideToUtf8(w.c_str());
}

}  // namespace Path
