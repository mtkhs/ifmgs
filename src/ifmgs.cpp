#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>
#include <susie.h>
#include <bmp.h>
#include <ps_detect.h>
#include <path_utils.h>

bool renderBmp(int pageNumber, const std::string& inputPath, std::vector<uint8_t>& outputBuffer, bool isPostScriptOrEps = false);
bool getEpsBoundingBox(const std::string& path, int& widthPx, int& heightPx);

extern "C" int __stdcall GetPluginInfo(int infono, LPSTR buf, int buflen)
{
	if (infono && (buflen < 64)) infono = -1;  // force default branch (returns "")
	switch (infono) {
		case 0:
			strcpy(buf, "00IN");
			break;
		case 1:
			strcpy(buf, "PostScript/EPS Plug-in Version 0.1 (C) mtkhs");
			break;
		case 2:
			strcpy(buf, "*.ps;*.eps");
			break;
		case 3:
			strcpy(buf, "PostScript (*.ps);EPS (*.eps)");
			break;
		default:
			buf[0] = '\0';
			break;
	}
	return static_cast<int>(strlen(buf));
}

// All strings are ASCII; reuse the ANSI version and widen via CP_ACP.
extern "C" int __stdcall GetPluginInfoW(int infono, LPWSTR buf, int buflen)
{
	char bufA[0x400];

	if (infono && (buflen < 64)) {
		buf[0] = L'\0';
		return 0;
	}
	GetPluginInfo(infono, bufA, 0x400);
	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, bufA, -1, buf, buflen);
	buf[buflen - 1] = L'\0';
	return static_cast<int>(wcslen(buf));
}

static int SusieIsSupportedFromFile(const char* filename, const void* dw)
{
	if (!filename) return 0;

	std::string fname(filename);
	size_t dotPos = fname.find_last_of('.');
	if (dotPos == std::string::npos) return 0;
	std::string ext = fname.substr(dotPos + 1);
	if (!PsDetect::IsPostScriptExtension(ext)) return 0;

	// dw NULL = host doesn't supply head bytes; accept on extension alone.
	if (dw) {
		const char* header = static_cast<const char*>(dw);
		if (PsDetect::IsPostScript(header, 8) || PsDetect::IsEPIF(header, 8)) {
			return 1;
		}
		return 0;
	}
	return 1;
}

extern "C" int __stdcall IsSupported(LPCSTR filename, const void* dw)
{
	return SusieIsSupportedFromFile(filename, dw);
}

extern "C" int __stdcall IsSupportedW(LPCWSTR filename, const void* dw)
{
	std::string utf8_filename = Path::WideToUtf8(filename);
	return utf8_filename.empty() ? 0 : SusieIsSupportedFromFile(utf8_filename.c_str(), dw);
}

