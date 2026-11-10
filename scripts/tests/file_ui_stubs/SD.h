#pragma once
#include <cstdint>
struct SdDiagnosticStub {
 uint64_t totalBytes(){return 8ull*1024*1024*1024;}
 uint64_t usedBytes(){return 2ull*1024*1024*1024;}
};
inline SdDiagnosticStub SD;
