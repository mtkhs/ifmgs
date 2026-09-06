#ifndef MGS_BMP_H
#define MGS_BMP_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace Bmp {

// 4-byte aligned scan-line stride (Windows DIB convention).
int CalculateLineBytes(int width, int bitsPerPixel = 24);

// Returns false on too-short input or missing 'BM' signature.
bool GetInfo(const std::vector<uint8_t>& bmpData, int& width, int& height);
bool GetInfo(const uint8_t* bmpData, size_t size, int& width, int& height);

}  // namespace Bmp

#endif
