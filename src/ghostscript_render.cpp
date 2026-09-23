#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <path_utils.h>

// Resolved at runtime from gsdll64.dll (no link against the GS import library).
extern "C" {
	typedef int (*gsapi_new_instance_t)(void** pinstance, void* caller_handle);
	typedef int (*gsapi_delete_instance_t)(void* instance);
	typedef int (*gsapi_init_with_args_t)(void* instance, int argc, char** argv);
	typedef int (*gsapi_exit_t)(void* instance);
	typedef int (*gsapi_set_display_callback_t)(void* instance, void* callback);
	typedef int (*gsapi_set_arg_encoding_t)(void* instance, int encoding);
	typedef int (*gsapi_set_stdio_t)(void* instance, int(*stdin_fn)(void*, char*, int), int(*stdout_fn)(void*, const char*, int), int(*stderr_fn)(void*, const char*, int));
	typedef int (*gsapi_run_string_with_length_t)(void* instance, const char* str, int length, int user_errors, int* pexit_code);

	// GS 10.x preferred path: display device queries the callout chain first;
	// only on a < 0 return does it fall back to the legacy callback below.
	// We register both for compatibility.
	typedef int (*gs_callout)(void* instance, void* callout_handle, const char* device_name, int id, int size, void* data);
	typedef int (*gsapi_register_callout_t)(void* instance, gs_callout callout, void* callout_handle);
}

#define DISPLAY_CALLOUT_GET_CALLBACK 0

struct gs_display_get_callback_t {
	struct display_callback* callback;
	void* caller_handle;
};

// Mirror of gdevdsp.h v3 display_callback. GS verifies sizeof, so the layout
// must match the loaded GS build exactly.
#define DISPLAY_VERSION_MAJOR 3
#define DISPLAY_VERSION_MINOR 0

struct display_callback {
	int size;
	int version_major;
	int version_minor;
	int  (*display_open)(void* handle, void* device);
	int  (*display_preclose)(void* handle, void* device);
	int  (*display_close)(void* handle, void* device);
	int  (*display_presize)(void* handle, void* device, int width, int height, int raster, unsigned int format);
	int  (*display_size)(void* handle, void* device, int width, int height, int raster, unsigned int format, unsigned char* pimage);
	int  (*display_sync)(void* handle, void* device);
	int  (*display_page)(void* handle, void* device, int copies, int flush);
	int  (*display_update)(void* handle, void* device, int x, int y, int w, int h);
	void*(*display_memalloc)(void* handle, void* device, size_t size);
	int  (*display_memfree)(void* handle, void* device, void* mem);
	int  (*display_separation)(void* handle, void* device, int component, const char* component_name, unsigned short c, unsigned short m, unsigned short y, unsigned short k);
	int  (*display_adjust_band_height)(void* handle, void* device, int bandheight);
	int  (*display_rectangle_request)(void* handle, void* device, void** memory, int* ox, int* oy, int* raster, int* plane_raster, int* x, int* y, int* w, int* h);
};

// 24-bit BGR, bottom-up, default row alignment.
// COLORS_RGB(0x4) | DEPTH_8(0x800) | LITTLEENDIAN(0x30000) = 0x30804.
// ROW_ALIGN_4 is rejected by display_set_color_format on x64; we keep
// pointer-aligned rows and re-pack to 4-byte stride in dsp_page.
constexpr unsigned int MGS_DISPLAY_FORMAT = 0x30804u;

constexpr int GS_ARG_ENCODING_UTF8 = 1;


namespace {

struct GhostscriptDll {
	HMODULE hModule = nullptr;
	bool ok = false;
	gsapi_new_instance_t gsapi_new_instance = nullptr;
	gsapi_delete_instance_t gsapi_delete_instance = nullptr;
	gsapi_init_with_args_t gsapi_init_with_args = nullptr;
	gsapi_exit_t gsapi_exit = nullptr;
	gsapi_set_display_callback_t gsapi_set_display_callback = nullptr;
	gsapi_set_arg_encoding_t gsapi_set_arg_encoding = nullptr;
	gsapi_register_callout_t gsapi_register_callout = nullptr;
	gsapi_set_stdio_t gsapi_set_stdio = nullptr;
	gsapi_run_string_with_length_t gsapi_run_string_with_length = nullptr;

