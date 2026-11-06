// kk4s: stand-in for <android/log.h> in the Switch build. rlvm's GLES files
// share their #ifdefs with the Android port, which logs through
// __android_log_print; on the Switch those messages are dropped (some are
// printed per character). Build with -DRLVM_SWITCH_ANDROID_LOG to print them.
#ifndef KK4S_RLVM_ANDROID_LOG_H_
#define KK4S_RLVM_ANDROID_LOG_H_

#include <stdarg.h>
#include <stdio.h>

enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };

static inline int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
#ifdef RLVM_SWITCH_ANDROID_LOG
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[%s] ", tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
#else
  if (prio >= ANDROID_LOG_ERROR) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
  }
#endif
  return 0;
}

#endif
