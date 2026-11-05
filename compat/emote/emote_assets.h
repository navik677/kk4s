// kk4s E-mote runtime: pixel data of a model's sources.
//
// The bridge between the PSB document (which owns the resource bytes), the
// normalized model (which names them) and the renderer contract (which takes
// TextureDesc + bytes). Only encodings whose layout was checked are handed
// out; anything else is UnsupportedFeature.
#pragma once

#include <cstddef>
#include <cstdint>

#include "emote/emote_model.h"
#include "emote/emote_status.h"
#include "emote/render_contract.h"

namespace kk4s {
namespace emote {

class Document;

struct TexturePixels {
    TextureDesc desc;
    const uint8_t* data = nullptr;  // points into the Document
    size_t size = 0;
};

// Atlas texture of `source` (level 0). RGBA8 with size == width*height*4 is
// accepted ([C] in real files); other formats are UnsupportedFeature.
Status GetSourceTexture(const Document& doc, const SourceDef& source, TexturePixels& out);

// Pixels of a stand-alone icon. Icons in real files are compressed "RL",
// a codec that is not researched: always UnsupportedFeature for now.
Status GetIconPixels(const Document& doc, const IconDef& icon, TexturePixels& out);

}  // namespace emote
}  // namespace kk4s
