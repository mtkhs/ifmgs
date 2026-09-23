#ifndef MGS_GHOSTSCRIPT_RENDER_H
#define MGS_GHOSTSCRIPT_RENDER_H

#include <cstdint>
#include <string>
#include <vector>

// All paths are UTF-8. Ghostscript is loaded from gsdll64.dll at runtime.

// Render one page to a complete BMP file image (file header + DIB header +
// 24bpp bottom-up pixels). PS/EPS input is cropped to its bounding box.
bool renderBmp(int pageNumber, const std::string& inputPath, std::vector<uint8_t>& outputBuffer, bool isPostScriptOrEps = false);

// Page count from the raw PDF bytes; falls back to Ghostscript via pdfPath
// when the byte scan finds nothing. 0 when unknown.
int getPdfPageCount(const std::string& pdfData, const char* pdfPath = nullptr);

// Pixel size Ghostscript would render the page at, from MediaBox and Rotate.
bool getPdfPageDimensions(const std::string& pdfPath, int pageNumber, int& widthPx, int& heightPx);

// Pixel size from the DSC %%BoundingBox / %%HiResBoundingBox comment.
bool getEpsBoundingBox(const std::string& path, int& widthPx, int& heightPx);

#endif
