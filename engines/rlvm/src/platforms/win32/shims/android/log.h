// kk4s: the rlvm GLES code paths (used on the Switch and, through ANGLE, on
// Windows) log through <android/log.h>. This shim keeps the calls but sends
// them to stderr, so rlvm.exe builds without the NDK.
#ifndef KK4S_SHIM_ANDROID_LOG_H_
#define KK4S_SHIM_ANDROID_LOG_H_

#include <cstdarg>
#include <cstdio>

#define ANDROID_LOG_UNKNOWN 0
#define ANDROID_LOG_DEFAULT 1
#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define ANDROID_LOG_FATAL 7
#define ANDROID_LOG_SILENT 8

inline int __android_log_print(int priority, const char* tag, const char* fmt,
                               ...) {
  if (priority < ANDROID_LOG_WARN)
    return 0;
  va_list ap;
  va_start(ap, fmt);
  std::fprintf(stderr, "%s: ", tag ? tag : "rlvm");
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  va_end(ap);
  return 0;
}

inline int __android_log_vprint(int priority, const char* tag, const char* fmt,
                                va_list ap) {
  if (priority < ANDROID_LOG_WARN)
    return 0;
  std::fprintf(stderr, "%s: ", tag ? tag : "rlvm");
  std::vfprintf(stderr, fmt, ap);
  std::fprintf(stderr, "\n");
  return 0;
}

#endif  // KK4S_SHIM_ANDROID_LOG_H_
