/* A plugin that is not a filter: V2Link asks for an unrelated function and
   never sets one. kk4s must leave it alone. */
#include "tpstub_min.h"

__declspec(dllexport) int __stdcall V2Link(void* exporter) {
    (void)query_narrow(exporter, "void ::TVPAddLog(const ttstr &)");
    return 0;
}
