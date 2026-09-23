// Loads axmgs.sph and walks a two-page PDF it writes itself.
//
//   smoke_axmgs <scratch dir>
//
// Page 1 is 72x36 pt, page 2 is 36x72 pt with /Rotate 90, so both render to
// 150x75 px at the default 150 dpi; the Rotate path is what makes page 2
// interesting. GetFileInfo's predicted size must equal GetFile's real output.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <susie.h>

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

static const int kExpectW = 150, kExpectH = 75;
static const size_t kExpectBmpSize = 54 + ((kExpectW * 3 + 3) & ~3) * kExpectH;

// Minimal PDF with a correct xref table. countKey is how the page-tree's
// /Count key is spelled; "/C#6Fu#6Et" is the same name with PDF #xx escapes,
// which keeps the literal bytes "/Count" out of the file.
static std::string MakePdf(const std::string& countKey = "/Count")
{
	const std::string objs[] = {
		"<< /Type /Catalog /Pages 2 0 R >>",
		"<< /Type /Pages /Kids [3 0 R 4 0 R] " + countKey + " 2 >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 36] >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 36 72] /Rotate 90 >>",
	};
	std::string pdf = "%PDF-1.4\n";
	std::vector<size_t> offsets;
	for (size_t i = 0; i < 4; i++) {
		offsets.push_back(pdf.size());
		pdf += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
	}
	size_t xref = pdf.size();
	pdf += "xref\n0 5\n0000000000 65535 f \n";
	char line[32];
	for (size_t off : offsets) {
		snprintf(line, sizeof(line), "%010zu 00000 n \n", off);
		pdf += line;
	}
	pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return pdf;
}

typedef int (__stdcall *GETARCHIVEINFOW_T)(LPCWSTR, LONG_PTR, unsigned int, HLOCAL*);
typedef int (__stdcall *GETFILEINFOW_T)(LPCWSTR, LONG_PTR, LPCWSTR, unsigned int, SUSIE_FINFOW*);
typedef int (__stdcall *GETFILEW_T)(LPCWSTR, LONG_PTR, LPWSTR, unsigned int, SUSIE_PROGRESS, LONG_PTR);

static bool WriteFileBytes(const std::wstring& path, const std::string& data)
{
	FILE* fp = _wfopen(path.c_str(), L"wb");
	if (!fp) return false;
	size_t n = fwrite(data.data(), 1, data.size(), fp);
	fclose(fp);
	return n == data.size();
}

