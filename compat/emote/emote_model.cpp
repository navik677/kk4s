// kk4s E-mote runtime: normalized character model helpers. See emote_model.h.
#include "emote/emote_model.h"

namespace kk4s {
namespace emote {

const char* SourceKindName(SourceRef::Kind k) {
    switch (k) {
    case SourceRef::Kind::None: return "none";
    case SourceRef::Kind::Icon: return "icon";
    case SourceRef::Kind::Motion: return "motion";
    case SourceRef::Kind::Blank: return "blank";
    case SourceRef::Kind::Layout: return "layout";
    case SourceRef::Kind::Shape: return "shape";
    case SourceRef::Kind::Unknown: return "unknown";
    }
    return "?";
}

double TimelineDef::Duration() const {
    double d = 0;
    for (const TimelineTrack& t : tracks) {
        if (!t.keys.empty() && t.keys.back().time > d) d = t.keys.back().time;
        if (t.has_end && t.end_time > d) d = t.end_time;
    }
    return d;
}

const VariableDef* CharacterModel::FindVariable(const std::string& name) const {
    for (const VariableDef& v : variables)
        if (v.name == name) return &v;
    return nullptr;
}

const TimelineDef* CharacterModel::FindTimeline(const std::string& label) const {
    for (const TimelineDef& t : timelines)
        if (t.label == label) return &t;
    return nullptr;
}

const ObjectDef* CharacterModel::FindObject(const std::string& name) const {
    for (const ObjectDef& o : objects)
        if (o.name == name) return &o;
    return nullptr;
}

const MotionDef* CharacterModel::FindMotion(const std::string& object, const std::string& motion) const {
    const ObjectDef* o = FindObject(object);
    if (!o) return nullptr;
    for (const MotionDef& m : o->motions)
        if (m.name == motion) return &m;
    return nullptr;
}

const SourceDef* CharacterModel::FindSource(const std::string& name) const {
    for (const SourceDef& s : sources)
        if (s.name == name) return &s;
    return nullptr;
}

const IconDef* CharacterModel::FindIcon(const std::string& source, const std::string& icon) const {
    const SourceDef* s = FindSource(source);
    if (!s) return nullptr;
    for (const IconDef& i : s->icons)
        if (i.name == icon) return &i;
    return nullptr;
}

}  // namespace emote
}  // namespace kk4s
