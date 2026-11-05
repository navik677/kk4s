/* XOR with a table built in V2Link on the Win32 heap: the first 0x80 bytes of
   a file use (hash ^ 0xA5), later bytes table[(offset) & 255] ^ hash. Built
   with KK4S_TPM_CDECL (Kirikiri 2.25+ exporter), asks with QueryFunctions
   (UTF-16 names). */
#include "tpstub_min.h"

__declspec(dllimport) void* __stdcall GetProcessHeap(void);
__declspec(dllimport) void* __stdcall HeapAlloc(void* heap, uint32_t flags, uint32_t size);

static uint8_t* table;

static void __stdcall filter(FilterInfo* info) {
    uint8_t* b = (uint8_t*)info->Buffer;
    uint8_t h = (uint8_t)info->FileHash;
    for (uint32_t i = 0; i < info->BufferSize; ++i) {
        uint64_t o = info->Offset + i;
        b[i] ^= o < 0x80 ? (uint8_t)(h ^ 0xA5) : (uint8_t)(table[o & 255] ^ h);
    }
}

__declspec(dllexport) int __stdcall V2Link(void* exporter) {
    table = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, 256);
    for (int i = 0; i < 256; ++i) table[i] = (uint8_t)(i * 37 + 11);
    ((SetFilterFn)query_wide(exporter, kSetFilter))(filter);
    return 0;
}
