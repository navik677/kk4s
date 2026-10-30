// xp3ls - list the files inside a Kirikiri XP3 archive (diagnostics only).
//   g++ -O2 -o xp3ls tools/xp3ls.cpp -lz
//   xp3ls data.xp3 [filter-substring]
// Prints: flags, original size, packed size, adler hash, name (UTF-8).
// Also reports unknown index chunks (often a sign of a custom/encrypted index).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

static uint64_t rd64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

static std::string utf16le_to_utf8(const uint8_t* p, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = rd16(p + i * 2);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            uint32_t d = rd16(p + (i + 1) * 2);
            c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
            ++i;
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: xp3ls <file.xp3> [filter]\n"); return 1; }
    const char* filter = argc > 2 ? argv[2] : nullptr;
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint8_t hdr[11];
    static const uint8_t magic[11] = {'X', 'P', '3', 0x0D, 0x0A, 0x20, 0x0A, 0x1A, 0x8B, 0x67, 0x01};
    if (fread(hdr, 1, 11, f) != 11 || memcmp(hdr, magic, 11) != 0) { fprintf(stderr, "not an XP3 archive\n"); return 1; }
    uint8_t b8[8];
    fread(b8, 1, 8, f);
    uint64_t index_ofs = rd64(b8);
    if (index_ofs == 0x17) { // 2.x header with version block
        uint8_t vb[4 + 1 + 8 + 8];
        fseeko(f, 0x17, SEEK_SET);
        fread(vb, 1, sizeof(vb), f);
        index_ofs = rd64(vb + 13);
        printf("# XP3 v2 (minor %u)\n", rd32(vb));
    }
    std::vector<uint8_t> index;
    for (;;) { // index may be chained (flag bit 0x80 = continue)
        fseeko(f, (off_t)index_ofs, SEEK_SET);
        int flag = fgetc(f);
        if (flag == EOF) { fprintf(stderr, "bad index offset\n"); return 1; }
        if ((flag & 0x7F) == 1) {
            fread(b8, 1, 8, f); uint64_t csize = rd64(b8);
            fread(b8, 1, 8, f); uint64_t osize = rd64(b8);
            std::vector<uint8_t> c(csize);
            fread(c.data(), 1, csize, f);
            std::vector<uint8_t> o(osize);
            uLongf ol = (uLongf)osize;
            if (uncompress(o.data(), &ol, c.data(), (uLong)csize) != Z_OK) { fprintf(stderr, "index zlib error\n"); return 1; }
            index.insert(index.end(), o.begin(), o.begin() + ol);
        } else {
            fread(b8, 1, 8, f); uint64_t size = rd64(b8);
            std::vector<uint8_t> o(size);
            fread(o.data(), 1, size, f);
            index.insert(index.end(), o.begin(), o.end());
        }
        if (!(flag & 0x80)) break;
        fread(b8, 1, 8, f);
        index_ofs = rd64(b8);
    }
    printf("# index %zu bytes\n", index.size());

    size_t pos = 0, files = 0;
    while (pos + 12 <= index.size()) {
        std::string tag((const char*)&index[pos], 4);
        uint64_t size = rd64(&index[pos + 4]);
        const uint8_t* body = &index[pos + 12];
        if (pos + 12 + size > index.size()) { printf("# truncated chunk %s\n", tag.c_str()); break; }
        if (tag == "File") {
            std::string name;
            uint32_t flags = 0, adlr = 0;
            uint64_t osz = 0, psz = 0;
            size_t p = 0;
            while (p + 12 <= size) {
                std::string st((const char*)body + p, 4);
                uint64_t ss = rd64(body + p + 4);
                const uint8_t* sb = body + p + 12;
                if (st == "info") {
                    flags = rd32(sb); osz = rd64(sb + 4); psz = rd64(sb + 12);
                    uint16_t nl = rd16(sb + 20);
                    name = utf16le_to_utf8(sb + 22, nl);
                } else if (st == "adlr") {
                    adlr = rd32(sb);
                }
                p += 12 + ss;
            }
            if (!filter || name.find(filter) != std::string::npos)
                printf("%08x %10llu %10llu %08x %s\n", flags, (unsigned long long)osz, (unsigned long long)psz, adlr, name.c_str());
            ++files;
        } else {
            printf("# chunk '%s' size %llu (not a File entry)\n", tag.c_str(), (unsigned long long)size);
        }
        pos += 12 + size;
    }
    printf("# %zu files\n", files);
    return 0;
}