static int SusieGetPictureInfoFromFile(const char* filename, struct PictureInfo* lpInfo)
{
	if (!filename || !lpInfo) return SUSIEERROR_NOTSUPPORT;

	int width, height;
	// %%(HiRes)BoundingBox gives dimensions without rendering; otherwise render.
	if (!getEpsBoundingBox(filename, width, height)) {
		std::vector<uint8_t> bmpData;
		if (!renderBmp(1, filename, bmpData, /*isPS=*/true) || bmpData.empty()) {
			return SUSIEERROR_NOTSUPPORT;
		}
		if (!Bmp::GetInfo(bmpData, width, height)) {
			return SUSIEERROR_UNKNOWNFORMAT;
		}
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

extern "C" int __stdcall GetPictureInfo(LPCSTR buf, LONG_PTR len, unsigned int flag, struct PictureInfo* lpInfo)
{
	if (flag & 7) return SUSIEERROR_NOTSUPPORT;  // memory-input mode unsupported (GS reads via path)
	std::string utf8 = Path::AnsiToUtf8(buf);
	if (utf8.empty()) return SUSIEERROR_NOTSUPPORT;
	return SusieGetPictureInfoFromFile(utf8.c_str(), lpInfo);
}

extern "C" int __stdcall GetPictureInfoW(LPCWSTR buf, LONG_PTR len, unsigned int flag, struct PictureInfo* lpInfo)
{
	if (flag & 7) return SUSIEERROR_NOTSUPPORT;
	std::string utf8 = Path::WideToUtf8(buf);
	if (utf8.empty()) return SUSIEERROR_NOTSUPPORT;
	return SusieGetPictureInfoFromFile(utf8.c_str(), lpInfo);
}

static int SusieGetPictureFromFile(const char* filename, HLOCAL* pHBInfo, HLOCAL* pHBm)
{
	if (!filename || !pHBInfo || !pHBm) return SUSIEERROR_NOTSUPPORT;

	std::vector<uint8_t> bmpData;
	if (!renderBmp(1, filename, bmpData, /*isPS=*/true) || bmpData.empty()) {
		return SUSIEERROR_NOTSUPPORT;
	}

	int width, height;
	if (!Bmp::GetInfo(bmpData, width, height)) {
		return SUSIEERROR_UNKNOWNFORMAT;
	}

	HLOCAL hBInfo = LocalAlloc(LHND, sizeof(BITMAPINFOHEADER));
	HLOCAL hBm = LocalAlloc(LHND, bmpData.size());

	if (!hBInfo || !hBm) {
		if (hBInfo) LocalFree(hBInfo);
		if (hBm) LocalFree(hBm);
		return SUSIEERROR_FAULTMEMORY;
	}

	BITMAPINFOHEADER* pBmi = (BITMAPINFOHEADER*)LocalLock(hBInfo);
	if (pBmi) {
		int lineBytes = Bmp::CalculateLineBytes(width, 24);
		int imageSize = lineBytes * abs(height);

		pBmi->biSize = sizeof(BITMAPINFOHEADER);
		pBmi->biWidth = width;
		pBmi->biHeight = height;
		pBmi->biPlanes = 1;
		pBmi->biBitCount = 24;
		pBmi->biCompression = BI_RGB;
		pBmi->biSizeImage = imageSize;
		pBmi->biXPelsPerMeter = 0;
		pBmi->biYPelsPerMeter = 0;
		pBmi->biClrUsed = 0;
		pBmi->biClrImportant = 0;
		LocalUnlock(hBInfo);
	}

	// pHBm contract: pixel data only (skip the 54-byte BMP headers).
	void* pBits = LocalLock(hBm);
	if (pBits) {
		size_t bmpHeaderSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
		if (bmpData.size() > bmpHeaderSize) {
			size_t bitsSize = bmpData.size() - bmpHeaderSize;
			memcpy(pBits, bmpData.data() + bmpHeaderSize, bitsSize);
		}
		LocalUnlock(hBm);
	}

	*pHBInfo = hBInfo;
	*pHBm = hBm;

	return SUSIEERROR_NOERROR;
}

extern "C" int __stdcall GetPicture(LPCSTR buf, LONG_PTR len, unsigned int flag, HLOCAL* pHBInfo, HLOCAL* pHBm, FARPROC lpProgressCallback, LONG_PTR lData)
{
	if (flag & 7) return SUSIEERROR_NOTSUPPORT;
	std::string utf8 = Path::AnsiToUtf8(buf);
	if (utf8.empty()) return SUSIEERROR_NOTSUPPORT;
	return SusieGetPictureFromFile(utf8.c_str(), pHBInfo, pHBm);
}

extern "C" int __stdcall GetPictureW(LPCWSTR buf, LONG_PTR len, unsigned int flag, HLOCAL* pHBInfo, HLOCAL* pHBm, FARPROC lpProgressCallback, LONG_PTR lData)
{
	if (flag & 7) return SUSIEERROR_NOTSUPPORT;
	std::string utf8 = Path::WideToUtf8(buf);
	if (utf8.empty()) return SUSIEERROR_NOTSUPPORT;
	return SusieGetPictureFromFile(utf8.c_str(), pHBInfo, pHBm);
}

// Stubs so hosts cleanly fall back to GetPicture instead of probing.
extern "C" int __stdcall GetPreview(LPCSTR, LONG_PTR, unsigned int, HLOCAL*, HLOCAL*, FARPROC, LONG_PTR)
{
	return SUSIEERROR_NOTSUPPORT;
}

extern "C" int __stdcall GetPreviewW(LPCWSTR, LONG_PTR, unsigned int, HLOCAL*, HLOCAL*, FARPROC, LONG_PTR)
{
	return SUSIEERROR_NOTSUPPORT;
}
