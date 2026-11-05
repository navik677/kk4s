// kk4s E-mote runtime: adapter for E-mote content version 3.03 documents:
//   root.version == 3.03 (stored as float32), root.metadata.format == "emote",
//   root.metadata.version == 1, root.spec in {"krkr", "win"}
//
// The schema was read from real files (NekoPara Vol.1: 79 "krkr" files in
// emote.xp3 and 78 "win" files in emotewin.xp3 / emotewin_low.xp3). The
// adapter is keyed to these in-data markers, not to a game: another title that
// writes the same schema is handled the same way, one that does not is refused.
//
// "spec" names the asset packing of the export:
//   krkr  per-icon images ("compress" "RL", own pixel resource), path-form
//         references ("src/<source>/<icon>")
//   win   texture atlas per source (icons carry left/top): RGBA8 in dx_ files,
//         DXT5 in dxlow_ files (format kept, not decoded); pair-form
//         references (src "<source>" + icon "<icon>"), root "easing" list
// Other spec values exist in the wider PSB world but were never seen here:
// they are refused instead of guessed.
//
// Only structure is normalized. Codes whose meaning is not known (frame types
// other than 0/2 in timelines, layer types, masks, blend modes, easing codes,
// mesh control points) are kept raw; see docs/universal_emote_runtime.md.
#include "emote/model_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kk4s {
namespace emote {

namespace {

const char* const kKnownRootKeys[] = {"id", "label", "metadata", "object", "screenSize", "source", "spec",
                                      "stereovisionProfile", "easing", "version"};

const double kContentVersion = 3.03;  // stored as float32 3.0299999713897705
const char* const kSpecs[] = {"krkr", "win"};

class EmoteV3 : public ModelAdapter {
public:
    const char* id() const override { return "emote-v3.03"; }
    Status Probe(const Value& root) const override;
    Status Build(const Value& root, CharacterModel& m) const override;
    EasingMap Easing() const override {
        // Real timelines store the codes 0, 1, -1, -2, -10; 0 is ~99%. Only 0 is
        // mapped, and only as an assumption: no reference output of the original
        // player was compared. Other codes stay unknown (RuntimeOptions decides).
        EasingMap e;
        e.Set(0, Curve::Linear, false);
        return e;
    }

private:
    void Variables(const Value& md, CharacterModel& m) const;
    void Timelines(const Value& md, CharacterModel& m) const;
    void Sources(const Value& src, CharacterModel& m) const;
    void Objects(const Value& obj, CharacterModel& m) const;
    void Layer(const Value& lv, LayerDef& out, CharacterModel& m) const;
    void Controllers(const Value& md, CharacterModel& m) const;
};

void Warn(CharacterModel& m, const std::string& w) {
    if (m.warnings.size() < 200) m.warnings.push_back(w);
}

double Num(const Value* v, double def = 0) { return v && v->is_number() ? v->number() : def; }

Status EmoteV3::Probe(const Value& root) const {
    if (!root.is_object()) return Status::Fail(Error::UnsupportedSchema, "root is not an object");
    const Value* md = root.Find("metadata");
    if (!md || !md->is_object()) return Status::Fail(Error::UnsupportedSchema, "no metadata object");
    if (md->GetString("format", "") != "emote") return Status::Fail(Error::UnsupportedSchema, "metadata.format is not \"emote\"");
    const Value* mver = md->Find("version");
    if (!mver || !mver->is_int() || mver->int_value() != 1)
        return Status::Fail(Error::UnsupportedSchema, "metadata.version is not 1");
    const Value* ver = root.Find("version");
    if (!ver || !ver->is_number() || std::fabs(ver->number() - kContentVersion) > 1e-4) {
        char m[96];
        std::snprintf(m, sizeof m, "content version %g is not 3.03", ver && ver->is_number() ? ver->number() : -1.0);
        return Status::Fail(Error::UnsupportedSchema, m);
    }
    std::string spec = root.GetString("spec", "");
    bool known = false;
    for (const char* s : kSpecs) known = known || spec == s;
    if (!known) return Status::Fail(Error::UnsupportedSchema, "spec \"" + spec + "\" was never seen with this schema");
    return Status::Ok();
}
Status EmoteV3::Build(const Value& root, CharacterModel& m) const {
    Status p = Probe(root);
    if (!p.ok()) return p;
    const Value& md = *root.Find("metadata");
    m.spec = root.GetString("spec", "");
    m.format = md.GetString("format", "");
    m.format_version = md.GetInt("version", 0);
    m.content_version = root.GetNumber("version", 0);
    if (const Value* ss = root.Find("screenSize")) {
        m.canvas.width = ss->GetNumber("width", 0);
        m.canvas.height = ss->GetNumber("height", 0);
        m.canvas.origin_x = ss->GetNumber("originX", 0);
        m.canvas.origin_y = ss->GetNumber("originY", 0);
    }
    if (const Value* base = md.Find("base")) {
        m.base_object = base->GetString("chara", "");
        m.base_motion = base->GetString("motion", "");
    }
    for (const Value::Member& mem : root.members()) {
        bool known = false;
        for (const char* k : kKnownRootKeys) known = known || mem.first == k;
        if (!known) m.unknown_root_keys.push_back(mem.first);
    }

    Variables(md, m);
    Timelines(md, m);
    if (const Value* src = root.Find("source")) Sources(*src, m);
    if (const Value* obj = root.Find("object")) Objects(*obj, m);

    // Variables that only appear as motion parameters still exist at run time.
    for (const ObjectDef& o : m.objects)
        for (const MotionDef& mo : o.motions)
            for (const ParameterDef& pd : mo.parameters) {
                VariableDef* v = nullptr;
                for (VariableDef& cand : m.variables)
                    if (cand.name == pd.id) v = &cand;
                if (!v) {
                    m.variables.push_back(VariableDef());
                    v = &m.variables.back();
                    v->name = pd.id;
                }
                double lo = std::min(pd.range_begin, pd.range_end), hi = std::max(pd.range_begin, pd.range_end);
                if (!v->has_range) {
                    v->has_range = true;
                    v->range_begin = lo;
                    v->range_end = hi;
                } else {
                    v->range_begin = std::min(v->range_begin, lo);
                    v->range_end = std::max(v->range_end, hi);
                }
            }
    Controllers(md, m);  // after all variables are known
    return Status::Ok();
}

void EmoteV3::Variables(const Value& md, CharacterModel& m) const {
    const Value* list = md.Find("variableList");
    if (!list || !list->is_array()) return;
    for (const Value& v : list->items()) {
        if (!v.is_object()) continue;
        VariableDef d;
        d.name = v.GetString("label", "");
        if (d.name.empty()) {
            Warn(m, "variableList entry without label");
            continue;
        }
        if (const Value* fl = v.Find("frameList"))
            for (const Value& f : fl->items()) {
                VariablePose p;
                p.label = f.GetString("label", "");
                p.value = f.GetNumber("frame", 0);
                d.poses.push_back(p);
            }
        m.variables.push_back(std::move(d));
    }
}

void EmoteV3::Timelines(const Value& md, CharacterModel& m) const {
    const Value* list = md.Find("timelineControl");
    if (!list || !list->is_array()) return;
    for (const Value& tv : list->items()) {
        if (!tv.is_object()) continue;
        TimelineDef t;
        t.label = tv.GetString("label", "");
        t.diff = tv.GetInt("diff", 0) != 0;
        t.loop_begin = tv.GetNumber("loopBegin", -1);
        t.loop_end = tv.GetNumber("loopEnd", -1);
        t.last_time = tv.GetNumber("lastTime", -1);
        if (const Value* vl = tv.Find("variableList"))
            for (const Value& var : vl->items()) {
                TimelineTrack tr;
                tr.variable = var.GetString("label", "");
                const Value* fl = var.Find("frameList");
                if (fl)
                    for (const Value& f : fl->items()) {
                        int64_t type = f.GetInt("type", -1);
                        double time = f.GetNumber("time", 0);
                        const Value* c = f.Find("content");
                        bool content = c && c->is_object();
                        if (type == 0 && !content) {  // terminator (always last in real files)
                            tr.has_end = true;
                            tr.end_time = time;
                            continue;
                        }
                        if (type != 2 || !content) {
                            char w[160];
                            std::snprintf(w, sizeof w, "timeline %s/%s: frame type %lld not understood, skipped",
                                          t.label.c_str(), tr.variable.c_str(), (long long)type);
                            Warn(m, w);
                            continue;
                        }
                        Keyframe k;
                        k.time = time;
                        k.easing_code = (int)c->GetInt("easing", 0);
                        const Value* val = c->Find("value");
                        if (val && val->is_number()) {
                            k.value.kind = KeyValue::Kind::Number;
                            k.value.number = val->number();
                        } else if (val && val->is_string()) {
                            k.value.kind = KeyValue::Kind::Text;
                            k.value.text = val->string_value();
                        } else {
                            Warn(m, "timeline " + t.label + "/" + tr.variable + ": key without a value, skipped");
                            continue;
                        }
                        tr.keys.push_back(k);
                    }
                bool sorted = std::is_sorted(tr.keys.begin(), tr.keys.end(),
                                             [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
                if (!sorted) {
                    Warn(m, "timeline " + t.label + "/" + tr.variable + ": keys out of order, sorted");
                    std::stable_sort(tr.keys.begin(), tr.keys.end(),
                                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
                }
                t.tracks.push_back(std::move(tr));
            }
        m.timelines.push_back(std::move(t));
    }
}

void EmoteV3::Sources(const Value& src, CharacterModel& m) const {
    for (const Value::Member& sm : src.members()) {
        SourceDef s;
        s.name = sm.first;
        const Value& sv = sm.second;
        if (const Value* icons = sv.Find("icon"))
            for (const Value::Member& im : icons->members()) {
                const Value& iv = im.second;
                IconDef ic;
                ic.name = im.first;
                ic.width = iv.GetNumber("width", 0);
                ic.height = iv.GetNumber("height", 0);
                ic.origin_x = iv.GetNumber("originX", 0);
                ic.origin_y = iv.GetNumber("originY", 0);
                if (iv.Find("left") && iv.Find("top")) {
                    ic.in_atlas = true;
                    ic.left = (int)iv.GetInt("left", 0);
                    ic.top = (int)iv.GetInt("top", 0);
                }
                if (const Value* clip = iv.Find("clip")) {
                    if (clip->is_object()) {
                        ic.has_clip = true;
                        ic.clip_left = clip->GetNumber("left", 0);
                        ic.clip_top = clip->GetNumber("top", 0);
                        ic.clip_right = clip->GetNumber("right", 0);
                        ic.clip_bottom = clip->GetNumber("bottom", 0);
                    }
                }
                ic.compress = iv.GetString("compress", "");
                if (const Value* px = iv.Find("pixel"))
                    if (px->is_resource()) ic.pixel_resource = px->resource_index();
                s.icons.push_back(std::move(ic));
            }
        if (const Value* tex = sv.Find("texture")) {
            if (tex->is_object()) {
                s.has_texture = true;
                TextureDef& t = s.texture;
                t.format = tex->GetString("type", "");
                t.width = (int)tex->GetInt("width", 0);
                t.height = (int)tex->GetInt("height", 0);
                t.truncated_width = (int)tex->GetInt("truncated_width", 0);
                t.truncated_height = (int)tex->GetInt("truncated_height", 0);
                if (const Value* px = tex->Find("pixel"))
                    if (px->is_resource()) t.pixel_resource = px->resource_index();
                if (const Value* mips = tex->Find("mipMap"))
                    for (const Value& mv : mips->items()) {
                        MipLevel ml;
                        ml.width = (int)mv.GetInt("width", 0);
                        ml.height = (int)mv.GetInt("height", 0);
                        if (const Value* px = mv.Find("pixel"))
                            if (px->is_resource()) ml.pixel_resource = px->resource_index();
                        t.mipmaps.push_back(ml);
                    }
            }
        }
        m.sources.push_back(std::move(s));
    }
}

void EmoteV3::Objects(const Value& obj, CharacterModel& m) const {
    // Names first: pair-form references are resolved against them.
    for (const Value::Member& om : obj.members()) {
        ObjectDef o;
        o.name = om.first;
        m.objects.push_back(std::move(o));
    }
    size_t oi = 0;
    for (const Value::Member& om : obj.members()) {
        const Value* motions = om.second.Find("motion");
        if (motions)
            for (const Value::Member& mm : motions->members()) {
                const Value& mv = mm.second;
                MotionDef mo;
                mo.name = mm.first;
                mo.last_time = mv.GetNumber("lastTime", 0);
                mo.loop_time = mv.GetNumber("loopTime", -1);
                if (const Value* params = mv.Find("parameter"))
                    for (const Value& pv : params->items()) {
                        ParameterDef pd;
                        pd.id = pv.GetString("id", "");
                        pd.range_begin = pv.GetNumber("rangeBegin", 0);
                        pd.range_end = pv.GetNumber("rangeEnd", 0);
                        pd.division = (int)pv.GetInt("division", 0);
                        pd.discretization = (int)pv.GetInt("discretization", 0);
                        pd.enabled = pv.GetInt("enabled", 1) != 0;
                        mo.parameters.push_back(pd);
                    }
                if (const Value* layers = mv.Find("layer"))
                    for (const Value& lv : layers->items()) {
                        LayerDef ld;
                        Layer(lv, ld, m);
                        mo.layers.push_back(std::move(ld));
                    }
                m.objects[oi].motions.push_back(std::move(mo));
            }
        oi++;
    }
}

void EmoteV3::Layer(const Value& lv, LayerDef& out, CharacterModel& m) const {
    out.label = lv.GetString("label", "");
    out.raw_type = (int)lv.GetInt("type", -1);
    const Value* pz = lv.Find("parameterize");
    out.parameterize = pz && pz->is_int() ? pz->int_value() : -1;
    out.mesh_division = (int)lv.GetInt("meshDivision", 0);
    out.mesh_transform = (int)lv.GetInt("meshTransform", 0);
    out.inherit_mask = lv.GetInt("inheritMask", 0);
    if (const Value* to = lv.Find("transformOrder"))
        for (const Value& x : to->items()) out.transform_order.push_back((int)x.number());
    if (const Value* fl = lv.Find("frameList"))
        for (const Value& fv : fl->items()) {
            LayerFrame f;
            f.time = fv.GetNumber("time", 0);
            f.raw_type = (int)fv.GetInt("type", -1);
            const Value* c = fv.Find("content");
            if (c && c->is_object()) {
                f.has_content = true;
                LayerFrameContent& fc = f.content;
                std::string src = c->GetString("src", "");
                const Value* icon = c->Find("icon");
                std::string icon_s = icon && icon->is_string() ? icon->string_value() : std::string();
                fc.src = ParseSourceRef(src, icon && icon->is_string() ? &icon_s : nullptr, m);
                if (const Value* coord = c->Find("coord")) {
                    if (coord->is_array() && coord->size() >= 2) {
                        fc.has_coord = true;
                        for (size_t i = 0; i < 3 && i < coord->size(); i++) fc.coord[i] = (*coord)[i].number();
                    }
                }
                if (const Value* a = c->Find("angle")) { fc.has_angle = a->is_number(); fc.angle = Num(a); }
                if (const Value* o = c->Find("opa")) { fc.has_opacity = o->is_number(); fc.opacity = Num(o); }
                if (const Value* col = c->Find("color")) { fc.has_color = col->is_number(); fc.color = (uint32_t)col->int_value(); }
                fc.mask = c->GetInt("mask", 0);
                fc.blend_mode = c->GetInt("bm", 0);
                if (const Value* mesh = c->Find("mesh")) {
                    if (mesh->is_object()) {
                        fc.has_mesh = true;
                        if (const Value* bp = mesh->Find("bp"))
                            for (const Value& x : bp->items()) fc.mesh_bp.push_back(x.number());
                    }
                }
                if (const Value* mo = c->Find("motion")) {
                    if (mo->is_object()) {
                        fc.has_motion_ref = true;
                        fc.motion_time_offset = mo->GetInt("timeOffset", 0);
                    }
                }
            }
            out.frames.push_back(std::move(f));
        }
    if (!std::is_sorted(out.frames.begin(), out.frames.end(),
                        [](const LayerFrame& a, const LayerFrame& b) { return a.time < b.time; })) {
        Warn(m, "layer " + out.label + ": frames out of order, sorted");
        std::stable_sort(out.frames.begin(), out.frames.end(),
                         [](const LayerFrame& a, const LayerFrame& b) { return a.time < b.time; });
    }
    if (const Value* ch = lv.Find("children"))
        for (const Value& cv : ch->items()) {
            LayerDef c;
            Layer(cv, c, m);
            out.children.push_back(std::move(c));
        }
}

void EmoteV3::Controllers(const Value& md, CharacterModel& m) const {
    const std::string suffix = "Control";
    for (const Value::Member& mem : md.members()) {
        const std::string& key = mem.first;
        if (key.size() <= suffix.size() || key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        if (key == "timelineControl") continue;  // normalized as timelines
        std::string kind = key.substr(0, key.size() - suffix.size());
        auto one = [&](const Value& item) {
            ControllerDef c;
            c.kind = kind;
            c.label = item.GetString("label", "");
            c.enabled = item.GetInt("enabled", 1) != 0;
            // Any string member naming a known variable (var_lr, label, talkLabel, ...).
            for (const Value::Member& f : item.members())
                if (f.second.is_string() && m.FindVariable(f.second.string_value()))
                    c.variables.push_back(f.second.string_value());
            m.controllers.push_back(std::move(c));
        };
        if (mem.second.is_array()) {
            for (const Value& item : mem.second.items())
                if (item.is_object()) one(item);
        } else if (mem.second.is_object()) {
            one(mem.second);
        }
    }
}

}  // namespace

std::unique_ptr<ModelAdapter> MakeEmoteV3Adapter() { return std::unique_ptr<ModelAdapter>(new EmoteV3()); }

}  // namespace emote
}  // namespace kk4s