	GhostscriptDll() {
		// .sph is x64-only; a 32-bit DLL can't load into a 64-bit process.
		hModule = LoadLibraryA("gsdll64.dll");
		if (!hModule) return;
		#define MGS_LOAD_GSAPI(fn) fn = (fn##_t)GetProcAddress(hModule, #fn)
		MGS_LOAD_GSAPI(gsapi_new_instance);
		MGS_LOAD_GSAPI(gsapi_delete_instance);
		MGS_LOAD_GSAPI(gsapi_init_with_args);
		MGS_LOAD_GSAPI(gsapi_exit);
		MGS_LOAD_GSAPI(gsapi_set_display_callback);
		MGS_LOAD_GSAPI(gsapi_set_arg_encoding);
		MGS_LOAD_GSAPI(gsapi_register_callout);
		MGS_LOAD_GSAPI(gsapi_set_stdio);
		MGS_LOAD_GSAPI(gsapi_run_string_with_length);
		#undef MGS_LOAD_GSAPI
		if (gsapi_new_instance && gsapi_delete_instance &&
			gsapi_init_with_args && gsapi_exit &&
			gsapi_set_display_callback && gsapi_set_arg_encoding &&
			gsapi_register_callout &&
			gsapi_set_stdio && gsapi_run_string_with_length) {
			ok = true;
			return;
		}
		FreeLibrary(hModule);
		hModule = nullptr;
	}

	~GhostscriptDll() {
		if (hModule) FreeLibrary(hModule);
	}

	GhostscriptDll(const GhostscriptDll&) = delete;
	GhostscriptDll& operator=(const GhostscriptDll&) = delete;
};

const GhostscriptDll& gsdll() {
	static GhostscriptDll d;  // C++11 thread-safe lazy init
	return d;
}

constexpr int kDefaultDpi = 150;
constexpr int kMinDpi     = 30;
constexpr int kMaxDpi     = 1200;

// Sibling INI path: same dir/basename as this DLL with .ini extension.
bool getOwnIniPathW(wchar_t* out, size_t cap) {
	HMODULE hSelf = nullptr;
	// Address-based lookup so we get our DLL, not the host EXE.
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&getOwnIniPathW), &hSelf)) {
		return false;
	}
	DWORD n = GetModuleFileNameW(hSelf, out, static_cast<DWORD>(cap));
	if (n == 0 || n >= cap) return false;
	wchar_t* dot = wcsrchr(out, L'.');
	if (!dot || static_cast<size_t>(dot - out) + 5 > cap) return false;  // need ".ini\0"
	wcscpy(dot, L".ini");
	return true;
}

// Cached on first call. INI changes require restarting the host.
int getRenderDpi() {
	static int dpi = []{
		wchar_t ini[MAX_PATH];
		if (!getOwnIniPathW(ini, MAX_PATH)) return kDefaultDpi;
		UINT v = GetPrivateProfileIntW(L"render", L"dpi", kDefaultDpi, ini);
		if (v < (UINT)kMinDpi || v > (UINT)kMaxDpi) return kDefaultDpi;
		return static_cast<int>(v);
	}();
	return dpi;
}

}  // namespace


// Largest /Count value in pdfData. The page-tree root /Count holds the total
// page count; being the root, it's also the maximum, so we don't need to
// follow the tree. Returns 0 when /Count is unreachable in the raw bytes
// (e.g. xref compressed in an object stream); caller falls back to GS.
int fastPdfPageCount(const std::string& pdfData) {
	int maxCount = 0;
	size_t pos = 0;
	while ((pos = pdfData.find("/Count", pos)) != std::string::npos) {
		const char* p = pdfData.c_str() + pos + 6;
		// Reject partial matches like "/CountX": next char must terminate the key.
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
			*p == '/' || *p == '>' || *p == ']') {
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '/') p++;
			if (*p >= '0' && *p <= '9') {
				int n = atoi(p);
				if (n > maxCount) maxCount = n;
			}
		}
		pos += 6;
	}
	return maxCount;
}



