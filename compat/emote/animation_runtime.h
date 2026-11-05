// kk4s E-mote runtime: animation state (variables, tweens, timelines).
//
// Works on the normalized model only (TimelineDef / VariableDef); it never
// sees PSB data and draws nothing. Any number of characters, variables and
// timelines; no variable name is special.
//
// Semantics that are kk4s decisions or hypotheses, not verified against the
// original player, are listed in docs/universal_emote_runtime.md
// ("Animation runtime") and selectable through RuntimeOptions where it matters.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "emote/easing.h"
#include "emote/emote_model.h"
#include "emote/emote_status.h"

namespace kk4s {
namespace emote {

enum class UnknownEasingPolicy {
    Linear,  // use a linear curve and count it in diagnostics()
    Error,   // refuse: SetVariable / PlayTimeline return UnsupportedFeature
};

struct RuntimeOptions {
    double frames_per_second = 60.0;  // [H] time base of timeline frames
    UnknownEasingPolicy unknown_easing = UnknownEasingPolicy::Linear;
    bool clamp_to_range = false;      // clamp evaluated values to a variable's range
    bool auto_define_variables = true;  // timeline tracks may name undeclared variables
};

enum class TimelineBlend { Override, Additive };

struct PlayOptions {
    bool loop = true;          // loop over the definition's loop range, when it has one
    double start_frame = 0;
    double weight = 1.0;       // 0..1 contribution
    // Default: Additive for TimelineDef::diff, else Override ([H]).
    bool blend_from_definition = true;
    TimelineBlend blend = TimelineBlend::Override;
};

struct RuntimeDiagnostics {
    std::vector<int> unknown_easing_codes;     // distinct codes that fell back to linear
    unsigned long unknown_easing_uses = 0;
    std::vector<std::string> auto_defined_variables;
    unsigned long unresolved_text_values = 0;  // text key values that are no pose and no number
};

class AnimationState {
public:
    explicit AnimationState(RuntimeOptions opt = RuntimeOptions(), EasingMap easing = EasingMap());

    // Declares the model's variables and copies its timelines.
    Status Bind(const CharacterModel& model);
    Status DefineVariable(const std::string& name, double initial, const VariableDef* def = nullptr);
    Status AddTimeline(const TimelineDef& def);  // replaces one with the same label

    // ---- variables
    bool HasVariable(const std::string& name) const;
    // Value set by SetVariable (with its tween), before timelines.
    Status GetBaseVariable(const std::string& name, double& out) const;
    // Value after timelines are applied (what a renderer would use).
    Status GetVariable(const std::string& name, double& out) const;
    // duration in frames; 0 = immediate. easing_code goes through the EasingMap.
    Status SetVariable(const std::string& name, double value, double duration_frames = 0, int easing_code = 0);
    Status SetVariableCurve(const std::string& name, double value, double duration_frames, Curve curve);
    // Text form: a pose label of the variable or a number.
    Status SetVariableText(const std::string& name, const std::string& value, double duration_frames = 0,
                           int easing_code = 0);

    // ---- timelines
    std::vector<std::string> TimelineLabels() const;
    Status PlayTimeline(const std::string& label, const PlayOptions& opt = PlayOptions());
    Status StopTimeline(const std::string& label);
    void StopAllTimelines();
    bool IsTimelinePlaying(const std::string& label) const;
    double TimelineFrame(const std::string& label) const;  // -1 when not playing
    std::vector<std::string> PlayingTimelines() const;

    // ---- time
    void AdvanceFrames(double frames);
    void AdvanceMs(double ms);
    double total_frames() const { return clock_; }

    struct VariableValue {
        std::string name;
        double value;
    };
    std::vector<VariableValue> Snapshot() const;  // evaluated, sorted by name

    const RuntimeDiagnostics& diagnostics() const { return diag_; }
    const RuntimeOptions& options() const { return opt_; }

    // Value of one track at `frame` (no looping); false when it has no keys.
    bool EvalTrack(const TimelineTrack& tr, double frame, double& out) const;

private:
    struct Var {
        double from = 0, to = 0;
        double elapsed = 0, duration = 0;
        Curve curve = Curve::Linear;
        bool has_range = false;
        double lo = 0, hi = 0;
        std::vector<VariablePose> poses;
        double Base() const;
    };
    struct Playing {
        std::string label;
        double frame = 0;
        bool loop = true;
        double weight = 1;
        TimelineBlend blend = TimelineBlend::Override;
    };

    Status ResolveCurve(int code, Curve& out);
    bool ResolveCurveConst(int code, Curve& out) const;
    bool TextToNumber(const Var* v, const std::string& text, double& out) const;
    const TimelineDef* FindDef(const std::string& label) const;
    std::map<std::string, double> Evaluate() const;
    void Finish(const Playing& p);

    RuntimeOptions opt_;
    EasingMap easing_;
    std::map<std::string, Var> vars_;
    std::vector<TimelineDef> timelines_;
    std::vector<Playing> playing_;
    double clock_ = 0;
    mutable RuntimeDiagnostics diag_;
};

}  // namespace emote
}  // namespace kk4s
