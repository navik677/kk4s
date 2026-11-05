// kk4s E-mote runtime: pixel data of a model's sources. See emote_assets.h.
#include "emote/emote_assets.h"

#include "emote/psb_document.h"

#include <cstdio>

namespace kk4s {
namespace emote {

Status GetSourceTexture(const Document& doc, const SourceDef& source, TexturePixels& out) {
    out = TexturePixels();
    if (!source.has_texture) return Status::Fail(Error::NotFound, "source " + source.name + " has no texture");
    const TextureDef& t = source.texture;
    if (t.format != "RGBA8")
        return Status::Fail(Error::UnsupportedFeature, "texture format '" + t.format + "' is not researched");
    ResourceSpan span;
    if (t.pixel_resource < 0 || !doc.Resource((uint32_t)t.pixel_resource, span))
        return Status::Fail(Error::CorruptStructure, "texture of " + source.name + " has no pixel resource");
    size_t need = (size_t)t.width * (size_t)t.height * 4;
    if (t.width <= 0 || t.height <= 0 || span.size != need) {
        char m[160];
        std::snprintf(m, sizeof m, "texture %dx%d RGBA8 needs %zu bytes, resource has %zu", t.width, t.height, need,
                      span.size);
        return Status::Fail(Error::CorruptStructure, m);
    }
    out.desc.width = t.width;
    out.desc.height = t.height;
    out.desc.format = PixelFormat::RGBA8;
    out.desc.debug_name = source.name;
    out.data = span.data;
    out.size = span.size;
    return Status::Ok();
}

Status GetIconPixels(const Document& doc, const IconDef& icon, TexturePixels& out) {
    (void)doc;
    out = TexturePixels();
    if (icon.in_atlas)
        return Status::Fail(Error::InvalidArgument, "icon " + icon.name + " lives in an atlas; use GetSourceTexture");
    return Status::Fail(Error::UnsupportedFeature,
                        "icon pixel codec '" + icon.compress + "' is not researched (icon " + icon.name + ")");
}

}  // namespace emote
}  // namespace kk4s
