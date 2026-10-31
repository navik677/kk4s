// kk4s image loading for the launcher: PNG / JPEG / WebP, Windows .ico and
// the icon inside a game .exe, plus small helpers (downscale, blur).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kk4s {
namespace img {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba; // 8-bit RGBA, rows top to bottom
    bool ok() const { return w > 0 && h > 0; }
};

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out, size_t max_size);

// Format from the first bytes (PNG, JPEG, WebP).
Image DecodeImageData(const uint8_t* data, size_t size);
// "png" / "webp" / "jpg" for a downloaded image (by its first bytes).
const char* ImageExt(const std::string& data);
// By extension: png jpg jpeg webp ico exe (the exe's biggest icon).
Image DecodeFile(const std::string& path);

// Box-filter downscale to fit max_w x max_h (unchanged if already smaller).
Image Shrink(const Image& src, int max_w, int max_h);
// In-place box blur (a few passes look close to a gaussian).
void BoxBlur(Image& img, int radius, int passes);

std::string Lower(std::string s);
std::string Ext(const std::string& name); // lower-case extension without the dot

} // namespace img
} // namespace kk4s