// Fallback for PDFs whose page count can't be derived structurally (compressed
// xref, etc.). Spawns GS in -dNODISPLAY mode and reads `pdfpagecount` from stdout.
static int gsPageCountFromPath(const char* pdfPath) {
	const auto& gs = gsdll();
	if (!gs.ok) return 0;

	void* inst = nullptr;
	if (gs.gsapi_new_instance(&inst, nullptr) != 0) return 0;

	// Susie hosts call us single-threaded, so a function-local static suffices.
	static std::string captured;
	captured.clear();
	auto stdoutFn = [](void*, const char* s, int n) -> int {
		captured.append(s, static_cast<size_t>(n));
		return n;
	};
	auto stderrFn = [](void*, const char*, int n) -> int { return n; };

	int pageCount = 0;
	gs.gsapi_set_arg_encoding(inst, GS_ARG_ENCODING_UTF8);
	if (gs.gsapi_set_stdio(inst, nullptr, stdoutFn, stderrFn) == 0) {
		std::string pathFwd(pdfPath);
		std::replace(pathFwd.begin(), pathFwd.end(), '\\', '/');  // PS `file` wants forward slashes
		// SAFER blocks `file` on anything not explicitly permitted.
		std::string permit = "--permit-file-read=" + pathFwd;
		char* args[] = {
			(char*)"gs",
			(char*)"-dSAFER",
			(char*)permit.c_str(),
			(char*)"-dNODISPLAY",
			(char*)"-dBATCH",
			(char*)"-dNOPAUSE",
			(char*)"-dQUIET"
		};
		if (gs.gsapi_init_with_args(inst, 7, args) == 0) {
			// `flush` is required: output is otherwise dropped on `quit`.
			std::string psCmd = "(" + pathFwd + ") (r) file runpdfbegin pdfpagecount = flush quit";
			int exitCode = 0;
			int rc = gs.gsapi_run_string_with_length(inst, psCmd.c_str(), (int)psCmd.length(), 0, &exitCode);
			// -101 == gs_error_Quit (normal exit via `quit`).
			if (rc == 0 || rc == -101) {
				for (size_t i = 0; i < captured.size(); ++i) {
					if (isdigit(static_cast<unsigned char>(captured[i]))) {
						int num = atoi(captured.c_str() + i);
						if (num > 0 && num < 1000000) {
							pageCount = num;
							break;
						}
					}
				}
			}
			gs.gsapi_exit(inst);
		}
	}

	gs.gsapi_delete_instance(inst);
	return pageCount;
}

int getPdfPageCount(const std::string& pdfData, const char* pdfPath) {
	int n = fastPdfPageCount(pdfData);
	if (n > 0) return n;
	if (pdfPath) return gsPageCountFromPath(pdfPath);
	return 0;
}


// MediaBox + Rotate -> pixel dimensions GS would render at. Doesn't decode pixels.
bool getPdfPageDimensions(const std::string& pdfPath, int pageNumber, int& widthPx, int& heightPx) {
	const auto& gs = gsdll();
	if (!gs.ok) return false;

	void* inst = nullptr;
	if (gs.gsapi_new_instance(&inst, nullptr) != 0) return false;

	static std::string captured;
	captured.clear();
	auto stdoutFn = [](void*, const char* s, int n) -> int {
		captured.append(s, static_cast<size_t>(n));
		return n;
	};
	auto stderrFn = [](void*, const char*, int n) -> int { return n; };

	bool ok = false;
	gs.gsapi_set_arg_encoding(inst, GS_ARG_ENCODING_UTF8);
	if (gs.gsapi_set_stdio(inst, nullptr, stdoutFn, stderrFn) == 0) {
		std::string pathFwd(pdfPath);
		std::replace(pathFwd.begin(), pathFwd.end(), '\\', '/');
		std::string permit = "--permit-file-read=" + pathFwd;
		char* args[] = {
			(char*)"gs",
			(char*)"-dSAFER",
			(char*)permit.c_str(),
			(char*)"-dNODISPLAY",
			(char*)"-dBATCH",
			(char*)"-dNOPAUSE",
			(char*)"-dQUIET"
		};
		if (gs.gsapi_init_with_args(inst, 7, args) == 0) {
			// Output (one per line): rotate, mb_llx, mb_lly, mb_urx, mb_ury.
			// /Rotate defaults to 0 when absent (PDF spec).
			std::string psCmd = "(" + pathFwd + ") (r) file runpdfbegin "
				+ std::to_string(pageNumber) + " pdfgetpage "
				"dup /Rotate known { dup /Rotate get } { 0 } ifelse "
				"exch /MediaBox get exch = { = } forall flush quit";
			int exitCode = 0;
			int rc = gs.gsapi_run_string_with_length(inst, psCmd.c_str(), (int)psCmd.length(), 0, &exitCode);
			if (rc == 0 || rc == -101) {
				double nums[5] = {0, 0, 0, 0, 0};
				int idx = 0;
				const char* p = captured.c_str();
				while (idx < 5 && *p) {
					while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
					if (!*p) break;
					char* end;
					nums[idx] = strtod(p, &end);
					if (end == p) break;
					p = end;
					idx++;
				}
				if (idx == 5) {
					double w_pt = nums[3] - nums[1];
					double h_pt = nums[4] - nums[2];
					int rotate = (int)nums[0] % 360;
					if (rotate < 0) rotate += 360;
					// 90/270 swap width and height in the rendered output.
					if (rotate == 90 || rotate == 270) std::swap(w_pt, h_pt);
					if (w_pt > 0 && h_pt > 0) {
						int dpi = getRenderDpi();
						// GS uses round-half-up for px conversion (verified empirically).
						widthPx = (int)(w_pt * dpi / 72.0 + 0.5);
						heightPx = (int)(h_pt * dpi / 72.0 + 0.5);
						ok = true;
					}
				}
			}
			gs.gsapi_exit(inst);
		}
	}

	gs.gsapi_delete_instance(inst);
	return ok;
}


