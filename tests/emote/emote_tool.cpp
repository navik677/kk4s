// kk4s E-mote runtime: inspection tool for one PSB file (used by
// tests/emote/run_real.py on real game resources).
//
//   emote_tool [--profile ID] [--sha256 HEX]... [--no-structural] [FILE|-]
//
// Reads the PSB (archive layer already removed) from FILE or stdin, runs the
// whole pipeline (core, cipher profile, adapter, runtime, scene, textures) and
// prints one JSON object. Exit 0 when the file loaded, 1 on a controlled error.
#include "emote/emote.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace kk4s::emote;

namespace {

std::string Esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += (char)c;
        } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else {
            o += (char)c;
        }
    }
    return o;
}

struct Stats {
    unsigned layers = 0, layer_frames = 0, content_frames = 0, mesh_bp = 0;
    std::map<std::string, unsigned> refs;
    unsigned unresolved = 0;
    std::map<int, unsigned> layer_frame_types;
};

void WalkLayers(const CharacterModel& m, const std::vector<LayerDef>& ls, Stats& st) {
    for (const LayerDef& l : ls) {
        st.layers++;
        for (const LayerFrame& f : l.frames) {
            st.layer_frames++;
            st.layer_frame_types[f.raw_type]++;
            if (!f.has_content) continue;
            st.content_frames++;
            if (!f.content.mesh_bp.empty()) st.mesh_bp++;
            const SourceRef& r = f.content.src;
            st.refs[SourceKindName(r.kind)]++;
            bool ok = true;
            if (r.kind == SourceRef::Kind::Icon) ok = m.FindIcon(r.a, r.b) != nullptr;
            if (r.kind == SourceRef::Kind::Motion) ok = m.FindMotion(r.a, r.b) != nullptr;
            if (r.kind == SourceRef::Kind::Unknown) ok = false;
            if (!ok) st.unresolved++;
        }
        WalkLayers(m, l.children, st);
    }
}

}  // namespace