int wmain(int argc, wchar_t** argv)
{
	CHECK(argc == 2);
	const std::wstring dir = std::wstring(argv[1]) + L"\\αβγ_テスト";
	CreateDirectoryW(dir.c_str(), nullptr);
	const std::wstring pdf = dir + L"\\two.pdf";
	const std::string pdfBytes = MakePdf();
	CHECK(WriteFileBytes(pdf, pdfBytes));

	HMODULE h = LoadLibraryW(PLUGIN_PATH);
	if (!h) fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
	CHECK(h != nullptr);
	auto getPluginInfo   = reinterpret_cast<GETPLUGININFO>(GetProcAddress(h, "GetPluginInfo"));
	auto isSupportedW    = reinterpret_cast<ISSUPPORTEDW>(GetProcAddress(h, "IsSupportedW"));
	auto getArchiveInfoW = reinterpret_cast<GETARCHIVEINFOW_T>(GetProcAddress(h, "GetArchiveInfoW"));
	auto getFileInfoW    = reinterpret_cast<GETFILEINFOW_T>(GetProcAddress(h, "GetFileInfoW"));
	auto getFileW        = reinterpret_cast<GETFILEW_T>(GetProcAddress(h, "GetFileW"));
	CHECK(getPluginInfo && isSupportedW && getArchiveInfoW && getFileInfoW && getFileW);
	CHECK(GetProcAddress(h, "getPdfPageCount") == nullptr);  // internals stay unexported

	char info[64];
	CHECK(getPluginInfo(0, info, sizeof(info)) == 4 && strcmp(info, "00AX") == 0);

	CHECK(isSupportedW(pdf.c_str(), pdfBytes.data()) == 1);
	CHECK(isSupportedW(pdf.c_str(), reinterpret_cast<const void*>(static_cast<uintptr_t>(0x4))) == 1);
	CHECK(isSupportedW(pdf.c_str(), "not a pdf") == 0);

	// GetArchiveInfo: two entries plus the method[0]=='\0' sentinel.
	HLOCAL hInf = nullptr;
	CHECK(getArchiveInfoW(pdf.c_str(), 0, 0, &hInf) == SUSIEERROR_NOERROR);
	auto* entries = static_cast<SUSIE_FINFO*>(LocalLock(hInf));
	CHECK(entries);
	CHECK(strcmp(entries[0].filename, "page001.bmp") == 0 && entries[0].position == 0);
	CHECK(strcmp(entries[1].filename, "page002.bmp") == 0 && entries[1].position == 1);
	CHECK(entries[2].method[0] == '\0');
	LocalUnlock(hInf);
	LocalFree(hInf);

	// GetFileInfo predicts the size; GetFile must produce exactly that many bytes.
	for (int page = 1; page <= 2; page++) {
		wchar_t name[32];
		swprintf(name, 32, L"page%03d.bmp", page);
		SUSIE_FINFOW fi{};
		CHECK(getFileInfoW(pdf.c_str(), 0, name, 0, &fi) == SUSIEERROR_NOERROR);
		CHECK(fi.filesize == kExpectBmpSize && fi.position == static_cast<ULONG_PTR>(page - 1));
		CHECK(wcscmp(fi.filename, name) == 0);

		HLOCAL hMem = nullptr;
		CHECK(getFileW(pdf.c_str(), page - 1, reinterpret_cast<LPWSTR>(&hMem), SUSIE_DEST_MEM, nullptr, 0) == SUSIEERROR_NOERROR);
		CHECK(hMem && LocalSize(hMem) == kExpectBmpSize);
		auto* bmp = static_cast<const unsigned char*>(LocalLock(hMem));
		CHECK(bmp[0] == 'B' && bmp[1] == 'M');
		const auto* bmi = reinterpret_cast<const BITMAPINFOHEADER*>(bmp + sizeof(BITMAPFILEHEADER));
		CHECK(bmi->biWidth == kExpectW && bmi->biHeight == kExpectH && bmi->biBitCount == 24);
		LocalUnlock(hMem);
		LocalFree(hMem);
	}

	// No literal "/Count" in the bytes (as with object streams in real files):
	// the page count must come from Ghostscript under SAFER instead.
	{
		const std::wstring hiddenPdf = dir + L"\\hidden.pdf";
		const std::string hiddenBytes = MakePdf("/C#6Fu#6Et");
		CHECK(hiddenBytes.find("/Count") == std::string::npos);
		CHECK(WriteFileBytes(hiddenPdf, hiddenBytes));
		HLOCAL hInf2 = nullptr;
		CHECK(getArchiveInfoW(hiddenPdf.c_str(), 0, 0, &hInf2) == SUSIEERROR_NOERROR);
		auto* e = static_cast<SUSIE_FINFO*>(LocalLock(hInf2));
		CHECK(e);
		CHECK(strcmp(e[1].filename, "page002.bmp") == 0 && e[2].method[0] == '\0');
		LocalUnlock(hInf2);
		LocalFree(hInf2);
	}

	// Entry names longer than SUSIE_FINFO::filename are truncated, not overflowed.
	{
		std::wstring longName = L"page1.bmp" + std::wstring(400, L'x');
		SUSIE_FINFOW fi{};
		CHECK(getFileInfoW(pdf.c_str(), 0, longName.c_str(), 0, &fi) == SUSIEERROR_NOERROR);
		CHECK(wcslen(fi.filename) == SUSIE_PATH_MAX - 1);
	}

	CHECK(getFileInfoW(pdf.c_str(), 0, L"cover.bmp", 0, nullptr) == SUSIEERROR_INTERNAL);

	FreeLibrary(h);
	puts("OK");
	return 0;
}