// Find `needle` in buf and parse the four numbers that follow into box[0..3].
static bool findBoundingBox(const char* buf, size_t n, size_t scanStart, const char* needle, double box[4]) {
	size_t needleLen = strlen(needle);
	for (size_t pos = scanStart; pos + needleLen < n; ++pos) {
		if (memcmp(buf + pos, needle, needleLen) != 0) continue;

		const char* p = buf + pos + needleLen;
		const char* end = buf + n;
		int idx = 0;
		while (idx < 4 && p < end) {
			while (p < end && (*p == ' ' || *p == '\t')) ++p;
			if (p >= end || *p == '\n' || *p == '\r') break;
			char* tail;
			box[idx] = strtod(p, &tail);
			if (tail == p) return false;  // e.g. "%%BoundingBox: (atend)"
			p = tail;
			++idx;
		}
		return idx == 4;
	}
	return false;
}

// PS/EPS dimensions from DSC comments. GS prefers %%HiResBoundingBox: over
// %%BoundingBox:, so we do the same. EPSI files (magic C5 D0 D3 C6) start
// with a binary header; scanning begins at the embedded PS section offset.
bool getEpsBoundingBox(const std::string& path, int& widthPx, int& heightPx) {
	std::wstring widePath = Path::Utf8ToWide(path);
	if (widePath.empty()) return false;
	FILE* fp = _wfopen(widePath.c_str(), L"rb");
	if (!fp) return false;

	constexpr size_t MAX_SCAN = 65536;  // DSC headers live in the first few KB
	std::vector<char> buf(MAX_SCAN);
	size_t n = fread(buf.data(), 1, MAX_SCAN, fp);
	fclose(fp);
	if (n < 16) return false;

	size_t scanStart = 0;
	if ((unsigned char)buf[0] == 0xC5 && (unsigned char)buf[1] == 0xD0 &&
		(unsigned char)buf[2] == 0xD3 && (unsigned char)buf[3] == 0xC6) {
		// EPSI: bytes 4-7 (LE) hold the PS section's byte offset.
		uint32_t psOffset = (uint8_t)buf[4] | ((uint8_t)buf[5] << 8) |
							((uint8_t)buf[6] << 16) | ((uint8_t)buf[7] << 24);
		scanStart = psOffset;
		if (scanStart >= n) return false;
	}

	double box[4];
	if (!findBoundingBox(buf.data(), n, scanStart, "%%HiResBoundingBox:", box) &&
		!findBoundingBox(buf.data(), n, scanStart, "%%BoundingBox:", box)) {
		return false;
	}

	double w_pt = box[2] - box[0];
	double h_pt = box[3] - box[1];
	if (w_pt <= 0 || h_pt <= 0) return false;

	int dpi = getRenderDpi();
	widthPx = (int)(w_pt * dpi / 72.0 + 0.5);
	heightPx = (int)(h_pt * dpi / 72.0 + 0.5);
	return true;
}


