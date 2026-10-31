// kk4s image loading (see kk4s_image.h).
#include "kk4s_image.h"

#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace kk4s {
namespace img {

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out, size_t max_size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && (size_t)size <= max_size;
    if (ok) {
        out.resize((size_t)size);
        ok = fread(out.data(), 1, out.size(), f) == out.size();
    }
    fclose(f);
    return ok;
}

uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

Image DecodePng(const uint8_t* data, size_t size) {
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&img, data, size)) return {};
    img.format = PNG_FORMAT_RGBA;
    if ((unsigned long long)img.width * img.height > 40000000ull) { png_image_free(&img); return {}; }
    Image out;
    out.rgba.resize(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, out.rgba.data(), 0, nullptr)) {
        png_image_free(&img);
        return {};
    }
    out.w = (int)img.width;
    out.h = (int)img.height;
    return out;
}

Image DecodeJpeg(const uint8_t* data, size_t size) {
    tjhandle h = tjInitDecompress();
    if (!h) return {};
    Image out;
    int w = 0, hh = 0, sub = 0, cs = 0;
    if (tjDecompressHeader3(h, data, (unsigned long)size, &w, &hh, &sub, &cs) == 0 && w > 0 && hh > 0 &&
        (long long)w * hh <= 40000000ll) {
        out.rgba.resize((size_t)w * hh * 4);
        if (tjDecompress2(h, data, (unsigned long)size, out.rgba.data(), w, 0, hh, TJPF_RGBA, 0) == 0) {
            out.w = w;
            out.h = hh;
        } else {
            out.rgba.clear();
        }
    }
    tjDestroy(h);
    return out;
}

Image DecodeWebp(const uint8_t* data, size_t size) {
    int w = 0, h = 0;
    uint8_t* p = WebPDecodeRGBA(data, size, &w, &h);
    if (!p) return {};
    Image out;
    out.w = w;
    out.h = h;
    out.rgba.assign(p, p + (size_t)w * h * 4);
    WebPFree(p);
    return out;
}

// Windows DIB as stored in .ico / exe icon resources (height counts the AND mask too).
Image DecodeDib(const uint8_t* d, size_t size) {
    if (size < 40) return {};
    uint32_t hdr = Rd32(d);
    int w = (int32_t)Rd32(d + 4), h = (int32_t)Rd32(d + 8) / 2;
    int bpp = Rd16(d + 14);
    uint32_t comp = Rd32(d + 16), colors = Rd32(d + 32);
    if (hdr < 40 || (comp != 0 && comp != 3) || w <= 0 || h <= 0 || w > 1024 || h > 1024) return {};
    size_t off = hdr + (comp == 3 && hdr == 40 ? 12 : 0);
    std::vector<uint32_t> pal;
    if (bpp <= 8) {
        size_t n = colors ? colors : (1u << bpp);
        if (n > 256 || off + n * 4 > size) return {};
        for (size_t i = 0; i < n; ++i) pal.push_back(Rd32(d + off + i * 4));
        off += n * 4;
    }
    size_t stride = (size_t)((w * bpp + 31) / 32) * 4, mstride = (size_t)((w + 31) / 32) * 4;
    if (off + stride * h > size) return {};
    const uint8_t* px = d + off;
    const uint8_t* mask = px + stride * h;
    bool has_mask = off + stride * h + mstride * h <= size;
    Image out;
    out.w = w;
    out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    bool any_alpha = false;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = px + stride * (h - 1 - y);
        for (int x = 0; x < w; ++x) {
            uint8_t r, g, b, a = 255;
            uint32_t c;
            switch (bpp) {
                case 32: b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; a = row[x * 4 + 3]; break;
                case 24: b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; break;
                case 8: case 4: case 1: {
                    unsigned idx = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15
                                                                : (row[x / 8] >> (7 - x % 8)) & 1;
                    c = idx < pal.size() ? pal[idx] : 0;
                    b = c & 255; g = (c >> 8) & 255; r = (c >> 16) & 255;
                    break;
                }
                default: return {};
            }
            if (a) any_alpha = true;
            uint8_t* o = &out.rgba[((size_t)y * w + x) * 4];
            o[0] = r; o[1] = g; o[2] = b; o[3] = a;
        }
    }
    if (bpp != 32 || !any_alpha) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                bool transparent = has_mask && ((mask[mstride * (h - 1 - y) + x / 8] >> (7 - x % 8)) & 1);
                out.rgba[((size_t)y * w + x) * 4 + 3] = transparent ? 0 : 255;
            }
    }
    return out;
}

