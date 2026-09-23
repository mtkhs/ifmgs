#ifndef MGS_PS_DETECT_H
#define MGS_PS_DETECT_H

#include <cstddef>
#include <string>

namespace PsDetect {

bool IsPostScript(const char* header, size_t size);  // "%!" magic
bool IsEPIF(const char* header, size_t size);         // EPSI: C5 D0 D3 C6
bool IsPostScriptExtension(const std::string& ext);   // ps / eps / epsf / epsi (case-insensitive)

}  // namespace PsDetect

#endif
