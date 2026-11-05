// kk4s E-mote runtime: animation state. See animation_runtime.h.
#include "emote/animation_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace kk4s {
namespace emote {

double AnimationState::Var::Base() const {
    if (duration <= 0 || elapsed >= duration) return to;
    return from + (to - from) * EvalCurve(curve, elapsed / duration);
}

AnimationState::AnimationState(RuntimeOptions opt, EasingMap easing) : opt_(opt), easing_(std::move(easing)) {
    if (!(opt_.frames_per_second > 0)) opt_.frames_per_second = 60.0;
}

Status AnimationState::Bind(const CharacterModel& model) {
    for (const VariableDef& v : model.variables) {
        double initial = 0;
        if (v.has_range && (initial < v.range_begin || initial > v.range_end)) initial = v.range_begin;
        Status s = DefineVariable(v.name, initial, &v);
        if (!s.ok()) return s;
    }
    for (const TimelineDef& t : model.timelines) {
        Status s = AddTimeline(t);
        if (!s.ok()) return s;
    }
    return Status::Ok();
}

Status AnimationState::DefineVariable(const std::string& name, double initial, const VariableDef* def) {
    if (name.empty()) return Status::Fail(Error::InvalidArgument, "variable without a name");
    Var v;
    v.from = v.to = initial;
    if (def) {
        v.has_range = def->has_range;
        v.lo = def->range_begin;
        v.hi = def->range_end;
        v.poses = def->poses;
    }
    vars_[name] = v;
    return Status::Ok();
}

Status AnimationState::AddTimeline(const TimelineDef& def) {
    if (def.label.empty()) return Status::Fail(Error::InvalidArgument, "timeline without a label");
    for (TimelineDef& t : timelines_)
        if (t.label == def.label) {
            t = def;
            return Status::Ok();
        }
    timelines_.push_back(def);
    return Status::Ok();
}

bool AnimationState::HasVariable(const std::string& name) const { return vars_.count(name) != 0; }

Status AnimationState::GetBaseVariable(const std::string& name, double& out) const {
    auto it = vars_.find(name);
    if (it == vars_.end()) return Status::Fail(Error::NotFound, "no variable " + name);
    out = it->second.Base();
    return Status::Ok();
}

Status AnimationState::GetVariable(const std::string& name, double& out) const {
    if (!HasVariable(name)) return Status::Fail(Error::NotFound, "no variable " + name);
    std::map<std::string, double> ev = Evaluate();
    out = ev[name];
    return Status::Ok();
}

bool AnimationState::ResolveCurveConst(int code, Curve& out) const {
    const EasingMap::Entry* e = easing_.Find(code);
    if (!e) return false;
    out = e->curve;
    return true;
}

Status AnimationState::ResolveCurve(int code, Curve& out) {
    if (ResolveCurveConst(code, out)) return Status::Ok();
    if (opt_.unknown_easing == UnknownEasingPolicy::Error)
        return Status::Fail(Error::UnsupportedFeature, "easing code " + std::to_string(code) + " has no known curve");
    out = Curve::Linear;
    if (std::find(diag_.unknown_easing_codes.begin(), diag_.unknown_easing_codes.end(), code) ==
        diag_.unknown_easing_codes.end())
        diag_.unknown_easing_codes.push_back(code);
    diag_.unknown_easing_uses++;
    return Status::Ok();
}

Status AnimationState::SetVariableCurve(const std::string& name, double value, double duration, Curve curve) {
    auto it = vars_.find(name);
    if (it == vars_.end()) return Status::Fail(Error::NotFound, "no variable " + name);
    if (!std::isfinite(value) || !(duration >= 0) || !std::isfinite(duration))
        return Status::Fail(Error::InvalidArgument, "variable value/duration must be finite");
    Var& v = it->second;
    v.from = v.Base();
    v.to = value;
    v.elapsed = 0;
    v.duration = duration;
    v.curve = curve;
    return Status::Ok();
}

Status AnimationState::SetVariable(const std::string& name, double value, double duration, int easing_code) {
    if (!HasVariable(name)) return Status::Fail(Error::NotFound, "no variable " + name);
    Curve c = Curve::Linear;
    if (duration > 0) {
        Status s = ResolveCurve(easing_code, c);
        if (!s.ok()) return s;
    }
    return SetVariableCurve(name, value, duration, c);
}

bool AnimationState::TextToNumber(const Var* v, const std::string& text, double& out) const {
    if (v)
        for (const VariablePose& p : v->poses)
            if (p.label == text) {
                out = p.value;
                return true;
            }
    if (text.empty()) return false;
    char* end = nullptr;
    double d = std::strtod(text.c_str(), &end);
    if (!end || *end || !std::isfinite(d)) return false;
    out = d;
    return true;
}

Status AnimationState::SetVariableText(const std::string& name, const std::string& value, double duration,
                                       int easing_code) {
    auto it = vars_.find(name);
    if (it == vars_.end()) return Status::Fail(Error::NotFound, "no variable " + name);
    double d = 0;
    if (!TextToNumber(&it->second, value, d))
        return Status::Fail(Error::InvalidArgument, "'" + value + "' is neither a pose of " + name + " nor a number");
    return SetVariable(name, d, duration, easing_code);
}

std::vector<std::string> AnimationState::TimelineLabels() const {
    std::vector<std::string> out;
    for (const TimelineDef& t : timelines_) out.push_back(t.label);
    return out;
}

const TimelineDef* AnimationState::FindDef(const std::string& label) const {
    for (const TimelineDef& t : timelines_)
        if (t.label == label) return &t;
    return nullptr;
}

Status AnimationState::PlayTimeline(const std::string& label, const PlayOptions& po) {
    const TimelineDef* def = FindDef(label);
    if (!def) return Status::Fail(Error::NotFound, "no timeline " + label);
    if (!std::isfinite(po.start_frame) || !std::isfinite(po.weight))
        return Status::Fail(Error::InvalidArgument, "start frame / weight must be finite");
    for (const TimelineTrack& tr : def->tracks) {
        for (const Keyframe& k : tr.keys) {
            Curve c;
            Status s = ResolveCurve(k.easing_code, c);
            if (!s.ok()) return Status::Fail(s.code, "timeline " + label + ": " + s.message);
        }
        if (!HasVariable(tr.variable)) {
            if (!opt_.auto_define_variables)
                return Status::Fail(Error::NotFound, "timeline " + label + " drives undeclared variable " + tr.variable);
            DefineVariable(tr.variable, 0, nullptr);
            diag_.auto_defined_variables.push_back(tr.variable);
        }
    }
    Playing p;
    p.label = label;
    p.frame = po.start_frame;
    p.loop = po.loop;
    p.weight = std::max(0.0, std::min(1.0, po.weight));
    p.blend = po.blend_from_definition ? (def->diff ? TimelineBlend::Additive : TimelineBlend::Override) : po.blend;
    for (Playing& q : playing_)
        if (q.label == label) {
            q = p;
            return Status::Ok();
        }
    playing_.push_back(p);
    return Status::Ok();
}

Status AnimationState::StopTimeline(const std::string& label) {
    for (size_t i = 0; i < playing_.size(); i++)
        if (playing_[i].label == label) {
            playing_.erase(playing_.begin() + (long)i);
            return Status::Ok();
        }
    return Status::Fail(Error::NotFound, "timeline " + label + " is not playing");
}

void AnimationState::StopAllTimelines() { playing_.clear(); }

bool AnimationState::IsTimelinePlaying(const std::string& label) const {
    for (const Playing& p : playing_)
        if (p.label == label) return true;
    return false;
}

double AnimationState::TimelineFrame(const std::string& label) const {
    for (const Playing& p : playing_)
        if (p.label == label) return p.frame;
    return -1;
}

std::vector<std::string> AnimationState::PlayingTimelines() const {
    std::vector<std::string> out;
    for (const Playing& p : playing_) out.push_back(p.label);
    return out;
}

bool AnimationState::EvalTrack(const TimelineTrack& tr, double frame, double& out) const {
    if (tr.keys.empty()) return false;
    auto it = vars_.find(tr.variable);
    const Var* var = it == vars_.end() ? nullptr : &it->second;
    auto value_of = [&](const Keyframe& k, double& v) -> bool {
        if (k.value.kind == KeyValue::Kind::Number) {
            v = k.value.number;
            return true;
        }
        if (TextToNumber(var, k.value.text, v)) return true;
        diag_.unresolved_text_values++;
        return false;
    };
    const std::vector<Keyframe>& ks = tr.keys;
    if (frame <= ks.front().time) return value_of(ks.front(), out);
    if (frame >= ks.back().time) return value_of(ks.back(), out);
    // last key with time <= frame
    size_t i = (size_t)(std::upper_bound(ks.begin(), ks.end(), frame,
                                         [](double f, const Keyframe& k) { return f < k.time; }) -
                        ks.begin()) - 1;
    const Keyframe& a = ks[i];
    const Keyframe& b = ks[i + 1];
    double va = 0, vb = 0;
    if (!value_of(a, va)) return false;
    // Text keys hold their value until the next key ([H]: discrete labels).
    if (a.value.kind == KeyValue::Kind::Text || b.value.kind == KeyValue::Kind::Text || !value_of(b, vb)) {
        out = va;
        return true;
    }
    double span = b.time - a.time;
    double u = span > 0 ? (frame - a.time) / span : 1.0;
    Curve c = Curve::Linear;
    ResolveCurveConst(a.easing_code, c);  // unknown codes were reported at PlayTimeline
    out = va + (vb - va) * EvalCurve(c, u);
    return true;
}

std::map<std::string, double> AnimationState::Evaluate() const {
    std::map<std::string, double> r;
    for (const auto& kv : vars_) r[kv.first] = kv.second.Base();
    for (const Playing& p : playing_) {
        const TimelineDef* def = FindDef(p.label);
        if (!def) continue;
        for (const TimelineTrack& tr : def->tracks) {
            double v = 0;
            if (!EvalTrack(tr, p.frame, v)) continue;
            double& cur = r[tr.variable];
            if (p.blend == TimelineBlend::Additive)
                cur += v * p.weight;
            else
                cur = cur * (1 - p.weight) + v * p.weight;
        }
    }
    if (opt_.clamp_to_range)
        for (auto& kv : r) {
            auto it = vars_.find(kv.first);
            if (it != vars_.end() && it->second.has_range)
                kv.second = std::max(it->second.lo, std::min(it->second.hi, kv.second));
        }
    return r;
}

// A finished Override timeline leaves its last values as the new base ([H]).
void AnimationState::Finish(const Playing& p) {
    if (p.blend != TimelineBlend::Override) return;
    const TimelineDef* def = FindDef(p.label);
    if (!def) return;
    for (const TimelineTrack& tr : def->tracks) {
        double v = 0;
        auto it = vars_.find(tr.variable);
        if (it == vars_.end() || !EvalTrack(tr, def->Duration(), v)) continue;
        Var& var = it->second;
        double base = var.Base();
        var.from = var.to = base * (1 - p.weight) + v * p.weight;
        var.elapsed = var.duration = 0;
    }
}

void AnimationState::AdvanceFrames(double frames) {
    if (!(frames > 0) || !std::isfinite(frames)) return;
    clock_ += frames;
    for (auto& kv : vars_) {
        Var& v = kv.second;
        if (v.duration > 0 && v.elapsed < v.duration) v.elapsed = std::min(v.duration, v.elapsed + frames);
    }
    std::vector<Playing> keep;
    for (Playing& p : playing_) {
        const TimelineDef* def = FindDef(p.label);
        if (!def) continue;
        p.frame += frames;
        bool loops = p.loop && def->loop_begin >= 0 && def->loop_end > def->loop_begin;
        if (loops) {
            if (p.frame >= def->loop_end)
                p.frame = def->loop_begin + std::fmod(p.frame - def->loop_begin, def->loop_end - def->loop_begin);
            keep.push_back(p);
        } else if (p.frame >= def->Duration()) {
            Finish(p);
        } else {
            keep.push_back(p);
        }
    }
    playing_ = std::move(keep);
}

void AnimationState::AdvanceMs(double ms) { AdvanceFrames(ms * opt_.frames_per_second / 1000.0); }

std::vector<AnimationState::VariableValue> AnimationState::Snapshot() const {
    std::vector<VariableValue> out;
    for (const auto& kv : Evaluate()) out.push_back(VariableValue{kv.first, kv.second});
    return out;
}

}  // namespace emote
}  // namespace kk4s
