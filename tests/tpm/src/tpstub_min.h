/* The few tp_stub pieces a Kirikiri 2 XP3 filter plugin uses, written for the
   tests from the documented ABI (no Kirikiri code): V2Link(exporter) is
   __stdcall; iTVPFunctionExporter's methods are TJS_INTF_METHOD (__stdcall up
   to Kirikiri 2.24, __cdecl from 2.25); imported functions are __stdcall; the
   filter callback is __stdcall and gets tTVPXP3ExtractionFilterInfo (pack 4). */
#include <stdint.h>

#pragma pack(push, 4)
typedef struct {
    uint32_t SizeOfSelf;
    uint64_t Offset;
    void* Buffer;
    uint32_t BufferSize;
    uint32_t FileHash;
} FilterInfo;
#pragma pack(pop)

typedef void(__stdcall* FilterFn)(FilterInfo*);
typedef void(__stdcall* SetFilterFn)(FilterFn);

#ifdef KK4S_TPM_CDECL
#define INTF __cdecl
#else
#define INTF __stdcall
#endif
typedef int(INTF* QueryNarrowFn)(void* self, const char** names, void** funcs, uint32_t count);
typedef int(INTF* QueryWideFn)(void* self, const uint16_t** names, void** funcs, uint32_t count);

static const char kSetFilter[] = "void ::TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter)";

static void* query_narrow(void* exporter, const char* name) {
    void** vtbl = *(void***)exporter;
    void* f = 0;
    ((QueryNarrowFn)vtbl[1])(exporter, &name, &f, 1);
    return f;
}

static void* query_wide(void* exporter, const char* name) {
    uint16_t w[128];
    int i = 0;
    for (; name[i] && i < 127; ++i) w[i] = (uint8_t)name[i];
    w[i] = 0;
    const uint16_t* p = w;
    void** vtbl = *(void***)exporter;
    void* f = 0;
    ((QueryWideFn)vtbl[0])(exporter, &p, &f, 1);
    return f;
}

int __stdcall DllMain(void* h, uint32_t reason, void* p) { (void)h; (void)reason; (void)p; return 1; }
