// kk4s launcher: the game picker, drawn with SDL2 into the engine's window.
// A row of cover cards (Switch home menu style) over a dimmed copy of the
// selected cover. Works with gamepad, touch (tap / swipe), keyboard and mouse.
//
// Covers, first found wins (see docs/sd-layout.md):
//   <data>/covers/<game folder name>.*            (the Y button downloads one from VNDB)
//   <game>/cover.* | folder.* | poster.*         (png, jpg, jpeg, webp)
//   otherwise a generated card with the game's icon (*.ico or the .exe icon).
// Title: <game>/title.txt, <data>/covers/<folder>.txt (UTF-8, first line), else the folder name.
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_net.h"

#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <strings.h>
#include <thread>
#include <sys/stat.h>
#include <vector>

namespace kk4s {
namespace detail {
namespace {

// ---------------------------------------------------------------------------
// images

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool ok() const { return w > 0 && h > 0; }
};

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

SDL_Texture* MakeTexture(SDL_Renderer* r, const Image& img) {
    if (!img.ok()) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, img.rgba.data(), img.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

// Files in dir whose lower-cased name passes pred.
std::vector<std::string> ListFiles(const std::string& dir, bool (*pred)(const std::string&)) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string name = e->d_name;
        if (pred(Lower(name))) out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool IsImageExt(const std::string& ext) { return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp"; }

// ---------------------------------------------------------------------------
// text (FreeType straight into textures; glyphs fall back across faces)

struct Text {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

class Fonts {
public:
    bool Init() {
        if (FT_Init_FreeType(&lib_)) return false;
        FT_Face proportional = nullptr, cyrillic = nullptr, cjk = nullptr;
        for (const std::string& path : SystemFontFiles()) {
            FT_Face f;
            if (FT_New_Face(lib_, path.c_str(), 0, &f)) continue;
            bool used = false;
            bool has_cyr = FT_Get_Char_Index(f, 0x0416) != 0, has_cjk = FT_Get_Char_Index(f, 0x3042) != 0;
            // Proportional Cyrillic, static (variable fonts default to a thin weight).
            if (!proportional && has_cyr && !FT_HAS_MULTIPLE_MASTERS(f) && !FT_Load_Char(f, 0x0430, FT_LOAD_NO_SCALE) &&
                f->glyph->metrics.horiAdvance < f->units_per_EM * 3 / 4) {
                proportional = f; used = true;
            } else if (!cyrillic && has_cyr) {
                cyrillic = f; used = true;
            }
            if (!cjk && has_cjk && !used) { cjk = f; used = true; }
            if (!used) FT_Done_Face(f);
        }
        for (FT_Face f : {proportional, cyrillic, cjk})
            if (f) faces_.push_back(f);
        return !faces_.empty();
    }
    ~Fonts() {
        for (auto& kv : cache_) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
        for (FT_Face f : faces_) FT_Done_Face(f);
        if (lib_) FT_Done_FreeType(lib_);
    }
    // White text (tint with SDL_SetTextureColorMod); cut with "…" past max_w pixels.
    const Text& Get(SDL_Renderer* r, const std::string& utf8, int px, int max_w = 0) {
        char key[32];
        snprintf(key, sizeof(key), "|%d|%d", px, max_w);
        std::string k = utf8 + key;
        auto it = cache_.find(k);
        if (it != cache_.end()) return it->second;
        Text& t = cache_[k];
        Render(r, Decode(utf8), px, max_w, t);
        return t;
    }

private:
    static std::vector<uint32_t> Decode(const std::string& s) {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = s[i];
            int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
            uint32_t cp = len == 1 ? c : c & (0xff >> (len + 1));
            for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3f);
            out.push_back(cp);
            i += len;
        }
        return out;
    }
    FT_Face FaceFor(uint32_t cp) {
        for (FT_Face f : faces_)
            if (FT_Get_Char_Index(f, cp)) return f;
        return faces_[0];
    }
    int Advance(uint32_t cp, int px) {
        FT_Face f = FaceFor(cp);
        FT_Set_Pixel_Sizes(f, 0, px);
        if (FT_Load_Char(f, cp, FT_LOAD_DEFAULT)) return 0;
        return (int)(f->glyph->advance.x >> 6);
    }
    void Render(SDL_Renderer* r, std::vector<uint32_t> cps, int px, int max_w, Text& out) {
        if (faces_.empty() || cps.empty()) return;
        int width = 0;
        for (uint32_t cp : cps) width += Advance(cp, px);
        if (max_w > 0 && width > max_w) {
            int ell = Advance(0x2026, px);
            while (!cps.empty() && width + ell > max_w) { width -= Advance(cps.back(), px); cps.pop_back(); }
            while (!cps.empty() && cps.back() == ' ') { width -= Advance(' ', px); cps.pop_back(); }
            cps.push_back(0x2026);
            width += ell;
        }
        FT_Set_Pixel_Sizes(faces_[0], 0, px);
        int ascent = (int)(faces_[0]->size->metrics.ascender >> 6);
        int height = (int)((faces_[0]->size->metrics.ascender - faces_[0]->size->metrics.descender) >> 6) + 2;
        if (width <= 0 || height <= 0) return;
        std::vector<uint32_t> pix((size_t)width * height, 0x00ffffffu);
        int pen = 0;
        for (uint32_t cp : cps) {
            FT_Face f = FaceFor(cp);
            FT_Set_Pixel_Sizes(f, 0, px);
            if (FT_Load_Char(f, cp, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = f->glyph;
            for (unsigned y = 0; y < g->bitmap.rows; ++y)
                for (unsigned x = 0; x < g->bitmap.width; ++x) {
                    int dx = pen + g->bitmap_left + (int)x, dy = ascent - g->bitmap_top + (int)y;
                    if (dx < 0 || dy < 0 || dx >= width || dy >= height) continue;
                    uint32_t a = g->bitmap.buffer[y * g->bitmap.pitch + x];
                    uint32_t& p = pix[(size_t)dy * width + dx];
                    if (a > (p >> 24)) p = (a << 24) | 0x00ffffffu;
                }
            pen += (int)(g->advance.x >> 6);
        }
        out.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!out.tex) return;
        SDL_UpdateTexture(out.tex, nullptr, pix.data(), width * 4);
        SDL_SetTextureBlendMode(out.tex, SDL_BLENDMODE_BLEND);
        out.w = width;
        out.h = height;
    }

    FT_Library lib_ = nullptr;
    std::vector<FT_Face> faces_;
    std::map<std::string, Text> cache_;
};

// ---------------------------------------------------------------------------
// games

struct Game {
    std::string folder;   // name inside GamesDir()
    std::string title;
    SDL_Texture* cover = nullptr; // full cover art (fills the card)
    SDL_Texture* icon = nullptr;  // small icon for generated cards
    int cover_w = 0, cover_h = 0;
    SDL_Color tint{60, 70, 110, 255};
};

// <data>/covers/<folder>.* first (where the cover search saves), then the game folder.
std::string FindCover(const std::string& dir, const std::string& folder) {
    std::string covers = RootDir() + "/covers";
    std::string want = Lower(folder);
    for (const std::string& f : ListFiles(covers, [](const std::string& n) { return IsImageExt(Ext(n)); }))
        if (Lower(f.substr(0, f.rfind('.'))) == want) return covers + "/" + f;
    static const char* const kNames[] = {"cover", "folder", "poster"};
    for (const char* base : kNames) {
        std::string w = base;
        for (const std::string& f : ListFiles(dir, [](const std::string& n) { return IsImageExt(Ext(n)); }))
            if (Lower(f.substr(0, f.rfind('.'))) == w) return dir + "/" + f;
    }
    return "";
}

std::string FindIcon(const std::string& dir, const std::string& folder) {
    std::vector<std::string> icos = ListFiles(dir, [](const std::string& n) { return Ext(n) == "ico"; });
    if (!icos.empty()) return dir + "/" + icos[0];
    // The game exe: same name as the folder, else the biggest one that is not an uninstaller.
    std::string best;
    long best_size = -1;
    for (const std::string& f : ListFiles(dir, [](const std::string& n) { return Ext(n) == "exe"; })) {
        std::string low = Lower(f);
        if (low.compare(0, 5, "unins") == 0) continue;
        if (low == Lower(folder) + ".exe") return dir + "/" + f;
        struct stat st;
        if (stat((dir + "/" + f).c_str(), &st) == 0 && st.st_size > best_size) { best_size = st.st_size; best = f; }
    }
    return best.empty() ? "" : dir + "/" + best;
}

std::string ReadFirstLine(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    fclose(f);
    char* s = buf;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    s[strcspn(s, "\r\n")] = 0;
    return s;
}

// <game>/title.txt, then <data>/covers/<folder>.txt (saved by the cover search), then the folder name.
std::string ReadTitle(const std::string& dir, const std::string& folder) {
    std::string t = ReadFirstLine(dir + "/title.txt");
    if (t.empty()) t = ReadFirstLine(RootDir() + "/covers/" + folder + ".txt");
    return t.empty() ? folder : t;
}

SDL_Color TintFor(const std::string& name) {
    uint32_t h = 2166136261u;
    for (char c : name) h = (h ^ (unsigned char)c) * 16777619u;
    // Muted colors that read well behind white text.
    static const SDL_Color kTints[] = {{70, 60, 130, 255}, {40, 90, 130, 255}, {120, 50, 80, 255},
                                       {40, 110, 100, 255}, {130, 80, 40, 255}, {80, 50, 120, 255}};
    return kTints[h % (sizeof(kTints) / sizeof(kTints[0]))];
}

void LoadGame(SDL_Renderer* r, Game& g) {
    if (g.cover) SDL_DestroyTexture(g.cover);
    if (g.icon) SDL_DestroyTexture(g.icon);
    g.cover = g.icon = nullptr;
    std::string dir = GamesDir() + "/" + g.folder;
    g.title = ReadTitle(dir, g.folder);
    g.tint = TintFor(g.folder);
    std::string cover = FindCover(dir, g.folder);
    if (!cover.empty()) {
        Image img = Shrink(DecodeFile(cover), 600, 840);
        g.cover = MakeTexture(r, img);
        g.cover_w = img.w;
        g.cover_h = img.h;
        Log("launcher: %s: cover %s%s", g.folder.c_str(), cover.c_str(), g.cover ? "" : " (cannot decode)");
    }
    if (!g.cover) {
        std::string icon = FindIcon(dir, g.folder);
        if (!icon.empty()) {
            g.icon = MakeTexture(r, Shrink(DecodeFile(icon), 256, 256));
            if (g.icon) Log("launcher: %s: icon from %s", g.folder.c_str(), icon.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// drawing helpers (layout in 1280x720 units, scaled to the output)

struct View {
    SDL_Renderer* r;
    float s = 1, ox = 0, oy = 0;
    SDL_FRect Map(float x, float y, float w, float h) const { return {ox + x * s, oy + y * s, w * s, h * s}; }
};

void FillRect(const View& v, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(v.r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(v.r, c.r, c.g, c.b, c.a);
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_RenderFillRectF(v.r, &rc);
}

// Vertical gradient quad.
void Gradient(const View& v, float x, float y, float w, float h, SDL_Color top, SDL_Color bottom) {
    SDL_FRect rc = v.Map(x, y, w, h);
    auto vert = [](float px, float py, SDL_Color c) {
        SDL_Vertex vx;
        vx.position = {px, py};
        vx.color = c;
        vx.tex_coord = {0, 0};
        return vx;
    };
    SDL_Vertex vs[4] = {vert(rc.x, rc.y, top), vert(rc.x + rc.w, rc.y, top), vert(rc.x + rc.w, rc.y + rc.h, bottom),
                        vert(rc.x, rc.y + rc.h, bottom)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(v.r, nullptr, vs, 4, idx, 6);
}

// Text at (x, y) in layout units; align: 0 left, 1 center, 2 right.
void DrawText(const View& v, Fonts& fonts, const std::string& s, float x, float y, float size, SDL_Color c,
              int align = 0, float max_w = 0) {
    const Text& t = fonts.Get(v.r, s, (int)std::lround(size * v.s), (int)(max_w * v.s));
    if (!t.tex) return;
    float w = t.w / v.s, h = t.h / v.s;
    if (align == 1) x -= w / 2;
    if (align == 2) x -= w;
    SDL_SetTextureColorMod(t.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t.tex, c.a);
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_RenderCopyF(v.r, t.tex, nullptr, &rc);
}

float TextWidth(const View& v, Fonts& fonts, const std::string& s, float size) {
    const Text& t = fonts.Get(v.r, s, (int)std::lround(size * v.s));
    return t.w / v.s;
}

// Anti-aliased filled circle, used for button hints.
SDL_Texture* MakeCircle(SDL_Renderer* r) {
    const int n = 96;
    Image img;
    img.w = img.h = n;
    img.rgba.resize(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float d = std::sqrt((x + 0.5f - n / 2.f) * (x + 0.5f - n / 2.f) + (y + 0.5f - n / 2.f) * (y + 0.5f - n / 2.f));
            float a = std::min(1.f, std::max(0.f, n / 2.f - d));
            uint8_t* p = &img.rgba[(y * n + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255);
        }
    return MakeTexture(r, img);
}

// "(A) label" hint; returns its width.
float DrawHint(const View& v, Fonts& fonts, SDL_Texture* circle, float x, float y, const char* key,
               const std::string& label) {
    const float d = 30;
    SDL_FRect rc = v.Map(x, y, d, d);
    SDL_SetTextureColorMod(circle, 235, 238, 245);
    SDL_SetTextureAlphaMod(circle, 255);
    SDL_RenderCopyF(v.r, circle, nullptr, &rc);
    DrawText(v, fonts, key, x + d / 2, y + 2, 20, {20, 24, 36, 255}, 1);
    DrawText(v, fonts, label, x + d + 10, y + 1, 22, {225, 228, 238, 255});
    return d + 10 + TextWidth(v, fonts, label, 22) + 34;
}

void DrawCard(const View& v, Fonts& fonts, const Game& g, float cx, float cy, float w, float h, bool selected,
              float glow) {
    float x = cx - w / 2, y = cy - h / 2;
    // shadow
    FillRect(v, x + 6, y + 10, w, h, {0, 0, 0, 90});
    if (g.cover) {
        // crop the cover to the card shape (cover fill)
        float ar = (float)g.cover_w / g.cover_h, card_ar = w / h;
        SDL_Rect src{0, 0, g.cover_w, g.cover_h};
        if (ar > card_ar) { src.w = (int)(g.cover_h * card_ar); src.x = (g.cover_w - src.w) / 2; }
        else { src.h = (int)(g.cover_w / card_ar); src.y = (g.cover_h - src.h) / 2; }
        SDL_FRect rc = v.Map(x, y, w, h);
        SDL_SetTextureAlphaMod(g.cover, 255);
        SDL_SetTextureColorMod(g.cover, 255, 255, 255);
        SDL_RenderCopyF(v.r, g.cover, &src, &rc);
    } else {
        SDL_Color top = g.tint, bottom{(Uint8)(g.tint.r / 3), (Uint8)(g.tint.g / 3), (Uint8)(g.tint.b / 3), 255};
        Gradient(v, x, y, w, h, top, bottom);
        float icon = w * 0.46f;
        if (g.icon) {
            SDL_FRect rc = v.Map(cx - icon / 2, y + h * 0.2f, icon, icon);
            SDL_RenderCopyF(v.r, g.icon, nullptr, &rc);
        } else {
            // first letter as a big monogram
            std::string first = g.title.substr(0, 1);
            if (!first.empty() && ((unsigned char)first[0] & 0x80)) {
                size_t len = 1;
                while (len < g.title.size() && ((unsigned char)g.title[len] & 0xC0) == 0x80) ++len;
                first = g.title.substr(0, len);
            }
            DrawText(v, fonts, first, cx, y + h * 0.18f, w * 0.42f, {255, 255, 255, 230}, 1);
        }
        // title on the card, wrapped onto two lines at a space if long
        std::string t1 = g.title, t2;
        float size = w * 0.085f;
        if (TextWidth(v, fonts, t1, size) > w - 24) {
            size_t sp = t1.rfind(' ', t1.size() / 2 + 6);
            if (sp != std::string::npos && sp > 0) { t2 = t1.substr(sp + 1); t1 = t1.substr(0, sp); }
        }
        float ty = y + h * 0.72f;
        DrawText(v, fonts, t1, cx, ty, size, {255, 255, 255, 240}, 1, w - 24);
        if (!t2.empty()) DrawText(v, fonts, t2, cx, ty + size * 1.25f, size, {255, 255, 255, 240}, 1, w - 24);
    }
    if (selected) {
        // accent frame, pulsing slightly
        Uint8 a = (Uint8)(200 + 55 * glow);
        SDL_Color c{90, 200, 250, a};
        const float b = 5;
        FillRect(v, x - b - 3, y - b - 3, w + 2 * b + 6, b, c);
        FillRect(v, x - b - 3, y + h + 3, w + 2 * b + 6, b, c);
        FillRect(v, x - b - 3, y - 3, b, h + 6, c);
        FillRect(v, x + w + 3, y - 3, b, h + 6, c);
    } else {
        FillRect(v, x, y, w, h, {10, 12, 20, 70}); // dim the others
    }
}


// Selection border used by the cover search.
void Frame(const View& v, float x, float y, float w, float h, SDL_Color c) {
    const float b = 4;
    FillRect(v, x - b - 2, y - b - 2, w + 2 * b + 4, b, c);
    FillRect(v, x - b - 2, y + h + 2, w + 2 * b + 4, b, c);
    FillRect(v, x - b - 2, y - 2, b, h + 4, c);
    FillRect(v, x + w + 2, y - 2, b, h + 4, c);
}

// ---------------------------------------------------------------------------
// cover search (network work runs on a detached thread; the UI polls the job)

struct SearchJob {
    std::mutex m;
    bool done = false, ok = false;
    std::string err;
    std::vector<net::CoverCandidate> found;
    std::vector<Image> thumbs;
};

struct DownloadJob {
    std::mutex m;
    bool done = false, ok = false;
    std::string err, data;
};

template <class F>
void RunDetached(const char* name, F fn) {
    // Own stack size: TLS + JSON + JPEG decoding need more than the default on Switch.
    auto* heap = new F(std::move(fn));
    SDL_Thread* t = SDL_CreateThreadWithStackSize(
        [](void* p) -> int {
            F* f = static_cast<F*>(p);
            (*f)();
            delete f;
            return 0;
        },
        name, 1u << 20, heap);
    if (t) SDL_DetachThread(t);
    else { (*heap)(); delete heap; }
}

std::shared_ptr<SearchJob> StartSearch(const std::string& query) {
    auto job = std::make_shared<SearchJob>();
    RunDetached("kk4s-cover-search", [job, query] {
        std::vector<net::CoverCandidate> found;
        std::string err;
        bool ok = net::SearchVndbCovers(query, found, err);
        std::vector<Image> thumbs;
        for (const net::CoverCandidate& c : found) {
            std::string data, e;
            Image img;
            if (net::Request(c.thumb_url, nullptr, data, e))
                img = Shrink(DecodeImageData((const uint8_t*)data.data(), data.size()), 300, 420);
            thumbs.push_back(std::move(img));
        }
        std::lock_guard<std::mutex> lock(job->m);
        job->found = std::move(found);
        job->thumbs = std::move(thumbs);
        job->err = err;
        job->ok = ok;
        job->done = true;
    });
    return job;
}

std::shared_ptr<DownloadJob> StartDownload(const std::string& url) {
    auto job = std::make_shared<DownloadJob>();
    RunDetached("kk4s-cover-download", [job, url] {
        std::string data, err;
        bool ok = net::Request(url, nullptr, data, err);
        if (ok && !DecodeImageData((const uint8_t*)data.data(), data.size()).ok()) { ok = false; err = "not an image"; }
        std::lock_guard<std::mutex> lock(job->m);
        job->data = std::move(data);
        job->err = err;
        job->ok = ok;
        job->done = true;
    });
    return job;
}

// Search text from a title / folder name: no [tags] or (notes), no separators.
std::string CleanQuery(const std::string& t) {
    std::string out;
    int depth = 0;
    for (char c : t) {
        if (c == '[' || c == '(') { ++depth; continue; }
        if ((c == ']' || c == ')') && depth > 0) { --depth; continue; }
        if (depth) continue;
        if (c == '_' || c == '-' || c == '.') c = ' ';
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? t : out;
}

bool WriteFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fclose(f) == 0) && ok;
    return ok;
}

// Store the chosen cover as <data>/covers/<folder>.<ext> (+ .txt with the title).
bool SaveCover(const std::string& folder, const std::string& data, const std::string& title) {
    std::string dir = RootDir() + "/covers";
    MakeDir(dir);
    for (const char* ext : {"png", "jpg", "jpeg", "webp"}) remove((dir + "/" + folder + "." + ext).c_str());
    if (!WriteFile(dir + "/" + folder + "." + ImageExt(data), data)) return false;
    if (!title.empty()) WriteFile(dir + "/" + folder + ".txt", title + "\n");
    return true;
}

enum class Action { None, Left, Right, PageLeft, PageRight, Ok, Back, Quit, Search };

struct Input {
    float drag_start_x = 0, drag_last_x = 0, drag_pos = 0;
    bool dragging = false, moved = false;
    int held_dir = 0;
    Uint32 next_repeat = 0;
    unsigned pad_prev = 0;
};

struct HintBox {
    float x, y, w, h;
    Action action;
};

} // namespace

// ---------------------------------------------------------------------------

std::string RunLauncher(SDL_Renderer* r, const std::vector<std::string>& folders, size_t sel) {
    Fonts fonts;
    if (!fonts.Init()) Log("launcher: no usable font, text will be missing");
    std::vector<Game> games(folders.size());
    for (size_t i = 0; i < folders.size(); ++i) {
        games[i].folder = folders[i];
        LoadGame(r, games[i]);
    }
    SDL_Texture* circle = MakeCircle(r);
    std::string last = ReadLastPlayed();

    const float card_w = 210, card_h = 296, step = card_w + 40, center_y = 316;
    float pos = (float)sel; // animated scroll position
    Input in;
    std::string chosen;
    bool running = true;
    Uint32 prev = SDL_GetTicks();
    std::vector<HintBox> hint_boxes;

    // cover search state
    bool searching = false;
    std::shared_ptr<SearchJob> search;
    std::shared_ptr<DownloadJob> download;
    std::vector<net::CoverCandidate> found;
    std::vector<SDL_Texture*> thumbs;
    std::vector<SDL_Point> thumb_size;
    size_t pick = 0;
    float pick_pos = 0;
    std::string search_query, search_msg;
    auto clear_results = [&] {
        for (SDL_Texture* t : thumbs) if (t) SDL_DestroyTexture(t);
        thumbs.clear();
        thumb_size.clear();
        found.clear();
    };
    auto close_search = [&] {
        searching = false;
        search.reset();
        download.reset();
        clear_results();
    };

    auto move = [&](int d) {
        if (searching) {
            if (!found.empty() && !download) pick = (size_t)std::max(0, std::min((int)found.size() - 1, (int)pick + d));
            return;
        }
        if (games.empty()) return;
        sel = (size_t)std::max(0, std::min((int)games.size() - 1, (int)sel + d));
    };
    auto act = [&](Action a) {
        switch (a) {
            case Action::Left: move(-1); break;
            case Action::Right: move(1); break;
            case Action::PageLeft: move(-5); break;
            case Action::PageRight: move(5); break;
            case Action::Ok:
                if (searching) {
                    if (!found.empty() && !download && search && search->done) {
                        download = StartDownload(found[pick].image_url);
                        search_msg.clear();
                    }
                } else if (!games.empty()) {
                    chosen = games[sel].folder;
                    running = false;
                }
                break;
            case Action::Back:
                if (searching) close_search();
                break;
            case Action::Quit:
                if (searching) close_search();
                else running = false;
                break;
            case Action::Search:
                if (!searching && !games.empty()) {
                    searching = true;
                    pick = 0;
                    pick_pos = 0;
                    search_msg.clear();
                    clear_results();
                    search_query = CleanQuery(games[sel].title);
                    Log("launcher: cover search '%s'", search_query.c_str());
                    search = StartSearch(search_query);
                }
                break;
            case Action::None: break;
        }
    };
    auto pad = [&](unsigned bit, bool down) {
        static int logged = 0;
        if (down && logged < 20) { ++logged; Log("launcher: pad 0x%x", bit); }
        if (!down) {
            if (bit == kPadLeft || bit == kPadRight || bit == kPadUp || bit == kPadDown) in.held_dir = 0;
            return;
        }
        switch (bit) {
            case kPadLeft: case kPadUp: act(Action::Left); in.held_dir = -1; in.next_repeat = SDL_GetTicks() + 380; break;
            case kPadRight: case kPadDown: act(Action::Right); in.held_dir = 1; in.next_repeat = SDL_GetTicks() + 380; break;
            case kPadL: act(Action::PageLeft); break;
            case kPadR: act(Action::PageRight); break;
            case kPadA: act(Action::Ok); break;
            case kPadB: act(Action::Back); break;
            case kPadY: act(Action::Search); break;
            case kPadPlus: act(Action::Quit); break;
        }
    };

    while (running) {
        int ow = 1280, oh = 720;
        SDL_GetRendererOutputSize(r, &ow, &oh);
        View v{r};
        v.s = std::min(ow / 1280.f, oh / 720.f);
        v.ox = (ow - 1280 * v.s) / 2;
        v.oy = (oh - 720 * v.s) / 2;
        auto lx = [&](float px) { return (px - v.ox) / v.s; };
        auto ly = [&](float py) { return (py - v.oy) / v.s; };

        // Switch buttons straight from libnx (by label); PC uses SDL events below.
        unsigned held;
        if (ReadPad(held)) {
            unsigned changed = held ^ in.pad_prev;
            in.pad_prev = held;
            for (unsigned bit = 1; changed && bit <= kPadDown; bit <<= 1)
                if (changed & bit) pad(bit, (held & bit) != 0);
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (SDL_GameControllerOpen(e.cdevice.which))
                        Log("input: controller '%s'", SDL_GameControllerNameForIndex(e.cdevice.which));
                    break;
#ifndef __SWITCH__
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                    pad(PadFromSdlButton(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN);
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                        int dir = e.caxis.value < -20000 ? -1 : e.caxis.value > 20000 ? 1 : 0;
                        if (dir != 0 && in.held_dir != dir) { act(dir < 0 ? Action::Left : Action::Right); in.next_repeat = SDL_GetTicks() + 380; }
                        if (dir != 0 || std::abs(e.caxis.value) < 12000) in.held_dir = dir;
                    }
                    break;
#endif
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_LEFT: case SDLK_UP: act(Action::Left); break;
                        case SDLK_RIGHT: case SDLK_DOWN: act(Action::Right); break;
                        case SDLK_PAGEUP: act(Action::PageLeft); break;
                        case SDLK_PAGEDOWN: act(Action::PageRight); break;
                        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: act(Action::Ok); break;
                        case SDLK_BACKSPACE: act(Action::Back); break;
                        case SDLK_ESCAPE: act(Action::Quit); break;
                        case SDLK_y: case SDLK_F3: act(Action::Search); break;
                    }
                    break;
                case SDL_MOUSEWHEEL: act(e.wheel.y > 0 ? Action::Left : e.wheel.y < 0 ? Action::Right : Action::None); break;
                case SDL_FINGERDOWN: case SDL_MOUSEBUTTONDOWN: {
                    if (e.type == SDL_MOUSEBUTTONDOWN && (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT)) break;
                    float x = e.type == SDL_FINGERDOWN ? lx(e.tfinger.x * ow) : lx((float)e.button.x);
                    in.dragging = true;
                    in.moved = false;
                    in.drag_start_x = in.drag_last_x = x;
                    in.drag_pos = searching ? pick_pos : pos;
                    break;
                }
                case SDL_FINGERMOTION: case SDL_MOUSEMOTION: {
                    if (!in.dragging) break;
                    if (e.type == SDL_MOUSEMOTION && e.motion.which == SDL_TOUCH_MOUSEID) break;
                    float x = e.type == SDL_FINGERMOTION ? lx(e.tfinger.x * ow) : lx((float)e.motion.x);
                    in.drag_last_x = x;
                    if (std::fabs(x - in.drag_start_x) > 12) in.moved = true;
                    break;
                }
                case SDL_FINGERUP: case SDL_MOUSEBUTTONUP: {
                    if (!in.dragging) break;
                    if (e.type == SDL_MOUSEBUTTONUP && e.button.which == SDL_TOUCH_MOUSEID) break;
                    in.dragging = false;
                    float x, y;
                    if (e.type == SDL_FINGERUP) { x = lx(e.tfinger.x * ow); y = ly(e.tfinger.y * oh); }
                    else { x = lx((float)e.button.x); y = ly((float)e.button.y); }
                    // footer / panel hints are buttons too
                    bool hit_hint = false;
                    for (const HintBox& h : hint_boxes)
                        if (!in.moved && x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) { act(h.action); hit_hint = true; break; }
                    if (hit_hint) break;
                    if (searching) {
                        const float tstep = 200;
                        if (x < 80 || x > 1200 || y < 90 || y > 630) { if (!in.moved) act(Action::Back); break; }
                        if (found.empty()) break;
                        if (in.moved) {
                            float p = in.drag_pos - (x - in.drag_start_x) / tstep;
                            pick = (size_t)std::max(0, std::min((int)found.size() - 1, (int)std::lround(p)));
                        } else if (std::fabs(y - 340) < 140) {
                            int hit = (int)std::lround(pick_pos + (x - 640) / tstep);
                            if (hit >= 0 && hit < (int)found.size()) {
                                if ((size_t)hit == pick) act(Action::Ok);
                                else pick = (size_t)hit;
                            }
                        }
                        break;
                    }
                    if (games.empty()) break;
                    if (in.moved) {
                        float p = in.drag_pos - (x - in.drag_start_x) / step;
                        sel = (size_t)std::max(0, std::min((int)games.size() - 1, (int)std::lround(p)));
                        pos = std::max(0.f, std::min((float)games.size() - 1, p));
                    } else if (std::fabs(y - center_y) < card_h * 0.6f) {
                        int hit = (int)std::lround(pos + (x - 640) / step);
                        if (hit >= 0 && hit < (int)games.size()) {
                            if ((size_t)hit == sel) act(Action::Ok);
                            else sel = (size_t)hit;
                        }
                    }
                    break;
                }
            }
        }
        Uint32 now = SDL_GetTicks();
        float dt = std::min(0.1f, (now - prev) / 1000.f);
        prev = now;
        if (in.held_dir != 0 && now >= in.next_repeat) { move(in.held_dir); in.next_repeat = now + 120; }
        if (!searching && in.dragging && in.moved)
            pos = std::max(-0.4f, std::min((float)games.size() - 0.6f, in.drag_pos - (in.drag_last_x - in.drag_start_x) / step));
        else
            pos += ((float)sel - pos) * std::min(1.f, dt * 12);
        if (searching && in.dragging && in.moved && !found.empty())
            pick_pos = std::max(-0.4f, std::min((float)found.size() - 0.6f, in.drag_pos - (in.drag_last_x - in.drag_start_x) / 200));
        else
            pick_pos += ((float)pick - pick_pos) * std::min(1.f, dt * 12);
        float glow = 0.5f + 0.5f * std::sin(now / 260.f);

        // background jobs
        if (searching && search && thumbs.empty() && found.empty()) {
            std::lock_guard<std::mutex> lock(search->m);
            if (search->done) {
                found = search->found;
                for (const Image& img : search->thumbs) {
                    thumbs.push_back(MakeTexture(r, img));
                    thumb_size.push_back({img.w, img.h});
                }
                if (!search->ok) search_msg = "Немає з'єднання з VNDB: " + search->err;
                else if (found.empty()) search_msg = "Нічого не знайдено";
                Log("launcher: cover search: %zu result(s)%s%s", found.size(), search->ok ? "" : ", error: ",
                    search->err.c_str());
            }
        }
        if (searching && download) {
            bool done, ok;
            std::string data, err;
            {
                std::lock_guard<std::mutex> lock(download->m);
                done = download->done;
                ok = download->ok;
                if (done) { data.swap(download->data); err = download->err; }
            }
            if (done) {
                download.reset();
                Game& g = games[sel];
                if (ok && SaveCover(g.folder, data, found[pick].title)) {
                    Log("launcher: %s: downloaded cover %s", g.folder.c_str(), found[pick].image_url.c_str());
                    LoadGame(r, g);
                    close_search();
                } else {
                    search_msg = "Не вдалося завантажити: " + (ok ? std::string("не можу записати на SD") : err);
                }
            }
        }

        // background: dark base, the selected cover dimmed over it, gradients for depth
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        FillRect(v, 0, 0, 1280, 720, {14, 17, 27, 255});
        if (!games.empty()) {
            const Game& g = games[sel];
            if (g.cover) {
                float ar = (float)g.cover_w / g.cover_h, w = 1280, h = 1280 / ar;
                if (h < 720) { h = 720; w = 720 * ar; }
                SDL_FRect rc = v.Map(640 - w / 2, 360 - h / 2, w, h);
                SDL_SetTextureAlphaMod(g.cover, 60);
                SDL_RenderCopyF(r, g.cover, nullptr, &rc);
            } else {
                Gradient(v, 0, 0, 1280, 720, {g.tint.r, g.tint.g, g.tint.b, 70}, {0, 0, 0, 0});
            }
        }
        Gradient(v, 0, 0, 1280, 140, {8, 10, 18, 230}, {8, 10, 18, 0});
        Gradient(v, 0, 430, 1280, 290, {8, 10, 18, 0}, {8, 10, 18, 245});

        // header
        DrawText(v, fonts, "kk4s", 48, 30, 34, {90, 200, 250, 255});
        DrawText(v, fonts, "Kirikiri для Switch", 48 + TextWidth(v, fonts, "kk4s", 34) + 14, 42, 20, {170, 176, 195, 255});
        time_t t = time(nullptr);
        struct tm* lt = localtime(&t);
        char clock[16];
        snprintf(clock, sizeof(clock), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
        DrawText(v, fonts, clock, 1232, 34, 26, {225, 228, 238, 255}, 2);

        if (games.empty()) {
            DrawText(v, fonts, "Ігор не знайдено", 640, 250, 40, {240, 242, 248, 255}, 1);
            DrawText(v, fonts, "Скопіюй папку гри Kirikiri (з data.xp3 або startup.tjs) у", 640, 320, 22, {180, 186, 204, 255}, 1);
            DrawText(v, fonts, GamesDir() + "/<НазваГри>/", 640, 356, 22, {90, 200, 250, 255}, 1);
        } else {
            // cards, far ones first so the selected one is drawn on top
            std::vector<int> order;
            for (int i = 0; i < (int)games.size(); ++i)
                if (std::fabs(i - pos) < 6) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return std::fabs(a - pos) > std::fabs(b - pos); });
            for (int i : order) {
                float d = std::fabs(i - pos);
                float k = 1 + 0.16f * std::max(0.f, 1 - d);
                DrawCard(v, fonts, games[i], 640 + (i - pos) * step, center_y, card_w * k, card_h * k,
                         (size_t)i == sel, glow);
            }
            const Game& g = games[sel];
            DrawText(v, fonts, g.title, 640, 520, 36, {245, 247, 252, 255}, 1, 1100);
            std::string sub = std::to_string(sel + 1) + " / " + std::to_string(games.size());
            if (g.folder == last) sub += "   ·   остання гра";
            if (g.title != g.folder) sub += "   ·   " + g.folder;
            DrawText(v, fonts, sub, 640, 570, 20, {160, 168, 190, 255}, 1, 1100);
        }

        // footer hints (also tappable)
        struct Hint { const char* key; const char* label; Action action; };
        auto draw_hints = [&](const std::vector<Hint>& hints, float right, float y) {
            float total = 0;
            for (const Hint& h : hints) total += 30 + 10 + TextWidth(v, fonts, h.label, 22) + 34;
            float hx = right - (total - 34);
            for (const Hint& h : hints) {
                float w = DrawHint(v, fonts, circle, hx, y, h.key, h.label);
                hint_boxes.push_back({hx - 8, y - 10, w - 18, 50, h.action});
                hx += w;
            }
        };
        hint_boxes.clear();
        FillRect(v, 48, 644, 1184, 1, {255, 255, 255, 40});
        if (!searching) {
            std::vector<Hint> hints;
            if (!games.empty()) {
                hints.push_back({"Y", "Обкладинка", Action::Search});
                hints.push_back({"A", "Грати", Action::Ok});
            }
            hints.push_back({"+", "Вихід", Action::Quit});
            draw_hints(hints, 1232, 664);
            if (!games.empty()) DrawText(v, fonts, "<  >   вибір", 48, 666, 22, {160, 168, 190, 255});
        }

        // cover search panel
        if (searching) {
            FillRect(v, 0, 0, 1280, 720, {4, 6, 12, 200});
            FillRect(v, 80, 90, 1120, 540, {22, 26, 40, 250});
            FillRect(v, 80, 90, 1120, 3, {90, 200, 250, 255});
            DrawText(v, fonts, "Обкладинка з інтернету", 112, 110, 30, {245, 247, 252, 255});
            DrawText(v, fonts, "vndb.org", 1168, 120, 18, {140, 148, 170, 255}, 2);
            DrawText(v, fonts, "Пошук: «" + search_query + "»", 112, 154, 20, {160, 168, 190, 255}, 0, 1050);
            bool waiting = search && found.empty() && search_msg.empty();
            if (waiting) {
                std::string dots(1 + (now / 400) % 3, '.');
                DrawText(v, fonts, "Шукаю" + dots, 640, 320, 28, {225, 228, 238, 255}, 1);
            } else if (!found.empty()) {
                const float tw = 170, th = 240, tstep = 200, ty = 340;
                SDL_FRect clipf = v.Map(80, 90, 1120, 540);
                SDL_Rect clip{(int)clipf.x, (int)clipf.y, (int)clipf.w, (int)clipf.h};
                SDL_RenderSetClipRect(r, &clip);
                for (int i = 0; i < (int)found.size(); ++i) {
                    float cx = 640 + (i - pick_pos) * tstep;
                    if (cx < 80 - tw || cx > 1200 + tw) continue;
                    float k = 1 + 0.12f * std::max(0.f, 1 - std::fabs(i - pick_pos));
                    float w = tw * k, h = th * k, x = cx - w / 2, y = ty - h / 2;
                    FillRect(v, x + 5, y + 8, w, h, {0, 0, 0, 90});
                    if (thumbs[i]) {
                        SDL_FRect rc = v.Map(x, y, w, h);
                        float ar = (float)thumb_size[i].x / thumb_size[i].y;
                        if (ar > w / h) { float nh = w / ar; rc = v.Map(x, y + (h - nh) / 2, w, nh); }
                        else { float nw = h * ar; rc = v.Map(x + (w - nw) / 2, y, nw, h); }
                        SDL_SetTextureAlphaMod(thumbs[i], 255);
                        SDL_RenderCopyF(r, thumbs[i], nullptr, &rc);
                    } else {
                        FillRect(v, x, y, w, h, {50, 56, 80, 255});
                    }
                    if ((size_t)i == pick) Frame(v, x, y, w, h, {90, 200, 250, (Uint8)(200 + 55 * glow)});
                }
                SDL_RenderSetClipRect(r, nullptr);
                const net::CoverCandidate& c = found[pick];
                DrawText(v, fonts, c.title, 640, 482, 24, {245, 247, 252, 255}, 1, 1060);
                std::string info = c.alt_title;
                if (!c.released.empty() && c.released != "TBA")
                    info += (info.empty() ? "" : "   ·   ") + c.released.substr(0, 4);
                DrawText(v, fonts, info, 640, 516, 18, {160, 168, 190, 255}, 1, 1060);
                if (download) {
                    std::string dots(1 + (now / 400) % 3, '.');
                    DrawText(v, fonts, "Завантажую" + dots, 112, 584, 20, {90, 200, 250, 255});
                }
            }
            if (!search_msg.empty())
                DrawText(v, fonts, search_msg, 640, found.empty() ? 320 : 552, 22, {255, 170, 150, 255}, 1, 1050);
            std::vector<Hint> hints;
            if (!found.empty()) hints.push_back({"A", "Вибрати", Action::Ok});
            hints.push_back({"B", "Назад", Action::Back});
            draw_hints(hints, 1168, 580);
        }

        // Debugging aid: KK4S_LAUNCHER_SHOT=<file.bmp> saves a frame and quits
        // (screen capture does not work everywhere, e.g. KDE Wayland).
        // KK4S_LAUNCHER_SEARCH=1 opens the cover search first (=pick: and takes the first result).
        static int frames = 0;
        ++frames;
        if (frames == 2 && getenv("KK4S_LAUNCHER_SEARCH")) act(Action::Search);
        if (frames == 600 && getenv("KK4S_LAUNCHER_SEARCH") && strcmp(getenv("KK4S_LAUNCHER_SEARCH"), "pick") == 0)
            act(Action::Ok); // KK4S_LAUNCHER_SEARCH=pick also takes the first result
        if (const char* shot = getenv("KK4S_LAUNCHER_SHOT")) {
            int at = getenv("KK4S_LAUNCHER_SEARCH") ? 900 : 40;
            if (frames == at) {
                SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                if (surf && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
                    SDL_SaveBMP(surf, shot);
                if (surf) SDL_FreeSurface(surf);
                running = false;
            }
        }
        SDL_RenderPresent(r);
    }

    clear_results();
    for (Game& g : games) {
        if (g.cover) SDL_DestroyTexture(g.cover);
        if (g.icon) SDL_DestroyTexture(g.icon);
    }
    if (circle) SDL_DestroyTexture(circle);
    // leave the renderer clean for the engine
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderPresent(r);
    return chosen;
}

} // namespace detail
} // namespace kk4s
