// kk4s E-mote runtime: one-call entry point.
//
//   bytes (archive layer already removed)
//     -> Document      PSB core + cipher profile        (psb_document.h, cipher_profile.h)
//     -> Value         normalized PSB value model       (psb_value.h)
//     -> CharacterModel via a schema adapter            (model_adapter.h, emote_model.h)
//     -> AnimationState variables / timelines           (animation_runtime.h)
//     -> SceneSnapshot  active layer frames             (scene.h)
//     -> RenderBackend  textures + triangles            (render_contract.h)
//
// Architecture and limits: docs/universal_emote_runtime.md.
#pragma once

#include "emote/animation_runtime.h"
#include "emote/cipher_profile.h"
#include "emote/emote_assets.h"
#include "emote/emote_model.h"
#include "emote/model_adapter.h"
#include "emote/psb_document.h"
#include "emote/render_contract.h"
#include "emote/scene.h"

namespace kk4s {
namespace emote {

struct LoadedCharacter {
    Document document;
    CharacterModel model;
    EasingMap easing;  // from the adapter that built the model
};

// Document::Load + AdapterRegistry::BuildModel. `adapters` nullptr = builtin.
Status LoadCharacter(const uint8_t* data, size_t size, const LoadOptions& opt, LoadedCharacter& out,
                     const AdapterRegistry* adapters = nullptr);

}  // namespace emote
}  // namespace kk4s
