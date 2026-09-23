#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <susie.h>
#include <susie_util.h>
#include <bmp.h>
#include <ps_detect.h>
#include <path_utils.h>
#include <ghostscript_render.h>

namespace {

const char* kInfoA[] = { "00IN", "PostScript/EPS Plug-in Version 0.2 (C) mtkhs", "*.ps;*.eps", "PostScript (*.ps);EPS (*.eps)" };
constexpr int kInfoCount = 4;

int IsSupportedImpl(const char* filename_utf8, const void* dw)
{
	std::string fname(filename_utf8);
	size_t dotPos = fname.find_last_of('.');
	if (dotPos == std::string::npos) return 0;
	if (!PsDetect::IsPostScriptExtension(fname.substr(dotPos + 1))) return 0;

	// No head bytes (NULL or a file handle): accept on extension alone.
	const uint8_t* head = SusieUtil::ToHeadPtr(dw);
	if (!head) return 1;
	const char* header = reinterpret_cast<const char*>(head);
	return (PsDetect::IsPostScript(header, 8) || PsDetect::IsEPIF(header, 8)) ? 1 : 0;
}

int GetPictureInfoImpl(const char* filename, SUSIE_PICTUREINFO* lpInfo)
{
	int width, height;
	// %%(HiRes)BoundingBox gives dimensions without rendering; otherwise render.
	if (!getEpsBoundingBox(filename, width, height)) {
		std::vector<uint8_t> bmpData;
		if (!renderBmp(1, filename, bmpData, /*isPS=*/true)) return SUSIEERROR_UNKNOWNFORMAT;
		if (!Bmp::GetInfo(bmpData, width, height)) return SUSIEERROR_BROKENDATA;
	}

	lpInfo->left = 0;
	lpInfo->top = 0;
	lpInfo->width = width;
	lpInfo->height = height;
	lpInfo->x_density = 0;
	lpInfo->y_density = 0;
	lpInfo->colorDepth = 24;
	lpInfo->hInfo = NULL;
	return SUSIEERROR_NOERROR;
}

int GetPictureImpl(const char* filename, HLOCAL* pHBInfo, HLOCAL* pHBm)
{
	*pHBInfo = NULL;
	*pHBm = NULL;

	std::vector<uint8_t> bmpData;
	if (!renderBmp(1, filename, bmpData, /*isPS=*/true)) return SUSIEERROR_UNKNOWNFORMAT;

	int width, height;
	if (!Bmp::GetInfo(bmpData, width, height)) return SUSIEERROR_BROKENDATA;

	// pHBm contract: pixel data only (skip the 54-byte BMP headers).
	constexpr size_t kBmpHeaderSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	if (bmpData.size() <= kBmpHeaderSize) return SUSIEERROR_BROKENDATA;
	const size_t bitsSize = bmpData.size() - kBmpHeaderSize;

	HLOCAL hBInfo = LocalAlloc(LHND, sizeof(BITMAPINFOHEADER));
	HLOCAL hBm = LocalAlloc(LHND, bitsSize);
	auto* pBmi = hBInfo ? static_cast<BITMAPINFOHEADER*>(LocalLock(hBInfo)) : nullptr;
	void* pBits = hBm ? LocalLock(hBm) : nullptr;
	if (!pBmi || !pBits) {
		if (pBmi) LocalUnlock(hBInfo);
		if (pBits) LocalUnlock(hBm);
		if (hBInfo) LocalFree(hBInfo);
		if (hBm) LocalFree(hBm);
		return SUSIEERROR_FAULTMEMORY;
	}

	pBmi->biSize = sizeof(BITMAPINFOHEADER);
	pBmi->biWidth = width;
	pBmi->biHeight = height;
	pBmi->biPlanes = 1;
	pBmi->biBitCount = 24;
	pBmi->biCompression = BI_RGB;
	pBmi->biSizeImage = static_cast<DWORD>(bitsSize);
	memcpy(pBits, bmpData.data() + kBmpHeaderSize, bitsSize);

	LocalUnlock(hBInfo);
	LocalUnlock(hBm);
	*pHBInfo = hBInfo;
	*pHBm = hBm;
	return SUSIEERROR_NOERROR;
}

}  // namespace

