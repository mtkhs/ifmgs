#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <susie.h>
#include <susie_util.h>
#include <path_utils.h>
#include <ghostscript_render.h>

namespace {

const char* kInfoA[] = { "00AX", "PDF Pages Archive Plug-in Version 0.1 (C) mtkhs", "*.pdf;*.ai", "PDF/AI Archive (*.pdf;*.ai)" };
constexpr int kInfoCount = 4;

constexpr size_t kBmpHeaderSize = 54;  // BITMAPFILEHEADER + BITMAPINFOHEADER

int IsSupportedImpl(const char* filename_utf8, const void* dw)
{
	std::string fname(filename_utf8);
	size_t dotPos = fname.find_last_of('.');
	if (dotPos == std::string::npos) return 0;
	std::string ext = fname.substr(dotPos + 1);
	if (_stricmp(ext.c_str(), "pdf") != 0 && _stricmp(ext.c_str(), "ai") != 0) return 0;

	// "%PDF" magic (AI files are PDFs underneath). No head bytes: ext-only.
	const uint8_t* h = SusieUtil::ToHeadPtr(dw);
	if (h && memcmp(h, "%PDF", 4) != 0) return 0;
	return 1;
}

// Reads the whole PDF for fastPdfPageCount. Caching here is pointless because
// afxw reloads the plugin DLL between API calls (verified empirically).
int ReadPdfBytes(const char* path, std::string& out)
{
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

int GetArchiveInfoImpl(const char* filename, HLOCAL* lphInf)
{
	*lphInf = NULL;

	std::string pdfData;
	int rcRead = ReadPdfBytes(filename, pdfData);
	if (rcRead != SUSIEERROR_NOERROR) return rcRead;

	int pageCount = getPdfPageCount(pdfData, filename);

	// +1 for the method[0]=='\0' sentinel terminating the SUSIE_FINFO array.
	size_t infoSize = sizeof(SUSIE_FINFO) * (pageCount + 1);
	*lphInf = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, infoSize);
	if (!*lphInf) return SUSIEERROR_FAULTMEMORY;

	SUSIE_FINFO* pInfo = static_cast<SUSIE_FINFO*>(LocalLock(*lphInf));
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
		snprintf(pInfo[i].filename, SUSIE_PATH_MAX, "page%03d.bmp", i + 1);
	}

	LocalUnlock(*lphInf);
	return SUSIEERROR_NOERROR;
}

int GetFileInfoImpl(const char* buf, const char* filename, SUSIE_FINFO* lpInfo)
{
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
	size_t bmpSize = kBmpHeaderSize + static_cast<size_t>(lineBytes) * height;

	memset(lpInfo, 0, sizeof(SUSIE_FINFO));
	strcpy(reinterpret_cast<char*>(lpInfo->method), "store");
	lpInfo->position = pageNumber - 1;
	lpInfo->compsize = bmpSize;
	lpInfo->filesize = bmpSize;
	lstrcpynA(lpInfo->filename, filename, SUSIE_PATH_MAX);
	return SUSIEERROR_NOERROR;
}

