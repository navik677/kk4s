// kk4s E-mote runtime: normalized character model.
//
// Independent of PSB bytes, ciphers, archives, games and renderers. Adapters
// (compat/emote/adapters/) fill it from a Value tree; the animation runtime
// and the scene evaluator only read it.
//
// Field status, per docs/universal_emote_runtime.md ("Data model"):
//   [C] confirmed: structure seen in every checked real file
//   [H] hypothesis: meaning inferred, not verified against the original player
//   [R] raw: value kept as stored, meaning unknown
// Unknown parts of the source data are kept as raw codes, never guessed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kk4s {
namespace emote {

struct Canvas {  // [C] root "screenSize"
    double width = 0, height = 0, origin_x = 0, origin_y = 0;
};

// Named value of a variable, e.g. "up" = -30.  [C] structure, [H] used by scripts to name poses
struct VariablePose {
    std::string label;
    double value = 0;
};

struct VariableDef {
    std::string name;                 // [C]
    std::vector<VariablePose> poses;  // [C]
    // [H] range: union of the motions' parameter ranges that use this id
    bool has_range = false;
    double range_begin = 0, range_end = 0;
};

// A value a key frame sets: a number, or text (seen in real timelines as e.g. "20").
struct KeyValue {
    enum class Kind { Number, Text };
    Kind kind = Kind::Number;
    double number = 0;
    std::string text;
};

struct Keyframe {
    double time = 0;        // [C] in timeline frames
    KeyValue value;         // [C]
    int easing_code = 0;    // [R] stored code; mapped by an EasingMap
};

struct TimelineTrack {
    std::string variable;          // [C]
    std::vector<Keyframe> keys;    // [C] sorted by time
    bool has_end = false;          // [C] terminating frame without content
    double end_time = 0;
};

struct TimelineDef {
    std::string label;      // [C]
    bool diff = false;      // [C] flag; [H] additive ("difference") timeline
    double loop_begin = -1, loop_end = -1;  // [C] -1 = none; [H] loop range in frames
    double last_time = -1;  // [R]
    std::vector<TimelineTrack> tracks;
    double Duration() const;  // last key / end marker time
};

// What a layer frame draws.
struct SourceRef {
    enum class Kind {
        None,    // no source (empty "src")
        Icon,    // an image: source + icon name
        Motion,  // a nested motion: object + motion name
        Blank,   // an empty box; detail = "w:h:ox:oy"
        Layout,  // a layout-only node
        Shape,   // a shape primitive; detail = shape name
        Unknown,
    };
    Kind kind = Kind::None;
    std::string a, b;   // Icon: source/icon; Motion: object/motion
    std::string detail;
    std::string raw;    // as stored ("src/x/y", or "x" + icon "y")
};
const char* SourceKindName(SourceRef::Kind k);

struct LayerFrameContent {
    SourceRef src;                                   // [C] reference; [H] Blank/Layout/Shape meaning
    bool has_coord = false;  double coord[3] = {0, 0, 0};  // [C] stored; [H] position x,y,z
    bool has_angle = false;  double angle = 0;       // [H] rotation, degrees
    bool has_opacity = false; double opacity = 0;    // [H] "opa"
    bool has_color = false;  uint32_t color = 0;     // [R]
    int64_t mask = 0;                                // [R]
    int64_t blend_mode = 0;                          // [R] "bm"
    bool has_mesh = false;                           // [C] "mesh" object present
    std::vector<double> mesh_bp;                     // [R] 32 numbers when present
    bool has_motion_ref = false;                     // [C] "motion" sub-object present
    int64_t motion_time_offset = 0;                  // [R]
};

struct LayerFrame {
    double time = 0;        // [C]
    int raw_type = 0;       // [R] 0 = no content (seen as gap and terminator), 2 / 3 = content
    bool has_content = false;
    LayerFrameContent content;
};

struct LayerDef {
    std::string label;      // [C]
    int raw_type = 0;       // [R]
    int64_t parameterize = -1;  // [R] -1 when null; [H] index into the motion's parameters
    int mesh_division = 0;  // [R]
    int mesh_transform = 0; // [R]
    int64_t inherit_mask = 0;  // [R]
    std::vector<int> transform_order;  // [R]
    std::vector<LayerFrame> frames;    // [C] sorted by time
    std::vector<LayerDef> children;    // [C]
};

struct ParameterDef {  // [C] structure; [H] maps a variable onto the motion's frame axis
    std::string id;
    double range_begin = 0, range_end = 0;
    int division = 0;
    int discretization = 0;
    bool enabled = true;
};

struct MotionDef {
    std::string name;
    double last_time = 0;   // [C]
    double loop_time = -1;  // [R]
    std::vector<ParameterDef> parameters;
    std::vector<LayerDef> layers;
};

struct ObjectDef {
    std::string name;
    std::vector<MotionDef> motions;
};

struct IconDef {
    std::string name;
    double width = 0, height = 0, origin_x = 0, origin_y = 0;  // [C]
    bool in_atlas = false; int left = 0, top = 0;   // [C] "left"/"top": position in the texture atlas
    bool has_clip = false; double clip_left = 0, clip_top = 0, clip_right = 0, clip_bottom = 0;  // [R]
    std::string compress;          // [C] e.g. "RL"; [R] codec unknown
    int64_t pixel_resource = -1;   // [C] own pixels (non-atlas icons)
};

struct MipLevel {
    int width = 0, height = 0;
    int64_t pixel_resource = -1;
};

struct TextureDef {   // [C] "texture" of an atlas source
    std::string format;           // seen: "RGBA8" ([C] size = w*h*4, [H] channel order), "DXT5" ([R] not decoded)
    int width = 0, height = 0;
    int truncated_width = 0, truncated_height = 0;  // [R]
    int64_t pixel_resource = -1;
    std::vector<MipLevel> mipmaps;
};

struct SourceDef {
    std::string name;
    std::vector<IconDef> icons;
    bool has_texture = false;
    TextureDef texture;
};

// Behaviour blocks in the metadata (blink, lip sync, physics, clamps, ...).
// Only the names of the variables they drive are extracted; the rest of each
// block is not interpreted.  [C] structure, [R] semantics
struct ControllerDef {
    std::string kind;    // metadata key without "Control", e.g. "eye", "hair"
    std::string label;
    bool enabled = true;
    std::vector<std::string> variables;
};

struct CharacterModel {
    std::string adapter_id;
    std::string spec;            // root "spec"
    std::string format;          // metadata "format"
    int64_t format_version = 0;  // metadata "version"
    double content_version = 0;  // root "version" (3.03 in every real file seen)
    Canvas canvas;
    std::string base_object, base_motion;  // [C] metadata "base"; [H] default pose
    std::vector<VariableDef> variables;
    std::vector<TimelineDef> timelines;
    std::vector<ObjectDef> objects;
    std::vector<SourceDef> sources;
    std::vector<ControllerDef> controllers;
    std::vector<std::string> unknown_root_keys;
    std::vector<std::string> warnings;

    const VariableDef* FindVariable(const std::string& name) const;
    const TimelineDef* FindTimeline(const std::string& label) const;
    const ObjectDef* FindObject(const std::string& name) const;
    const MotionDef* FindMotion(const std::string& object, const std::string& motion) const;
    const SourceDef* FindSource(const std::string& name) const;
    const IconDef* FindIcon(const std::string& source, const std::string& icon) const;
};

}  // namespace emote
}  // namespace kk4s
