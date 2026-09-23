#include <ps_detect.h>
#include <algorithm>
#include <cctype>

namespace PsDetect {

bool IsPostScript(const char* header, size_t size) {
	if (size < 2) return false;
	return header[0] == '%' && header[1] == '!';
}

bool IsEPIF(const char* header, size_t size) {
	if (size < 4) return false;
	const auto* h = reinterpret_cast<const unsigned char*>(header);
	return h[0] == 0xC5 && h[1] == 0xD0 && h[2] == 0xD3 && h[3] == 0xC6;
}

bool IsPostScriptExtension(const std::string& ext) {
	std::string lower = ext;
	std::transform(lower.begin(), lower.end(), lower.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower == "ps" || lower == "eps" || lower == "epsf" || lower == "epsi";
}

}  // namespace PsDetect
