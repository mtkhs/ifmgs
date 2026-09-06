#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "susie.h"
#include <path_utils.h>

int getPdfPageCount(const std::string& pdfData, const char* pdfPath = nullptr);
bool getPdfPageDimensions(const std::string& pdfPath, int pageNumber, int& widthPx, int& heightPx);
bool renderBmp(int pageNumber, const std::string& inputPath, std::vector<uint8_t>& outputBuffer, bool isPostScriptOrEps = false);

extern "C" int __stdcall GetPluginInfo(int infono, LPSTR buf, int buflen)
{
	if (infono && (buflen < 64)) infono = -1;
	switch (infono) {
		case 0:
			strcpy(buf, "00AX");
			break;
		case 1:
			strcpy(buf, "PDF Pages Archive Plug-in Version 0.1 (C) mtkhs");
			break;
		case 2:
			strcpy(buf, "*.pdf;*.ai");
			break;
		case 3:
			strcpy(buf, "PDF/AI Archive (*.pdf;*.ai)");
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
	size_t dotPos = fname.find_last_of(".");
	if (dotPos == std::string::npos) return 0;
	std::string ext = fname.substr(dotPos + 1);
	if (_stricmp(ext.c_str(), "pdf") != 0 && _stricmp(ext.c_str(), "ai") != 0) {
		return 0;
	}

	// "%PDF" magic check (AI files are PDFs underneath); NULL dw = ext-only.
	if (dw) {
		const unsigned char* h = static_cast<const unsigned char*>(dw);
		if (h[0] != '%' || h[1] != 'P' || h[2] != 'D' || h[3] != 'F') return 0;
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

// Reads the whole PDF for fastPdfPageCount. Caching here is pointless because
// afxw reloads the plugin DLL between API calls (verified empirically).
static int readPdfBytes(const char* path, std::string& out) {
	std::wstring widePath = Path::Utf8ToWide(path);
	if (widePath.empty()) return SUSIEERROR_FAULTREAD;
	FILE* fp = _wfopen(widePath.c_str(), L"rb");
	if (!fp) return SUSIEERROR_FAULTREAD;
	if (_fseeki64(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return SUSIEERROR_FAULTREAD;
	}
	__int64 fsz = _ftelli64(fp);
	if (fsz <= 0 || _fseeki64(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return SUSIEERROR_FAULTREAD;
	}

	const size_t fsz_st = static_cast<size_t>(fsz);
	// Catch bad_alloc: C++ exceptions across the extern "C" boundary are UB.
	try {
		out.assign(fsz_st, '\0');
	} catch (...) {
		fclose(fp);
		return SUSIEERROR_FAULTMEMORY;
	}
	size_t r = fread(&out[0], 1, fsz_st, fp);
	fclose(fp);
	if (r != fsz_st) return SUSIEERROR_FAULTREAD;
	return SUSIEERROR_NOERROR;
}

static int SusieGetArchiveInfoFromFile(const char* filename, HLOCAL* lphInf)
{
	if (!filename || !lphInf) return SUSIEERROR_UNKNOWNFORMAT;
	*lphInf = NULL;

	std::string pdfData;
	int rcRead = readPdfBytes(filename, pdfData);
	if (rcRead != SUSIEERROR_NOERROR) {
		return rcRead;
	}

	int pageCount = getPdfPageCount(pdfData, filename);

	// +1 for the method[0]=='\0' sentinel terminating the SUSIE_FINFO array.
	size_t infoSize = sizeof(SUSIE_FINFO) * (pageCount + 1);
	*lphInf = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, infoSize);
	if (!*lphInf) return SUSIEERROR_FAULTMEMORY;

	SUSIE_FINFO* pInfo = (SUSIE_FINFO*)LocalLock(*lphInf);
	if (!pInfo) {
		LocalFree(*lphInf);
		*lphInf = NULL;
		return SUSIEERROR_FAULTMEMORY;
	}

	// filesize stays 0; GetFileInfo fills it in. Rendering here would render
	// every page twice given Susie's two-step (Info -> File) call pattern.
	for (int i = 0; i < pageCount; i++) {
		strcpy(reinterpret_cast<char*>(pInfo[i].method), "store");
		pInfo[i].position = i;
		pInfo[i].compsize = 0;
		pInfo[i].filesize = 0;
		pInfo[i].timestamp = 0;
		pInfo[i].path[0] = '\0';
		sprintf(pInfo[i].filename, "page%03d.bmp", i + 1);
		pInfo[i].crc = 0;
	}
	memset(&pInfo[pageCount], 0, sizeof(SUSIE_FINFO));

	LocalUnlock(*lphInf);
	return 0;
}

extern "C" int __stdcall GetArchiveInfo(LPCSTR buf, LONG_PTR len, unsigned int flag, HLOCAL* lphInf)
{
	std::string utf8 = Path::AnsiToUtf8(buf);
	return utf8.empty() ? SUSIEERROR_UNKNOWNFORMAT : SusieGetArchiveInfoFromFile(utf8.c_str(), lphInf);
}

extern "C" int __stdcall GetArchiveInfoW(LPCWSTR buf, LONG_PTR len, unsigned int flag, HLOCAL* lphInf)
{
	std::string utf8_filename = Path::WideToUtf8(buf);
	return utf8_filename.empty() ? SUSIEERROR_UNKNOWNFORMAT : SusieGetArchiveInfoFromFile(utf8_filename.c_str(), lphInf);
}

static int SusieGetFileInfoFromFile(const char* buf, LONG_PTR len, const char* filename, unsigned int flag, SUSIE_FINFO* lpInfo)
{
	if (!buf || !filename || !lpInfo) return SUSIEERROR_UNKNOWNFORMAT;

	// GetArchiveInfo names entries "page%03d.bmp"; reverse that here.
	int pageNumber;
	if (sscanf(filename, "page%d.bmp", &pageNumber) != 1 || pageNumber < 1) {
		return SUSIEERROR_UNKNOWNFORMAT;
	}

	// Predict BMP byte count from MediaBox without rendering. The match with
	// GetFile's actual render is exact (verified across many DPIs).
	int width, height;
	if (!getPdfPageDimensions(buf, pageNumber, width, height)) {
		return SUSIEERROR_UNKNOWNFORMAT;
	}
	int lineBytes = ((width * 3) + 3) & ~3;
	int bmpSize = 54 + lineBytes * height;  // 54 = BITMAPFILEHEADER + BITMAPINFOHEADER

	memset(lpInfo, 0, sizeof(SUSIE_FINFO));
	strcpy(reinterpret_cast<char*>(lpInfo->method), "store");
	lpInfo->position = pageNumber - 1;
	lpInfo->compsize = bmpSize;
	lpInfo->filesize = bmpSize;
	lpInfo->timestamp = 0;
	lpInfo->path[0] = '\0';
	strcpy(lpInfo->filename, filename);
	lpInfo->crc = 0;
	return 0;
}

extern "C" int __stdcall GetFileInfo(LPCSTR buf, LONG_PTR len, LPCSTR filename, unsigned int flag, SUSIE_FINFO* lpInfo)
{
	std::string utf8 = Path::AnsiToUtf8(buf);
	if (utf8.empty()) return SUSIEERROR_UNKNOWNFORMAT;
	return SusieGetFileInfoFromFile(utf8.c_str(), len, filename, flag, lpInfo);
}

extern "C" int __stdcall GetFileInfoW(LPCWSTR buf, LONG_PTR len, LPCWSTR filename, unsigned int flag, SUSIE_FINFOW* lpInfo)
{
	std::string utf8_filename = Path::WideToUtf8(buf);
	std::string utf8_pagename = Path::WideToUtf8(filename);

	if (utf8_filename.empty() || utf8_pagename.empty()) {
		return SUSIEERROR_UNKNOWNFORMAT;
	}

	// Repack the ANSI SUSIE_FINFO returned by the shared impl into SUSIE_FINFOW.
	SUSIE_FINFO infoA;
	int result = SusieGetFileInfoFromFile(utf8_filename.c_str(), len, utf8_pagename.c_str(), flag, &infoA);
	if (result != 0) return result;

	memset(lpInfo, 0, sizeof(SUSIE_FINFOW));
	memcpy(lpInfo->method, infoA.method, sizeof(infoA.method));
	lpInfo->position = infoA.position;
	lpInfo->compsize = infoA.compsize;
	lpInfo->filesize = infoA.filesize;
	lpInfo->timestamp = infoA.timestamp;

	std::wstring widePath = Path::Utf8ToWide(infoA.path);
	std::wstring wideFilename = Path::Utf8ToWide(infoA.filename);

	wcscpy(lpInfo->path, widePath.c_str());
	wcscpy(lpInfo->filename, wideFilename.c_str());
	lpInfo->crc = infoA.crc;

	return 0;
}

// dest is a UTF-8 file path when flag&0x0100==0, otherwise an HLOCAL* (memory mode).
static int SusieGetFileFromArchive(const char* src, LONG_PTR len, const char* dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	if (!src) return SUSIEERROR_UNKNOWNFORMAT;

	// Render is atomic, so progress can only be 0% / 100%.
	if (progressCallback && progressCallback(0, 100, lData) != 0) {
		return SUSIEERROR_USERCANCEL;
	}

	int pageNumber = static_cast<int>(len) + 1;  // Susie passes 0-based entry index
	if (pageNumber < 1) return SUSIEERROR_UNKNOWNFORMAT;

	std::vector<uint8_t> bitmapData;
	if (!renderBmp(pageNumber, std::string(src), bitmapData)) {
		return SUSIEERROR_UNKNOWNFORMAT;
	}
	if (bitmapData.size() < 54) return SUSIEERROR_BROKENDATA;

	if ((flag & 0x0100) == 0) {
		if (!dest) return SUSIEERROR_UNKNOWNFORMAT;
		std::wstring wideDest = Path::Utf8ToWide(dest);
		if (wideDest.empty()) return SUSIEERROR_FILEWRITE;
		FILE* fp = _wfopen(wideDest.c_str(), L"wb");
		if (!fp) return SUSIEERROR_FILEWRITE;
		fwrite(bitmapData.data(), 1, bitmapData.size(), fp);
		fclose(fp);
	} else {
		// LMEM_MOVEABLE is required: LMEM_FIXED or GlobalAlloc breaks afxw display.
		if (!dest) return SUSIEERROR_UNKNOWNFORMAT;
		HLOCAL* phMem = (HLOCAL*)dest;
		*phMem = LocalAlloc(LMEM_MOVEABLE, bitmapData.size());
		if (!*phMem) return SUSIEERROR_FAULTMEMORY;
		void* pMem = LocalLock(*phMem);
		if (!pMem) {
			LocalFree(*phMem);
			*phMem = NULL;
			return SUSIEERROR_FAULTMEMORY;
		}
		memcpy(pMem, bitmapData.data(), bitmapData.size());
		LocalUnlock(*phMem);
	}

	if (progressCallback && progressCallback(100, 100, lData) != 0) {
		return SUSIEERROR_USERCANCEL;
	}
	return 0;
}

extern "C" int __stdcall GetFile(LPCSTR src, LONG_PTR len, LPSTR dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	std::string utf8_src = Path::AnsiToUtf8(src);
	if (utf8_src.empty()) return SUSIEERROR_UNKNOWNFORMAT;
	if ((flag & 0x0100) == 0) {
		std::string utf8_dest = Path::AnsiToUtf8(dest);
		return SusieGetFileFromArchive(utf8_src.c_str(), len, utf8_dest.c_str(), flag, progressCallback, lData);
	}
	// dest is HLOCAL*; pass through unchanged.
	return SusieGetFileFromArchive(utf8_src.c_str(), len, dest, flag, progressCallback, lData);
}

extern "C" int __stdcall GetFileW(LPCWSTR src, LONG_PTR len, LPWSTR dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	std::string utf8_src = Path::WideToUtf8(src);
	if (utf8_src.empty()) return SUSIEERROR_UNKNOWNFORMAT;
	if ((flag & 0x0100) == 0) {
		std::string utf8_dest = Path::WideToUtf8(dest);
		return SusieGetFileFromArchive(utf8_src.c_str(), len, utf8_dest.c_str(), flag, progressCallback, lData);
	}
	return SusieGetFileFromArchive(utf8_src.c_str(), len, reinterpret_cast<const char*>(dest), flag, progressCallback, lData);
}
