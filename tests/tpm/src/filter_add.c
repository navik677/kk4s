/* Not a XOR: adds (hash ^ position). Runs, but only through the interpreter. */
#include "tpstub_min.h"

static void __stdcall filter(FilterInfo* info) {
    uint8_t* b = (uint8_t*)info->Buffer;
    for (uint32_t i = 0; i < info->BufferSize; ++i)
        b[i] = (uint8_t)(b[i] + (uint8_t)(info->FileHash ^ (uint32_t)(info->Offset + i)));
}

__declspec(dllexport) int __stdcall V2Link(void* exporter) {
    ((SetFilterFn)query_narrow(exporter, kSetFilter))(filter);
    return 0;
}
