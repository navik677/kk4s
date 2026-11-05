// kk4s E-mote runtime: one-call entry point. See emote.h.
#include "emote/emote.h"

namespace kk4s {
namespace emote {

Status LoadCharacter(const uint8_t* data, size_t size, const LoadOptions& opt, LoadedCharacter& out,
                     const AdapterRegistry* adapters) {
    out.model = CharacterModel();
    out.easing = EasingMap();
    Status s = out.document.Load(data, size, opt);
    if (!s.ok()) return s;
    const AdapterRegistry& reg = adapters ? *adapters : AdapterRegistry::Builtin();
    s = reg.BuildModel(out.document.root(), out.model);
    if (!s.ok()) return s;
    if (const ModelAdapter* a = reg.Find(out.model.adapter_id)) out.easing = a->Easing();
    return Status::Ok();
}

}  // namespace emote
}  // namespace kk4s
