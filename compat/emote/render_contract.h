// kk4s E-mote runtime: the contract between the runtime and a renderer.
//
// A backend receives textures and triangle lists; it knows nothing about PSB,
// ciphers, adapters or a game. kk4s composes frames in software and uploads
// the result through SDL2 (desktop and Switch alike), so the first real
// backend is meant to rasterize into a Kirikiri layer bitmap (the target of
// Motion.Player.draw); a GPU backend can implement the same interface.
//
// Not implemented yet: turning a SceneSnapshot into DrawCommands. It needs the
// geometry semantics (coord / angle / mesh control points / transformOrder /
// masks / blend codes), which are not researched; see
// docs/universal_emote_runtime.md ("Renderer").
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kk4s {
namespace emote {

enum class PixelFormat {
    RGBA8,  // 4 bytes per pixel, stored order R,G,B,A ([H] channel order unverified)
};

struct TextureDesc {
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::string debug_name;
};

using TextureHandle = uint32_t;
const TextureHandle kNoTexture = 0;

struct Vertex {
    float x = 0, y = 0;  // target pixels
    float u = 0, v = 0;  // texture coordinates, 0..1
    uint32_t color = 0xFFFFFFFFu;  // multiplied, 0xAARRGGBB
};

enum class BlendMode { Alpha, Additive, Multiply };

struct DrawCommand {
    TextureHandle texture = kNoTexture;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;  // triangles
    BlendMode blend = BlendMode::Alpha;
    float opacity = 1.0f;
};

class RenderBackend {
public:
    virtual ~RenderBackend() {}
    // Returns kNoTexture on failure. `pixels` holds width*height*bpp bytes.
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const uint8_t* pixels, size_t size) = 0;
    virtual void DestroyTexture(TextureHandle t) = 0;
    virtual void BeginFrame(int width, int height) = 0;
    virtual void Submit(const DrawCommand& cmd) = 0;
    virtual void EndFrame() = 0;
};

size_t BytesPerPixel(PixelFormat f);
// Checks a command against the textures a backend created (indices in range,
// triangle count, known texture). Used by backends and tests.
bool ValidateDrawCommand(const DrawCommand& cmd, const std::vector<TextureHandle>& live, std::string* why);

// Records everything it receives; for tests and debugging tools.
class RecordingBackend : public RenderBackend {
public:
    TextureHandle CreateTexture(const TextureDesc& desc, const uint8_t* pixels, size_t size) override;
    void DestroyTexture(TextureHandle t) override;
    void BeginFrame(int width, int height) override;
    void Submit(const DrawCommand& cmd) override;
    void EndFrame() override;

    struct Frame {
        int width = 0, height = 0;
        std::vector<DrawCommand> commands;
        bool ended = false;
    };
    std::vector<TextureHandle> live;
    std::vector<TextureDesc> created;
    std::vector<Frame> frames;
    std::vector<std::string> errors;

private:
    TextureHandle next_ = 1;
};

}  // namespace emote
}  // namespace kk4s