extern "C" {

int __stdcall GetPluginInfo(int infono, LPSTR buf, int buflen)
{
	if (!buf || buflen <= 0) return 0;
	if (infono < 0 || infono >= kInfoCount) { buf[0] = '\0'; return 0; }
	int n = static_cast<int>(strlen(kInfoA[infono]));
	if (n >= buflen) n = buflen - 1;
	memcpy(buf, kInfoA[infono], n);
	buf[n] = '\0';
	return n;
}

int __stdcall GetPluginInfoW(int infono, LPWSTR buf, int buflen)
{
	if (!buf || buflen <= 0) return 0;
	char bufA[256];
	int n = GetPluginInfo(infono, bufA, sizeof(bufA));
	if (n == 0) { buf[0] = L'\0'; return 0; }
	MultiByteToWideChar(CP_ACP, 0, bufA, -1, buf, buflen);
	buf[buflen - 1] = L'\0';
	return static_cast<int>(wcslen(buf));
}

int __stdcall IsSupported(LPCSTR filename, const void* dw)
{
	try {
		if (!filename) return 0;
		std::string utf8 = Path::AnsiToUtf8(filename);
		return utf8.empty() ? 0 : IsSupportedImpl(utf8.c_str(), dw);
	} catch (...) { return 0; }
}

int __stdcall IsSupportedW(LPCWSTR filename, const void* dw)
{
	try {
		if (!filename) return 0;
		std::string utf8 = Path::WideToUtf8(filename);
		return utf8.empty() ? 0 : IsSupportedImpl(utf8.c_str(), dw);
	} catch (...) { return 0; }
}

int __stdcall GetPictureInfo(LPCSTR buf, LONG_PTR, unsigned int flag, SUSIE_PICTUREINFO* lpInfo)
{
	try {
		if (!buf || !lpInfo) return SUSIEERROR_INTERNAL;
		if (flag & SUSIE_SOURCE_MASK) return SUSIEERROR_NOTSUPPORT;  // memory input: GS reads via path
		std::string utf8 = Path::AnsiToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetPictureInfoImpl(utf8.c_str(), lpInfo);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetPictureInfoW(LPCWSTR buf, LONG_PTR, unsigned int flag, SUSIE_PICTUREINFO* lpInfo)
{
	try {
		if (!buf || !lpInfo) return SUSIEERROR_INTERNAL;
		if (flag & SUSIE_SOURCE_MASK) return SUSIEERROR_NOTSUPPORT;
		std::string utf8 = Path::WideToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetPictureInfoImpl(utf8.c_str(), lpInfo);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetPicture(LPCSTR buf, LONG_PTR, unsigned int flag, HLOCAL* pHBInfo, HLOCAL* pHBm, SUSIE_PROGRESS, LONG_PTR)
{
	try {
		if (!buf || !pHBInfo || !pHBm) return SUSIEERROR_INTERNAL;
		if (flag & SUSIE_SOURCE_MASK) return SUSIEERROR_NOTSUPPORT;
		std::string utf8 = Path::AnsiToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetPictureImpl(utf8.c_str(), pHBInfo, pHBm);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetPictureW(LPCWSTR buf, LONG_PTR, unsigned int flag, HLOCAL* pHBInfo, HLOCAL* pHBm, SUSIE_PROGRESS, LONG_PTR)
{
	try {
		if (!buf || !pHBInfo || !pHBm) return SUSIEERROR_INTERNAL;
		if (flag & SUSIE_SOURCE_MASK) return SUSIEERROR_NOTSUPPORT;
		std::string utf8 = Path::WideToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetPictureImpl(utf8.c_str(), pHBInfo, pHBm);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

// Stubs so hosts cleanly fall back to GetPicture instead of probing.
int __stdcall GetPreview(LPCSTR, LONG_PTR, unsigned int, HLOCAL*, HLOCAL*, SUSIE_PROGRESS, LONG_PTR)
{
	return SUSIEERROR_NOTSUPPORT;
}

int __stdcall GetPreviewW(LPCWSTR, LONG_PTR, unsigned int, HLOCAL*, HLOCAL*, SUSIE_PROGRESS, LONG_PTR)
{
	return SUSIEERROR_NOTSUPPORT;
}

}  // extern "C"
