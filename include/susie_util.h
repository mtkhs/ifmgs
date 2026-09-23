#ifndef MGS_SUSIE_UTIL_H
#define MGS_SUSIE_UTIL_H

#include <cstdint>

namespace SusieUtil {

// IsSupported's dw is either a pointer to the first SUSIE_CHECK_SIZE bytes of
// the file or a Windows HANDLE (small integer) cast to void*. User-mode
// addresses are always above 64 KB, so anything at or below is not a buffer.
inline const uint8_t* ToHeadPtr(const void* dw) {
    return (reinterpret_cast<uintptr_t>(dw) > 0xFFFF)
        ? static_cast<const uint8_t*>(dw)
        : nullptr;
}

}  // namespace SusieUtil

#endif
