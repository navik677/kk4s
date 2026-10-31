// Small file helpers (see kk4s_files.h).
#include "kk4s_files.h"
#include "kk4s_fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kk4s {
namespace files {

namespace {

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
}

template <class F>
void ForEachEntry(const std::string& dir, F fn) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string full = Join(dir, e->d_name);
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        fn(std::string(e->d_name), full, st);
    }
    closedir(d);
}

bool CopyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[64 * 1024];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

} // namespace

bool IsDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

DirStats Stats(const std::string& dir) {
    DirStats s;
    ForEachEntry(dir, [&](const std::string&, const std::string& full, const struct stat& st) {
        if (S_ISDIR(st.st_mode)) {
            DirStats sub = Stats(full);
            s.files += sub.files;
            s.bytes += sub.bytes;
            s.newest = std::max(s.newest, sub.newest);
        } else {
            s.files++;
            s.bytes += (unsigned long long)st.st_size;
            s.newest = std::max(s.newest, (long long)st.st_mtime);
        }
    });
    return s;
}

bool MakeDirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur += path[i];
        if ((path[i] == '/' || i + 1 == path.size()) && cur.size() > 1 && cur.back() != ':' &&
            !(cur.size() >= 2 && cur[cur.size() - 2] == ':' && cur.back() == '/'))
            mkdir(cur.c_str(), 0777); // fails harmlessly when it exists
    }
    return IsDirectory(path);
}

bool CopyDir(const std::string& from, const std::string& to) {
    if (!MakeDirs(to)) return false;
    bool ok = true;
    ForEachEntry(from, [&](const std::string& name, const std::string& full, const struct stat& st) {
        if (S_ISDIR(st.st_mode)) ok = CopyDir(full, Join(to, name)) && ok;
        else ok = CopyFile(full, Join(to, name)) && ok;
    });
    return ok;
}

bool RemoveDir(const std::string& dir) {
    bool ok = true;
    ForEachEntry(dir, [&](const std::string&, const std::string& full, const struct stat& st) {
        if (S_ISDIR(st.st_mode)) ok = RemoveDir(full) && ok;
        else ok = remove(full.c_str()) == 0 && ok;
    });
    return rmdir(dir.c_str()) == 0 && ok;
}

bool MoveDir(const std::string& from, const std::string& to) {
    std::string parent = to.substr(0, to.find_last_of('/', to.size() > 1 ? to.size() - 2 : 0));
    if (!parent.empty()) MakeDirs(parent);
    std::string src = from, dst = to;
    while (src.size() > 1 && src.back() == '/') src.pop_back();
    while (dst.size() > 1 && dst.back() == '/') dst.pop_back();
    if (rename(src.c_str(), dst.c_str()) == 0) return true;
    if (!CopyDir(src, dst)) return false;
    return RemoveDir(src);
}

std::vector<std::string> SubDirs(const std::string& dir) {
    std::vector<std::string> out;
    ForEachEntry(dir, [&](const std::string& name, const std::string&, const struct stat& st) {
        if (S_ISDIR(st.st_mode) && name[0] != '.') out.push_back(name);
    });
    std::sort(out.begin(), out.end());
    return out;
}

std::string Timestamp() {
    time_t t = time(nullptr);
    struct tm* lt = localtime(&t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);
    return buf;
}

} // namespace files
} // namespace kk4s
