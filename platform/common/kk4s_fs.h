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

#if !defined(_WIN32) && !defined(__SWITCH__)
#include <string.h>
#include <strings.h>
// The engine lower-cases ASCII letters in storage names (a Windows habit), so
// on a case-sensitive file system "Override2.tjs" is looked up as
// "override2.tjs". When the exact path does not exist, match each missing
// component case-insensitively against its directory. Unchanged if not found.
inline std::string kk4sResolveCase(const std::string &path) {
	struct stat st;
	if (path.empty() || stat(path.c_str(), &st) == 0) return path;
	std::string out;
	size_t pos = 0;
	if (path[0] == '/') { out = "/"; pos = 1; }
	while (pos < path.size()) {
		size_t end = path.find('/', pos);
		if (end == std::string::npos) end = path.size();
		std::string comp = path.substr(pos, end - pos);
		if (!comp.empty()) {
			std::string cand = out + comp; // out is empty or ends with '/'
			if (stat(cand.c_str(), &st) != 0) {
				DIR *d = opendir(out.empty() ? "." : out.c_str());
				bool found = false;
				if (d) {
					while (struct dirent *e = readdir(d)) {
						if (strcasecmp(e->d_name, comp.c_str()) == 0) {
							cand = out + e->d_name;
							found = true;
							break;
						}
					}
					closedir(d);
				}
				if (!found) return path;
			}
			out = cand;
			if (end < path.size()) out += '/';
		}
		pos = end + 1;
	}
	return out;
}
#else
// Windows and Switch file systems are case-insensitive.
inline std::string kk4sResolveCase(const std::string &path) { return path; }
#endif

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
