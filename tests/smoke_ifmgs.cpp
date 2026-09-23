// Loads ifmgs.sph and renders an EPS it writes itself.
//
//   smoke_ifmgs <scratch dir>
//
// The EPS is a 72x36 pt red rectangle; at the plugin's default 150 dpi that is
// a 150x75 px DIB. The file lives under a name CP932 cannot represent so the W
// entry points must be doing real wide-path I/O.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <susie.h>

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

static const char kEps[] =
	"%!PS-Adobe-3.0 EPSF-3.0\n"
	"%%BoundingBox: 0 0 72 36\n"
	"1 0 0 setrgbcolor 0 0 72 36 rectfill\n"
	"showpage\n";

static const int kExpectW = 150, kExpectH = 75;  // 72x36 pt at the default 150 dpi

typedef int (__stdcall *GETPICTUREINFOW_T)(LPCWSTR, LONG_PTR, unsigned int, SUSIE_PICTUREINFO*);
typedef int (__stdcall *GETPICTUREW_T)(LPCWSTR, LONG_PTR, unsigned int, HLOCAL*, HLOCAL*, SUSIE_PROGRESS, LONG_PTR);

static bool WriteFileBytes(const std::wstring& path, const void* data, size_t len)
{
	FILE* fp = _wfopen(path.c_str(), L"wb");
	if (!fp) return false;
	size_t n = fwrite(data, 1, len, fp);
	fclose(fp);
	return n == len;
}

int wmain(int argc, wchar_t** argv)
{
	CHECK(argc == 2);
	const std::wstring dir = std::wstring(argv[1]) + L"\\αβγ_テスト";
	CreateDirectoryW(dir.c_str(), nullptr);
	const std::wstring eps = dir + L"\\red.eps";
	CHECK(WriteFileBytes(eps, kEps, sizeof(kEps) - 1));

	HMODULE h = LoadLibraryW(PLUGIN_PATH);
	if (!h) fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
	CHECK(h != nullptr);
	auto getPluginInfo   = reinterpret_cast<GETPLUGININFO>(GetProcAddress(h, "GetPluginInfo"));
	auto getPluginInfoW  = reinterpret_cast<GETPLUGININFOW>(GetProcAddress(h, "GetPluginInfoW"));
	auto isSupportedW    = reinterpret_cast<ISSUPPORTEDW>(GetProcAddress(h, "IsSupportedW"));
	auto getPictureInfoW = reinterpret_cast<GETPICTUREINFOW_T>(GetProcAddress(h, "GetPictureInfoW"));
	auto getPictureW     = reinterpret_cast<GETPICTUREW_T>(GetProcAddress(h, "GetPictureW"));
	CHECK(getPluginInfo && getPluginInfoW && isSupportedW && getPictureInfoW && getPictureW);
	CHECK(GetProcAddress(h, "renderBmp") == nullptr);  // internals stay unexported

	char info[64];
	CHECK(getPluginInfo(0, info, sizeof(info)) == 4 && strcmp(info, "00IN") == 0);
	wchar_t infoW[64];
	CHECK(getPluginInfoW(0, infoW, 0) == 0);  // must not touch buf[-1]

	// dw: head bytes, a HANDLE-like small value, or NULL.
	CHECK(isSupportedW(eps.c_str(), kEps) == 1);
	CHECK(isSupportedW(eps.c_str(), reinterpret_cast<const void*>(static_cast<uintptr_t>(0x4))) == 1);
	CHECK(isSupportedW(eps.c_str(), nullptr) == 1);
	CHECK(isSupportedW(eps.c_str(), "not postscript at all") == 0);
	CHECK(isSupportedW(L"x.png", kEps) == 0);

	SUSIE_PICTUREINFO pi{};
	CHECK(getPictureInfoW(eps.c_str(), 0, SUSIE_SOURCE_DISK, &pi) == SUSIEERROR_NOERROR);
	CHECK(pi.width == kExpectW && pi.height == kExpectH && pi.colorDepth == 24);

	HLOCAL hInfo = nullptr, hBm = nullptr;
	CHECK(getPictureW(eps.c_str(), 0, SUSIE_SOURCE_MEM, &hInfo, &hBm, nullptr, 0) == SUSIEERROR_NOTSUPPORT);
	CHECK(getPictureW(eps.c_str(), 0, SUSIE_SOURCE_DISK, &hInfo, &hBm, nullptr, 0) == SUSIEERROR_NOERROR);
	auto* bmi  = static_cast<BITMAPINFOHEADER*>(LocalLock(hInfo));
	auto* bits = static_cast<unsigned char*>(LocalLock(hBm));
	CHECK(bmi && bits);
	CHECK(bmi->biWidth == kExpectW && bmi->biHeight == kExpectH && bmi->biBitCount == 24);
	const size_t stride = (kExpectW * 3 + 3) & ~3;
	CHECK(bmi->biSizeImage == stride * kExpectH);
	CHECK(LocalSize(hBm) >= stride * kExpectH);
	const unsigned char* centre = bits + (kExpectH / 2) * stride + (kExpectW / 2) * 3;
	CHECK(centre[0] == 0 && centre[1] == 0 && centre[2] == 255);  // BGR red
	LocalUnlock(hInfo);
	LocalUnlock(hBm);
	LocalFree(hInfo);
	LocalFree(hBm);

	CHECK(getPictureW(L"\\\\?\\does-not-exist.eps", 0, SUSIE_SOURCE_DISK, &hInfo, &hBm, nullptr, 0) == SUSIEERROR_UNKNOWNFORMAT);
	CHECK(hInfo == nullptr && hBm == nullptr);

	FreeLibrary(h);
	puts("OK");
	return 0;
}