// dest is a UTF-8 file path when flag&SUSIE_DEST_MASK==SUSIE_DEST_DISK,
// otherwise an HLOCAL* (memory mode).
int GetFileImpl(const char* src, LONG_PTR len, const char* dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	// Render is atomic, so progress can only be 0% / 100%.
	if (progressCallback && progressCallback(0, 100, lData) != 0) {
		return SUSIEERROR_USERCANCEL;
	}

	int pageNumber = static_cast<int>(len) + 1;  // Susie passes 0-based entry index
	if (pageNumber < 1) return SUSIEERROR_UNKNOWNFORMAT;

	std::vector<uint8_t> bitmapData;
	if (!renderBmp(pageNumber, std::string(src), bitmapData)) return SUSIEERROR_UNKNOWNFORMAT;

	if ((flag & SUSIE_DEST_MASK) == SUSIE_DEST_DISK) {
		if (!dest) return SUSIEERROR_UNKNOWNFORMAT;
		std::wstring wideDest = Path::Utf8ToWide(dest);
		if (wideDest.empty()) return SUSIEERROR_FILEWRITE;
		FILE* fp = _wfopen(wideDest.c_str(), L"wb");
		if (!fp) return SUSIEERROR_FILEWRITE;
		size_t written = fwrite(bitmapData.data(), 1, bitmapData.size(), fp);
		fclose(fp);
		if (written != bitmapData.size()) return SUSIEERROR_FILEWRITE;
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

int __stdcall GetArchiveInfo(LPCSTR buf, LONG_PTR, unsigned int, HLOCAL* lphInf)
{
	try {
		if (!buf || !lphInf) return SUSIEERROR_INTERNAL;
		std::string utf8 = Path::AnsiToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetArchiveInfoImpl(utf8.c_str(), lphInf);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetArchiveInfoW(LPCWSTR buf, LONG_PTR, unsigned int, HLOCAL* lphInf)
{
	try {
		if (!buf || !lphInf) return SUSIEERROR_INTERNAL;
		std::string utf8 = Path::WideToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetArchiveInfoImpl(utf8.c_str(), lphInf);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetFileInfo(LPCSTR buf, LONG_PTR, LPCSTR filename, unsigned int, SUSIE_FINFO* lpInfo)
{
	try {
		if (!buf || !filename || !lpInfo) return SUSIEERROR_INTERNAL;
		std::string utf8 = Path::AnsiToUtf8(buf);
		return utf8.empty() ? SUSIEERROR_FAULTREAD : GetFileInfoImpl(utf8.c_str(), filename, lpInfo);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetFileInfoW(LPCWSTR buf, LONG_PTR, LPCWSTR filename, unsigned int, SUSIE_FINFOW* lpInfo)
{
	try {
		if (!buf || !filename || !lpInfo) return SUSIEERROR_INTERNAL;
		std::string utf8_filename = Path::WideToUtf8(buf);
		std::string utf8_pagename = Path::WideToUtf8(filename);
		if (utf8_filename.empty() || utf8_pagename.empty()) return SUSIEERROR_FAULTREAD;

		// Repack the ANSI SUSIE_FINFO returned by the shared impl into SUSIE_FINFOW.
		SUSIE_FINFO infoA;
		int result = GetFileInfoImpl(utf8_filename.c_str(), utf8_pagename.c_str(), &infoA);
		if (result != SUSIEERROR_NOERROR) return result;

		memset(lpInfo, 0, sizeof(SUSIE_FINFOW));
		memcpy(lpInfo->method, infoA.method, sizeof(infoA.method));
		lpInfo->position = infoA.position;
		lpInfo->compsize = infoA.compsize;
		lpInfo->filesize = infoA.filesize;
		lpInfo->timestamp = infoA.timestamp;
		lstrcpynW(lpInfo->path, Path::Utf8ToWide(infoA.path).c_str(), SUSIE_PATH_MAX);
		lstrcpynW(lpInfo->filename, Path::Utf8ToWide(infoA.filename).c_str(), SUSIE_PATH_MAX);
		lpInfo->crc = infoA.crc;
		return SUSIEERROR_NOERROR;
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetFile(LPCSTR src, LONG_PTR len, LPSTR dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	try {
		if (!src) return SUSIEERROR_INTERNAL;
		std::string utf8_src = Path::AnsiToUtf8(src);
		if (utf8_src.empty()) return SUSIEERROR_FAULTREAD;
		if ((flag & SUSIE_DEST_MASK) == SUSIE_DEST_DISK) {
			std::string utf8_dest = Path::AnsiToUtf8(dest);
			return GetFileImpl(utf8_src.c_str(), len, utf8_dest.c_str(), flag, progressCallback, lData);
		}
		// dest is HLOCAL*; pass through unchanged.
		return GetFileImpl(utf8_src.c_str(), len, dest, flag, progressCallback, lData);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

int __stdcall GetFileW(LPCWSTR src, LONG_PTR len, LPWSTR dest, unsigned int flag, SUSIE_PROGRESS progressCallback, LONG_PTR lData)
{
	try {
		if (!src) return SUSIEERROR_INTERNAL;
		std::string utf8_src = Path::WideToUtf8(src);
		if (utf8_src.empty()) return SUSIEERROR_FAULTREAD;
		if ((flag & SUSIE_DEST_MASK) == SUSIE_DEST_DISK) {
			std::string utf8_dest = Path::WideToUtf8(dest);
			return GetFileImpl(utf8_src.c_str(), len, utf8_dest.c_str(), flag, progressCallback, lData);
		}
		return GetFileImpl(utf8_src.c_str(), len, reinterpret_cast<const char*>(dest), flag, progressCallback, lData);
	} catch (...) { return SUSIEERROR_INTERNAL; }
}

}  // extern "C"