Image DecodeIconImage(const uint8_t* d, size_t size) {
    if (size >= 8 && memcmp(d, "\x89PNG", 4) == 0) return DecodePng(d, size);
    return DecodeDib(d, size);
}

Image DecodeIco(const uint8_t* d, size_t size) {
    if (size < 6 || Rd16(d) != 0 || Rd16(d + 2) != 1) return {};
    int n = Rd16(d + 4), best = -1;
    long best_score = -1;
    for (int i = 0; i < n && 6 + (size_t)i * 16 + 16 <= size; ++i) {
        const uint8_t* e = d + 6 + i * 16;
        uint32_t sz = Rd32(e + 8), off = Rd32(e + 12);
        if ((size_t)off + sz > size) continue;
        long score = (e[0] ? e[0] : 256) * 64L + Rd16(e + 6);
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best < 0) return {};
    const uint8_t* e = d + 6 + best * 16;
    return DecodeIconImage(d + Rd32(e + 12), Rd32(e + 8));
}

// Biggest icon from a Windows .exe (RT_GROUP_ICON -> RT_ICON resources).
Image DecodeExeIcon(const std::vector<uint8_t>& f) {
    const size_t n = f.size();
    auto in = [&](size_t off, size_t len) { return off <= n && len <= n - off; };
    if (!in(0, 0x40) || f[0] != 'M' || f[1] != 'Z') return {};
    uint32_t pe = Rd32(&f[0x3c]);
    if (!in(pe, 24) || memcmp(&f[pe], "PE\0\0", 4) != 0) return {};
    unsigned nsec = Rd16(&f[pe + 6]), optsize = Rd16(&f[pe + 20]);
    size_t opt = pe + 24;
    if (!in(opt, 2)) return {};
    size_t ddir = opt + (Rd16(&f[opt]) == 0x20b ? 112 : 96);
    if (!in(ddir, 24)) return {};
    uint32_t rsrc_rva = Rd32(&f[ddir + 16]);
    size_t sec = opt + optsize;
    auto rva_to_off = [&](uint32_t rva, size_t& out) {
        for (unsigned i = 0; i < nsec; ++i) {
            size_t s = sec + (size_t)i * 40;
            if (!in(s, 40)) return false;
            uint32_t va = Rd32(&f[s + 12]), vsz = Rd32(&f[s + 8]), raw = Rd32(&f[s + 20]), rawsz = Rd32(&f[s + 16]);
            if (rva >= va && rva - va < std::max(vsz, rawsz)) {
                out = (size_t)raw + (rva - va);
                return out < n;
            }
        }
        return false;
    };
    size_t root;
    if (!rsrc_rva || !rva_to_off(rsrc_rva, root)) return {};
    struct Ent { uint32_t id; uint32_t off; bool named; };
    auto entries = [&](uint32_t rel) {
        std::vector<Ent> out;
        size_t p = root + rel;
        if (!in(p, 16)) return out;
        unsigned count = Rd16(&f[p + 12]) + Rd16(&f[p + 14]);
        for (unsigned k = 0; k < count && in(p + 16 + k * 8, 8); ++k) {
            uint32_t name = Rd32(&f[p + 16 + k * 8]), off = Rd32(&f[p + 20 + k * 8]);
            out.push_back({name & 0x7fffffffu, off, (name & 0x80000000u) != 0});
        }
        return out;
    };
    // Follow first subdirectories down to a data entry (language level etc.).
    auto leaf = [&](uint32_t off, const uint8_t*& data, size_t& size) {
        for (int depth = 0; (off & 0x80000000u) && depth < 4; ++depth) {
            std::vector<Ent> es = entries(off & 0x7fffffffu);
            if (es.empty()) return false;
            off = es[0].off;
        }
        size_t p = root + off;
        if ((off & 0x80000000u) || !in(p, 16)) return false;
        size_t at;
        uint32_t rva = Rd32(&f[p]), sz = Rd32(&f[p + 4]);
        if (!rva_to_off(rva, at) || !in(at, sz)) return false;
        data = &f[at];
        size = sz;
        return true;
    };
    uint32_t group_off = 0, icon_dir = 0;
    for (const Ent& t : entries(0)) {
        if (!t.named && t.id == 14 && !group_off) group_off = t.off;
        if (!t.named && t.id == 3) icon_dir = t.off;
    }
    if (!group_off || !(icon_dir & 0x80000000u)) return {};
    const uint8_t* g;
    size_t gsize;
    if (!leaf(group_off, g, gsize) || gsize < 6) return {};
    unsigned count = Rd16(g + 4), best_id = 0;
    long best_score = -1;
    for (unsigned i = 0; i < count && 6 + (size_t)i * 14 + 14 <= gsize; ++i) {
        const uint8_t* e = g + 6 + i * 14;
        long score = (e[0] ? e[0] : 256) * 64L + Rd16(e + 6);
        if (score > best_score) { best_score = score; best_id = Rd16(e + 12); }
    }
    for (const Ent& e : entries(icon_dir & 0x7fffffffu)) {
        if (e.named || e.id != best_id) continue;
        const uint8_t* data;
        size_t size;
        if (leaf(e.off, data, size)) return DecodeIconImage(data, size);
    }
    return {};
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string Ext(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? "" : Lower(name.substr(dot + 1));
}

// Image in memory, format from its first bytes (downloads have no reliable extension).
Image DecodeImageData(const uint8_t* d, size_t n) {
    if (n >= 8 && memcmp(d, "\x89PNG", 4) == 0) return DecodePng(d, n);
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return DecodeJpeg(d, n);
    if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0) return DecodeWebp(d, n);
    return {};
}

