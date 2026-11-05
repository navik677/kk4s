// kk4s E-mote runtime: scene evaluation (what is shown at a frame).
//
// Walks a motion's layer tree and picks, for every layer, the frame that is
// active at a given motion frame. Nested motion references are expanded.
// Nothing here interpolates between layer frames, applies mesh deformation or
// composes transforms: those semantics are not researched yet
// (docs/universal_emote_runtime.md, "Geometry"). The raw per-frame values
// are passed through so a renderer, or a later evaluator, can use them.
#pragma once

#include <string>
#include <vector>

#include "emote/emote_model.h"
#include "emote/emote_status.h"

namespace kk4s {
namespace emote {

struct SceneNode {
    std::string path;     // "object/motion/layer/child..." labels
    int depth = 0;
    const LayerDef* layer = nullptr;
    const LayerFrame* frame = nullptr;  // active frame, nullptr when none
    bool visible = false;               // active frame has content
    SourceRef src;
    bool src_resolved = false;          // Icon/Motion reference found in the model
    const IconDef* icon = nullptr;      // for Icon references
};

struct SceneSnapshot {
    std::vector<SceneNode> nodes;  // depth-first, in stored order
    unsigned unresolved = 0;
    unsigned truncated = 0;        // nested motions not expanded (depth limit / cycle)
};

// Active frame of a layer at `frame`: the last frame with time <= frame;
// nullptr before the first frame. ([C] frames are sorted; [H] no interpolation.)
const LayerFrame* ActiveLayerFrame(const LayerDef& layer, double frame);

Status EvaluateStaticPose(const CharacterModel& model, const std::string& object, const std::string& motion,
                          double frame, SceneSnapshot& out, int max_depth = 16);

// [H] A parameter maps a variable onto the motion's frame axis: rangeBegin..rangeEnd
// onto frames 0..division (real files: range -30..30, division 60, lastTime 61).
// Unverified against the original player; exposed for experiments and tests.
double ParameterToFrame(const ParameterDef& p, double value);

}  // namespace emote
}  // namespace kk4s
