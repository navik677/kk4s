// kk4s E-mote runtime: renderer contract helpers. See render_contract.h.
#include "emote/render_contract.h"

#include <algorithm>

namespace kk4s {
namespace emote {

size_t BytesPerPixel(PixelFormat f) {
    switch (f) {
    case PixelFormat::RGBA8: return 4;
    }
    return 0;
}

bool ValidateDrawCommand(const DrawCommand& cmd, const std::vector<TextureHandle>& live, std::string* why) {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (cmd.texture != kNoTexture && std::find(live.begin(), live.end(), cmd.texture) == live.end())
        return fail("unknown texture handle");
    if (cmd.indices.size() % 3 != 0) return fail("index count is not a multiple of 3");
    for (uint32_t i : cmd.indices)
        if (i >= cmd.vertices.size()) return fail("index out of range");
    if (!(cmd.opacity >= 0.0f && cmd.opacity <= 1.0f)) return fail("opacity outside 0..1");
    return true;
}

TextureHandle RecordingBackend::CreateTexture(const TextureDesc& desc, const uint8_t* pixels, size_t size) {
    size_t need = (size_t)desc.width * (size_t)desc.height * BytesPerPixel(desc.format);
    if (desc.width <= 0 || desc.height <= 0 || !pixels || size != need) {
        errors.push_back("CreateTexture: size mismatch for " + desc.debug_name);
        return kNoTexture;
    }
    TextureHandle h = next_++;
    live.push_back(h);
    created.push_back(desc);
    return h;
}

void RecordingBackend::DestroyTexture(TextureHandle t) {
    auto it = std::find(live.begin(), live.end(), t);
    if (it == live.end())
        errors.push_back("DestroyTexture: unknown handle");
    else
        live.erase(it);
}

void RecordingBackend::BeginFrame(int width, int height) {
    if (!frames.empty() && !frames.back().ended) errors.push_back("BeginFrame: previous frame not ended");
    Frame f;
    f.width = width;
    f.height = height;
    frames.push_back(f);
}

void RecordingBackend::Submit(const DrawCommand& cmd) {
    if (frames.empty() || frames.back().ended) {
        errors.push_back("Submit outside a frame");
        return;
    }
    std::string why;
    if (!ValidateDrawCommand(cmd, live, &why)) errors.push_back("Submit: " + why);
    frames.back().commands.push_back(cmd);
}

void RecordingBackend::EndFrame() {
    if (frames.empty() || frames.back().ended)
        errors.push_back("EndFrame without BeginFrame");
    else
        frames.back().ended = true;
}

}  // namespace emote
}  // namespace kk4s
