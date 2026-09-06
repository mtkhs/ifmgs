#include <bmp.h>
#include <windows.h>
#include <cstdlib>

namespace Bmp {

int CalculateLineBytes(int width, int bitsPerPixel) {
	int bytesPerPixel = (bitsPerPixel + 7) / 8;
	int lineBytes = width * bytesPerPixel;
	return (lineBytes + 3) & ~3;
}

bool GetInfo(const std::vector<uint8_t>& bmpData, int& width, int& height) {
	return GetInfo(bmpData.data(), bmpData.size(), width, height);
}

bool GetInfo(const uint8_t* bmpData, size_t size, int& width, int& height) {
	if (size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) return false;
	if (bmpData[0] != 'B' || bmpData[1] != 'M') return false;

	const BITMAPINFOHEADER* pInfoHdr = reinterpret_cast<const BITMAPINFOHEADER*>(
		bmpData + sizeof(BITMAPFILEHEADER));

	// abs() handles top-down DIBs (negative biHeight).
	width = abs(pInfoHdr->biWidth);
	height = abs(pInfoHdr->biHeight);
	return true;
}

}  // namespace Bmp