const char* ImageExt(const std::string& d) {
    if (d.size() >= 4 && memcmp(d.data(), "\x89PNG", 4) == 0) return "png";
    if (d.size() >= 12 && memcmp(d.data(), "RIFF", 4) == 0) return "webp";
    return "jpg";
}

Image DecodeFile(const std::string& path) {
    std::vector<uint8_t> data;
    std::string ext = Ext(path);
    if (!ReadWholeFile(path, data, ext == "exe" ? (64u << 20) : (32u << 20))) return {};
    if (ext == "png") return DecodePng(data.data(), data.size());
    if (ext == "jpg" || ext == "jpeg") return DecodeJpeg(data.data(), data.size());
    if (ext == "webp") return DecodeWebp(data.data(), data.size());
    if (ext == "ico") return DecodeIco(data.data(), data.size());
    if (ext == "exe") return DecodeExeIcon(data);
    return {};
}

// Box-filter downscale so big covers do not eat texture memory.
Image Shrink(const Image& src, int max_w, int max_h) {
    if (src.w <= max_w && src.h <= max_h) return src;
    double k = std::min((double)max_w / src.w, (double)max_h / src.h);
    Image out;
    out.w = std::max(1, (int)(src.w * k));
    out.h = std::max(1, (int)(src.h * k));
    out.rgba.resize((size_t)out.w * out.h * 4);
    for (int y = 0; y < out.h; ++y) {
        int y0 = y * src.h / out.h, y1 = std::max(y0 + 1, (y + 1) * src.h / out.h);
        for (int x = 0; x < out.w; ++x) {
            int x0 = x * src.w / out.w, x1 = std::max(x0 + 1, (x + 1) * src.w / out.w);
            unsigned sum[4] = {0, 0, 0, 0}, cnt = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const uint8_t* p = &src.rgba[((size_t)yy * src.w + xx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                    ++cnt;
                }
            uint8_t* o = &out.rgba[((size_t)y * out.w + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = (uint8_t)(sum[c] / cnt);
        }
    }
    return out;
}

void BoxBlur(Image& img, int radius, int passes) {
    if (!img.ok() || radius < 1) return;
    std::vector<uint8_t> tmp(img.rgba.size());
    for (int p = 0; p < passes; ++p) {
        for (int dir = 0; dir < 2; ++dir) {
            const int w = img.w, h = img.h;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    unsigned sum[4] = {0, 0, 0, 0}, n = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        int xx = dir == 0 ? std::min(w - 1, std::max(0, x + k)) : x;
                        int yy = dir == 1 ? std::min(h - 1, std::max(0, y + k)) : y;
                        const uint8_t* s = &img.rgba[((size_t)yy * w + xx) * 4];
                        for (int c = 0; c < 4; ++c) sum[c] += s[c];
                        ++n;
                    }
                    uint8_t* d = &tmp[((size_t)y * w + x) * 4];
                    for (int c = 0; c < 4; ++c) d[c] = (uint8_t)(sum[c] / n);
                }
            img.rgba.swap(tmp);
        }
    }
}

} // namespace img
} // namespace kk4s