namespace {
struct DisplayCtx {
	int width = 0;
	int height = 0;
	int raster = 0;            // bytes per row in GS's internal layout
	unsigned int format = 0;
	unsigned char* pimage = nullptr;
	bool pageReceived = false;
	std::vector<uint8_t>* outBmp = nullptr;
};

extern "C" {

// open / preclose / close / sync need no work, but a NULL pointer crashes GS.
static int dsp_open(void*, void*)     { return 0; }
static int dsp_preclose(void*, void*) { return 0; }
static int dsp_close(void*, void*)    { return 0; }
static int dsp_sync(void*, void*)     { return 0; }

static int dsp_presize(void* h, void*, int w, int hgt, int r, unsigned int f) {
	if (auto* ctx = static_cast<DisplayCtx*>(h)) {
		ctx->width = w;
		ctx->height = hgt;
		ctx->raster = r;
		ctx->format = f;
		ctx->pimage = nullptr;
	}
	return 0;
}

static int dsp_size(void* h, void*, int w, int hgt, int r, unsigned int f, unsigned char* pimage) {
	if (auto* ctx = static_cast<DisplayCtx*>(h)) {
		ctx->width = w;
		ctx->height = hgt;
		ctx->raster = r;
		ctx->format = f;
		ctx->pimage = pimage;
	}
	return 0;
}

// Wrap GS's framebuffer in BMP headers and re-pack rows from GS's
// pointer-aligned stride to BMP's 4-byte stride.
static int dsp_page(void* h, void*, int /*copies*/, int /*flush*/) {
	auto* ctx = static_cast<DisplayCtx*>(h);
	if (!ctx || !ctx->outBmp || !ctx->pimage ||
		ctx->width <= 0 || ctx->height <= 0 || ctx->raster <= 0) {
		return 0;
	}

	constexpr size_t fileHeader = 14;
	constexpr size_t dibHeader  = 40;
	constexpr size_t dataOffset = fileHeader + dibHeader;

	const int srcStride = ctx->raster;
	const int dstStride = ((ctx->width * 3) + 3) & ~3;
	const size_t dataSize = static_cast<size_t>(dstStride) * static_cast<size_t>(ctx->height);
	const size_t total    = dataOffset + dataSize;

	// Catch bad_alloc: C++ exceptions across the extern "C" boundary are UB.
	try {
		ctx->outBmp->resize(total);
	} catch (...) {
		return 0;
	}
	uint8_t* p = ctx->outBmp->data();

	// Little-endian byte writes (no struct-packing assumptions).
	auto put16 = [](uint8_t* dst, uint16_t v) {
		dst[0] = static_cast<uint8_t>(v);
		dst[1] = static_cast<uint8_t>(v >> 8);
	};
	auto put32 = [](uint8_t* dst, uint32_t v) {
		dst[0] = static_cast<uint8_t>(v);
		dst[1] = static_cast<uint8_t>(v >> 8);
		dst[2] = static_cast<uint8_t>(v >> 16);
		dst[3] = static_cast<uint8_t>(v >> 24);
	};

	// BITMAPFILEHEADER
	p[0] = 'B';
	p[1] = 'M';
	put32(p + 2,  static_cast<uint32_t>(total));
	put16(p + 6,  0);
	put16(p + 8,  0);
	put32(p + 10, static_cast<uint32_t>(dataOffset));

	// BITMAPINFOHEADER. Positive biHeight = bottom-up (matches BOTTOMFIRST).
	uint8_t* bi = p + fileHeader;
	put32(bi + 0,  static_cast<uint32_t>(dibHeader));
	put32(bi + 4,  static_cast<uint32_t>(ctx->width));
	put32(bi + 8,  static_cast<uint32_t>(ctx->height));
	put16(bi + 12, 1);
	put16(bi + 14, 24);
	put32(bi + 16, 0);
	put32(bi + 20, static_cast<uint32_t>(dataSize));
	put32(bi + 24, 0);
	put32(bi + 28, 0);
	put32(bi + 32, 0);
	put32(bi + 36, 0);

	uint8_t* dstRow = p + dataOffset;
	const unsigned char* srcRow = ctx->pimage;
	// resize() zero-filled the buffer, so a src stride shorter than dst leaves
	// the padding bytes at 0 instead of reading past the row.
	const size_t copyBytes = static_cast<size_t>(std::min(srcStride, dstStride));
	for (int y = 0; y < ctx->height; ++y) {
		memcpy(dstRow, srcRow, copyBytes);
		dstRow += dstStride;
		srcRow += srcStride;
	}

	ctx->pageReceived = true;
	return 0;
}

}  // extern "C"

// File-scope so the function pointers have a stable address (GS keeps the
// pointer it was handed). Non-const because gs_display_get_callback_t::callback
// is a non-const pointer.
static display_callback g_displayCallback = {
	sizeof(display_callback),
	DISPLAY_VERSION_MAJOR,
	DISPLAY_VERSION_MINOR,
	&dsp_open,
	&dsp_preclose,
	&dsp_close,
	&dsp_presize,
	&dsp_size,
	&dsp_sync,
	&dsp_page,
	nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

extern "C" {
static int mgsDisplayCallout(void* /*instance*/, void* callout_handle, const char* device_name, int id, int size, void* data) {
	if (!device_name || strcmp(device_name, "display") != 0) return -1;
	if (id != DISPLAY_CALLOUT_GET_CALLBACK) return -1;
	if (!data || size < (int)sizeof(gs_display_get_callback_t)) return -1;
	auto* cb = static_cast<gs_display_get_callback_t*>(data);
	cb->callback = &g_displayCallback;
	cb->caller_handle = callout_handle;
	return 0;
}
}  // extern "C"

}  // namespace

// Render one page via GS. inputPath is UTF-8 (passed as such via gsapi_set_arg_encoding).
// PS/EPS gets -dEPSCrop and -sAutoRotatePages=None; PDF gets stock GS behavior.
bool renderWithGhostscript(int dpi, int pageNumber, const std::string& inputPath, std::vector<uint8_t>& outputBuffer, bool isPostScriptOrEps) {

	const auto& gs = gsdll();
	if (!gs.ok) {
		return false;
	}

	DisplayCtx dctx;
	dctx.outBmp = &outputBuffer;

	void* gsInstance = NULL;
	if (gs.gsapi_new_instance(&gsInstance, &dctx) != 0) {
		return false;
	}

	gs.gsapi_set_arg_encoding(gsInstance, GS_ARG_ENCODING_UTF8);

	if (gs.gsapi_register_callout(gsInstance, &mgsDisplayCallout, &dctx) != 0) {
		gs.gsapi_delete_instance(gsInstance);
		return false;
	}
	gs.gsapi_set_display_callback(gsInstance, (void*)&g_displayCallback);

	char dpiStr[32];
	char firstPageStr[32];
	char lastPageStr[32];
	char dispFmtStr[64];
	snprintf(dpiStr, sizeof(dpiStr), "-r%d", dpi);
	snprintf(firstPageStr, sizeof(firstPageStr), "-dFirstPage=%d", pageNumber);
	snprintf(lastPageStr, sizeof(lastPageStr), "-dLastPage=%d", pageNumber);
	snprintf(dispFmtStr, sizeof(dispFmtStr), "-dDisplayFormat=%u", MGS_DISPLAY_FORMAT);

	// SAFER: the input file named on the command line is readable; nothing else is.
	std::vector<char*> args = {
		(char*)"gs",
		(char*)"-dBATCH",
		(char*)"-dNOPAUSE",
		(char*)"-dQUIET",
		(char*)"-dSAFER",
		(char*)"-dTextAlphaBits=4",
		(char*)"-dGraphicsAlphaBits=4",
		(char*)"-dAlignToPixels=0",
		(char*)"-dGridFitTT=2",
		firstPageStr,
		lastPageStr,
		(char*)"-sDEVICE=display",
		dispFmtStr,
		dpiStr,
	};
	if (isPostScriptOrEps) {
		args.push_back((char*)"-dEPSCrop");
		args.push_back((char*)"-sAutoRotatePages=None");
	}
	args.push_back((char*)inputPath.c_str());

	bool success = false;
	int result = gs.gsapi_init_with_args(gsInstance, (int)args.size(), args.data());
	if (result == 0) {
		gs.gsapi_exit(gsInstance);
		success = dctx.pageReceived && !outputBuffer.empty();
	}

	gs.gsapi_delete_instance(gsInstance);
	return success;
}


// Plugin-facing wrapper. < 54 bytes (file + DIB headers) is a failure.
bool renderBmp(int pageNumber, const std::string& inputPath, std::vector<uint8_t>& outputBuffer, bool isPostScriptOrEps) {
	if (!renderWithGhostscript(getRenderDpi(), pageNumber, inputPath, outputBuffer, isPostScriptOrEps) ||
		outputBuffer.size() < 54) {
		outputBuffer.clear();
		return false;
	}
	return true;
}