int main(int argc, char** argv) {
    LoadOptions opt;
    std::string path = "-";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--profile" && i + 1 < argc) opt.pinned_profile = argv[++i];
        else if (a == "--sha256" && i + 1 < argc) opt.binary_sha256.push_back(argv[++i]);
        else if (a == "--no-structural") opt.allow_structural_detection = false;
        else path = a;
    }
    std::vector<uint8_t> data;
    FILE* f = nullptr;
    if (path == "-") {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        f = stdin;
    } else {
        f = std::fopen(path.c_str(), "rb");
        if (!f) {
            std::printf("{\"ok\":false,\"error\":\"io\",\"message\":\"cannot open input\"}\n");
            return 1;
        }
    }
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    if (f != stdin) std::fclose(f);

    LoadedCharacter ch;
    Status st = LoadCharacter(data.data(), data.size(), opt, ch);
    const Document& d = ch.document;
    std::string out = "{";
    char tmp[256];
    out += std::string("\"ok\":") + (st.ok() ? "true" : "false");
    out += ",\"error\":\"" + std::string(st.ok() ? "" : ErrorName(st.code)) + "\"";
    out += ",\"message\":\"" + Esc(st.message) + "\"";
    out += ",\"size\":" + std::to_string(data.size());
    if (!d.root().is_undefined()) {
        std::snprintf(tmp, sizeof tmp,
                      ",\"version\":%u,\"version_support\":\"%s\",\"profile\":\"%s\",\"confidence\":\"%s\","
                      "\"adler32_plain\":\"%08x\",\"resources\":%zu",
                      (unsigned)d.header().version, VersionSupportName(d.version_support()),
                      d.cipher().profile_id.c_str(), ConfidenceName(d.cipher().confidence), d.plain_adler32(),
                      d.resource_count());
        out += tmp;
    }
    if (!st.ok()) {
        out += "}";
        std::printf("%s\n", out.c_str());
        return 1;
    }

    const CharacterModel& m = ch.model;
    Stats s;
    unsigned motions = 0;
    for (const ObjectDef& o : m.objects)
        for (const MotionDef& mo : o.motions) {
            motions++;
            WalkLayers(m, mo.layers, s);
        }
    unsigned tracks = 0, keys = 0, text_keys = 0;
    std::map<int, unsigned> easing;
    for (const TimelineDef& t : m.timelines)
        for (const TimelineTrack& tr : t.tracks) {
            tracks++;
            for (const Keyframe& k : tr.keys) {
                keys++;
                easing[k.easing_code]++;
                if (k.value.kind == KeyValue::Kind::Text) text_keys++;
            }
        }
    unsigned icons = 0, icons_rl = 0, icons_atlas = 0, textures = 0, textures_ok = 0;
    unsigned textures_unsupported = 0;
    std::map<std::string, unsigned> texture_formats;
    std::string texture_errors;
    for (const SourceDef& src : m.sources) {
        for (const IconDef& ic : src.icons) {
            icons++;
            if (ic.compress == "RL") icons_rl++;
            if (ic.in_atlas) icons_atlas++;
        }
        if (src.has_texture) {
            textures++;
            TexturePixels tp;
            Status ts = GetSourceTexture(d, src, tp);
            texture_formats[src.texture.format]++;
            if (ts.ok()) textures_ok++;
            else if (ts.code == Error::UnsupportedFeature) textures_unsupported++;
            else texture_errors += Esc(ts.message) + ";";
        }
    }

    // Runtime: play every timeline from a fresh state, all values must stay finite.
    unsigned played = 0, play_errors = 0, nonfinite = 0;
    std::map<int, bool> unknown_easing;
    for (const TimelineDef& t : m.timelines) {
        AnimationState rt(RuntimeOptions(), ch.easing);
        if (!rt.Bind(m).ok() || !rt.PlayTimeline(t.label).ok()) {
            play_errors++;
            continue;
        }
        played++;
        double limit = std::min(t.Duration() + 30.0, 3000.0);
        for (double fr = 0; fr < limit; fr += 1.0) {
            rt.AdvanceFrames(1.0);
            if ((int)fr % 7 == 0)
                for (const AnimationState::VariableValue& vv : rt.Snapshot())
                    if (!std::isfinite(vv.value)) nonfinite++;
        }
        for (int c : rt.diagnostics().unknown_easing_codes) unknown_easing[c] = true;
    }

    // Scene: every motion at frame 0 and half its length.
    unsigned scene_nodes = 0, scene_unresolved = 0, scene_truncated = 0, scene_errors = 0;
    for (const ObjectDef& o : m.objects)
        for (const MotionDef& mo : o.motions)
            for (double fr : {0.0, mo.last_time / 2}) {
                SceneSnapshot snap;
                if (!EvaluateStaticPose(m, o.name, mo.name, fr, snap).ok()) {
                    scene_errors++;
                    continue;
                }
                scene_nodes += (unsigned)snap.nodes.size();
                scene_unresolved += snap.unresolved;
                scene_truncated += snap.truncated;
            }

    std::snprintf(tmp, sizeof tmp,
                  ",\"adapter\":\"%s\",\"canvas\":[%g,%g],\"variables\":%zu,\"timelines\":%zu,\"tracks\":%u,"
                  "\"keys\":%u,\"text_keys\":%u,\"controllers\":%zu,\"warnings\":%zu",
                  m.adapter_id.c_str(), m.canvas.width, m.canvas.height, m.variables.size(), m.timelines.size(),
                  tracks, keys, text_keys, m.controllers.size(), m.warnings.size());
    out += tmp;
    std::snprintf(tmp, sizeof tmp,
                  ",\"objects\":%zu,\"motions\":%u,\"layers\":%u,\"layer_frames\":%u,\"content_frames\":%u,"
                  "\"mesh_bp_frames\":%u,\"refs_unresolved\":%u",
                  m.objects.size(), motions, s.layers, s.layer_frames, s.content_frames, s.mesh_bp, s.unresolved);
    out += tmp;
    std::snprintf(tmp, sizeof tmp,
                  ",\"sources\":%zu,\"icons\":%u,\"icons_rl\":%u,\"icons_atlas\":%u,\"textures\":%u,"
                  "\"textures_ok\":%u,\"texture_errors\":\"%s\"",
                  m.sources.size(), icons, icons_rl, icons_atlas, textures, textures_ok, texture_errors.c_str());
    out += tmp;
    std::snprintf(tmp, sizeof tmp,
                  ",\"runtime\":{\"played\":%u,\"errors\":%u,\"nonfinite\":%u},"
                  "\"scene\":{\"nodes\":%u,\"unresolved\":%u,\"truncated\":%u,\"errors\":%u}",
                  played, play_errors, nonfinite, scene_nodes, scene_unresolved, scene_truncated, scene_errors);
    out += tmp;
    auto map_json = [](const char* name, const auto& mp) {
        std::string r = std::string(",\"") + name + "\":{";
        bool first = true;
        for (const auto& kv : mp) {
            if (!first) r += ",";
            first = false;
            std::string k;
            if constexpr (std::is_same<typename std::decay<decltype(kv.first)>::type, std::string>::value) k = kv.first;
            else k = std::to_string(kv.first);
            r += "\"" + k + "\":" + std::to_string((unsigned)kv.second);
        }
        return r + "}";
    };
    out += ",\"textures_unsupported\":" + std::to_string(textures_unsupported);
    out += map_json("texture_formats", texture_formats);
    out += map_json("refs", s.refs);
    out += map_json("layer_frame_types", s.layer_frame_types);
    out += map_json("easing_codes", easing);
    out += ",\"unknown_easing_codes\":[";
    bool first = true;
    for (const auto& kv : unknown_easing) {
        if (!first) out += ",";
        first = false;
        out += std::to_string(kv.first);
    }
    out += "],\"unknown_root_keys\":[";
    for (size_t i = 0; i < m.unknown_root_keys.size(); i++)
        out += (i ? ",\"" : "\"") + Esc(m.unknown_root_keys[i]) + "\"";
    out += "],\"first_warnings\":[";
    for (size_t i = 0; i < m.warnings.size() && i < 3; i++) out += (i ? ",\"" : "\"") + Esc(m.warnings[i]) + "\"";
    out += "]}";
    std::printf("%s\n", out.c_str());
    return 0;
}
