// Small POSIX portability helpers used by the engine (kk4s).
#pragma once
#include <dirent.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>

// dirent::d_type is missing on MinGW and can be DT_UNKNOWN on some file
// systems; fall back to stat() in those cases.
inline bool kk4sIsRegularFile(const std::string &dir, const struct dirent *e) {
#if defined(DT_REG) && defined(DT_UNKNOWN)
	if (e->d_type != DT_UNKNOWN) return e->d_type == DT_REG;
#endif
	struct stat st;
	std::string full = dir + "/" + e->d_name;
	return stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

#ifdef _WIN32
#include <direct.h>
// MinGW mkdir() takes no mode argument.
inline int mkdir(const char *path, int) { return _mkdir(path); }

// MinGW has no realpath(); _fullpath() does the same (malloc'd result when buf is NULL).
inline char *realpath(const char *path, char *resolved) {
	char *r = _fullpath(resolved, path, 4096);
	if (r) for (char *p = r; *p; ++p) if (*p == '\\') *p = '/';
	return r;
}
#endif
