/* XOR with one byte per file: key = (hash >> 8) ^ 0x5A. Exporter: stdcall,
   QueryFunctionsByNarrowString. */
#include "tpstub_min.h"

static void __stdcall filter(FilterInfo* info) {
    if (info->SizeOfSelf != sizeof(FilterInfo)) return;
    uint8_t* b = (uint8_t*)info->Buffer;
    uint8_t key = (uint8_t)(info->FileHash >> 8) ^ 0x5A;
    for (uint32_t i = 0; i < info->BufferSize; ++i) b[i] ^= key;
}

__declspec(dllexport) int __stdcall V2Link(void* exporter) {
    ((SetFilterFn)query_narrow(exporter, kSetFilter))(filter);
    return 0;
}
__declspec(dllexport) int __stdcall V2Unlink(void) { return 0; }
