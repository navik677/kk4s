#pragma once
#include <string>
#include <functional>
#include "tjs.h"
#include <dirent.h>
#include <sys/stat.h> // kk4s: before the st_*time #undefs below

#ifndef S_IFMT
#define S_IFDIR  0x4000 // Directory
#define S_IFREG  0x8000 // Regular
#endif

struct tTVPLocalFileInfo {
	const char * NativeName;
	unsigned short Mode; // S_IFMT
	tjs_uint64         Size;
	time_t         AccessTime;
	time_t         ModifyTime;
	time_t         CreationTime;
};


#undef st_atime
#undef st_ctime
#undef st_mtime

struct tTVP_stat {
	uint16_t st_mode;
	uint64_t st_size;
	uint64_t st_atime;
	uint64_t st_mtime;
	uint64_t st_ctime;
};

bool TVP_stat(const tjs_char *name, tTVP_stat &s);
bool TVP_stat(const char *name, tTVP_stat &s);

void TVPGetLocalFileListAt(const ttstr &name, const std::function<void(const ttstr&, tTVPLocalFileInfo*)>& cb);

// kk4s: dirent::d_type is missing on MinGW and can be DT_UNKNOWN on some
// file systems; fall back to stat() in those cases.
inline bool kk4sIsRegularFile(const std::string &dir, const struct dirent *e) {
#if defined(DT_REG) && defined(DT_UNKNOWN)
	if (e->d_type != DT_UNKNOWN) return e->d_type == DT_REG;
#endif
	struct stat st;
	std::string full = dir + "/" + e->d_name;
	return stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
