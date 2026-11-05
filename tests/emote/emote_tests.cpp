// Unit tests for the kk4s E-mote runtime (compat/emote). Synthetic data only.
//
//   g++ -std=c++17 -O2 -I compat -I tests/emote tests/emote/emote_tests.cpp
//       compat/emote/*.cpp compat/emote/profiles/*.cpp compat/emote/adapters/*.cpp
//       compat/psb/psb.cpp -lz -o emote_tests
//
// Exit code 0 = all checks passed.
#include "emote/emote.h"
#include "psb_writer.h"

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace kk4s::emote;
using namespace psbw;

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_failed++;                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                     \
    } while (0)
#define CHECK_ERR(st, e)                                                                             \
    do {                                                                                             \
        g_checks++;                                                                                  \
        if ((st).code != (e)) {                                                                      \
            g_failed++;                                                                              \
            std::printf("FAIL %s:%d: expected %s, got %s\n", __FILE__, __LINE__, ErrorName(e),       \
                        (st).ToString().c_str());                                                    \
        }                                                                                            \
    } while (0)
#define CHECK_OK(st) CHECK_ERR(st, Error::None)
#define CHECK_NEAR(a, b) CHECK(std::fabs((double)(a) - (double)(b)) < 1e-9)

static const char* const kNeko = "emote.xorshift128.nekopara-vol1";
static const char* const kNekoPlayerSha = "536361228be24fb47de73e7c72409060577e8b926fefa24e1ffd5854e6779541";

static Bytes Encrypt(Bytes data, const CipherProfile& p) {
    PsbHeader h;
    ParsePsbHeader(data.data(), data.size(), h);
    p.Apply(data.data(), data.size(), h);
    return data;
}

static const CipherProfile& Neko() { return *CipherRegistry::Builtin().Find(kNeko); }

static std::unique_ptr<CipherProfile> CustomProfile(const std::string& id, uint32_t key) {
    ProfileInfo info;
    info.id = id;
    info.psb_versions = {2};
    Xorshift128Params p;
    p.seed_x = 123456789u;
    p.seed_y = 362436069u;
    p.seed_z = 521288629u;
    p.key_w = key;
    return std::unique_ptr<CipherProfile>(new Xorshift128RegionProfile(info, p));
}

static Value SmallRoot() {
    return O({{"spec", S("krkr")},
              {"n", I(-300)},
              {"f", D(0.5)},
              {"d", D(3.141592653589793)},
              {"list", A({I(1), S("two"), A({N()}), O({{"deep", Value::MakeBool(true)}})})},
              {"res", R(0)},
              {"utf8", S("\xE6\x97\xA5\xE6\x9C\xAC \xD1\x82\xD0\xB5\xD1\x81\xD1\x82")}});
}

// ---------------------------------------------------------------- cipher

static void TestKeystream() {
    Xorshift128Keystream ks(123456789u, 362436069u, 521288629u, 742877301u);
    const uint8_t want[16] = {0x8d, 0x3b, 0xad, 0xf5, 0xa0, 0x6d, 0x5f, 0x32,
                              0xcd, 0x37, 0x1e, 0xbc, 0xfb, 0xe4, 0x0d, 0xab};
    bool same = true;
    for (uint8_t w : want) same = same && ks.Next() == w;
    CHECK(same);

    // Independent naive loop (as in research/emote-psb/emote_psb.py).
    Xorshift128Keystream a(123456789u, 362436069u, 521288629u, 742877301u);
    uint32_t x = 123456789u, y = 362436069u, z = 521288629u, w = 742877301u, cur = 0;
    const size_t n = 1 << 20;
    bool eq = true;
    for (size_t i = 0; i < n; i++) {
        if (cur == 0) {
            uint32_t t = x ^ (x << 11);
            x = y; y = z; z = w;
            w = w ^ (w >> 19) ^ t ^ (t >> 8);
            cur = w;
        }
        uint8_t b = (uint8_t)cur;
        cur >>= 8;
        if (a.Next() != b) { eq = false; break; }
    }
    CHECK(eq);
    // A word whose high byte is 0 yields fewer than four bytes: more words than n/4.
    CHECK(a.words_drawn() > n / 4);
}

static void TestCipherRegion() {
    Built b = Build(SmallRoot(), 2, {Bytes(10, 0x41)});
    Bytes enc = Encrypt(b.bytes, Neko());
    CHECK(enc.size() == b.bytes.size());
    bool outside_same = true, inside_changed = false;
    for (size_t i = 0; i < enc.size(); i++) {
        bool in = i >= b.header_len && i < b.off_chunk_offsets;
        if (!in && enc[i] != b.bytes[i]) outside_same = false;
        if (in && enc[i] != b.bytes[i]) inside_changed = true;
    }
    CHECK(outside_same);
    CHECK(inside_changed);
    CHECK(Encrypt(enc, Neko()) == b.bytes);  // involution
    ByteRegion r = Neko().Region([&] { PsbHeader h; ParsePsbHeader(enc.data(), enc.size(), h); return h; }());
    CHECK(r.begin == b.header_len && r.end == b.off_chunk_offsets);
}

// ---------------------------------------------------------------- PSB core

static void TestLoadPlainAndEncrypted() {
    Built b = Build(SmallRoot(), 2, {Bytes{1, 2, 3}});
    Document d;
    CHECK_OK(d.Load(b.bytes.data(), b.bytes.size()));
    CHECK(d.cipher().confidence == Confidence::PlainContainer);
    CHECK(d.cipher().profile_id.empty());
    CHECK(d.version_support() == VersionSupport::Verified);
    CHECK(d.root().GetString("spec", "") == "krkr");
    CHECK(d.root().GetInt("n", 0) == -300);
    CHECK_NEAR(d.root().GetNumber("f", 0), 0.5);
    CHECK_NEAR(d.root().GetNumber("d", 0), 3.141592653589793);
    const Value* deep = d.root().FindPath("list/3/deep");
    CHECK(deep && deep->is_bool() && deep->bool_value());
    CHECK(d.root().FindPath("list/9") == nullptr);
    CHECK(d.root().GetString("utf8", "") == "\xE6\x97\xA5\xE6\x9C\xAC \xD1\x82\xD0\xB5\xD1\x81\xD1\x82");
    ResourceSpan sp;
    CHECK(d.Resource(0, sp) && sp.size == 3 && sp.data[2] == 3);
    CHECK(!d.Resource(5, sp));

    Bytes enc = Encrypt(b.bytes, Neko());
    Document e;
    CHECK_OK(e.Load(enc.data(), enc.size()));
    CHECK(e.cipher().profile_id == kNeko);
    CHECK(e.cipher().confidence == Confidence::StructureOnly);
    CHECK(e.root().GetInt("n", 0) == -300);
    CHECK(e.plain_adler32() == d.plain_adler32());

    LoadOptions ev;
    std::string upper = kNekoPlayerSha;
    for (char& c : upper) c = (char)toupper((unsigned char)c);
    ev.binary_sha256 = {"00", upper};
    Document e2;
    CHECK_OK(e2.Load(enc.data(), enc.size(), ev));
    CHECK(e2.cipher().confidence == Confidence::BinaryEvidence);

    LoadOptions pin;
    pin.pinned_profile = kNeko;
    Document e3;
    CHECK_OK(e3.Load(enc.data(), enc.size(), pin));
    CHECK(e3.cipher().confidence == Confidence::Pinned);

    // Pinning a profile onto a plain file decrypts it into garbage: refused.
    Document e4;
    CHECK_ERR(e4.Load(b.bytes.data(), b.bytes.size(), pin), Error::ProfileMismatch);
    LoadOptions bad;
    bad.pinned_profile = "no.such.profile";
    CHECK_ERR(e4.Load(enc.data(), enc.size(), bad), Error::UnknownCipherProfile);

    // Structural detection off: without binary evidence nothing is tried.
    LoadOptions strict;
    strict.allow_structural_detection = false;
    CHECK_ERR(e4.Load(enc.data(), enc.size(), strict), Error::UnknownCipherProfile);
    strict.binary_sha256 = {kNekoPlayerSha};
    CHECK_OK(e4.Load(enc.data(), enc.size(), strict));
    CHECK(e4.cipher().confidence == Confidence::BinaryEvidence);
}

static void TestUnknownAndCustomProfiles() {
    Built b = Build(SmallRoot(), 2, {Bytes{1}});
    std::unique_ptr<CipherProfile> other = CustomProfile("test.other-key", 1234567u);
    Bytes enc = Encrypt(b.bytes, *other);
    Document d;
    Status st = d.Load(enc.data(), enc.size());
    CHECK_ERR(st, Error::UnknownCipherProfile);
    CHECK(st.message.find(kNeko) != std::string::npos);  // says what was tried
    CHECK(d.root().is_undefined());                       // nothing half-loaded

    // A new build is a new registered profile; the core is unchanged.
    CipherRegistry reg;
    CHECK_OK(reg.Register(CustomProfile("test.other-key", 1234567u)));
    CHECK_ERR(reg.Register(CustomProfile("test.other-key", 1u)), Error::InvalidArgument);
    LoadOptions o;
    o.registry = &reg;
    CHECK_OK(d.Load(enc.data(), enc.size(), o));
    CHECK(d.cipher().profile_id == "test.other-key");

    // Two profiles that both validate: refused instead of picking one.
    CipherRegistry amb;
    amb.Register(CustomProfile("a", 742877301u));
    amb.Register(CustomProfile("b", 742877301u));
    Bytes neko = Encrypt(b.bytes, Neko());
    o.registry = &amb;
    CHECK_ERR(d.Load(neko.data(), neko.size(), o), Error::AmbiguousCipherProfile);
}

static void TestVersionsAndLayout() {
    Document d;
    Built v3 = Build(SmallRoot(), 3, {Bytes{1}});
    CHECK_OK(d.Load(v3.bytes.data(), v3.bytes.size()));
    CHECK(d.version_support() == VersionSupport::ParsedUnverified);
    LoadOptions o;
    o.allow_unverified_versions = false;
    CHECK_ERR(d.Load(v3.bytes.data(), v3.bytes.size(), o), Error::UnsupportedVersion);
    // The NekoPara profile was only seen with v2: an encrypted v3 file is not guessed.
    Bytes v3enc = Encrypt(v3.bytes, *CustomProfile("tmp", 742877301u));
    CHECK_ERR(d.Load(v3enc.data(), v3enc.size()), Error::UnknownCipherProfile);

    Built v7 = Build(SmallRoot(), 7, {Bytes{1}});
    CHECK_ERR(d.Load(v7.bytes.data(), v7.bytes.size()), Error::UnsupportedVersion);

    Built b = Build(SmallRoot(), 2, {Bytes{1}});
    Bytes flag = b.bytes;
    flag[6] = 1;
    CHECK_ERR(d.Load(flag.data(), flag.size()), Error::UnsupportedLayout);

    // header_len != off_names: outside the verified layout, refused.
    Bytes enc = Encrypt(b.bytes, Neko());
    enc[8] = 0x2C;
    Status st = d.Load(enc.data(), enc.size());
    CHECK_ERR(st, Error::UnknownCipherProfile);

    CHECK_ERR(d.Load(nullptr, 5), Error::InvalidArgument);
    const uint8_t junk[] = {'X', 'Y', 'Z', 0, 1, 2};
    CHECK_ERR(d.Load(junk, sizeof junk), Error::NotPsb);
    CHECK_ERR(d.Load(b.bytes.data(), 20), Error::Truncated);
    CHECK_ERR(d.Load(b.bytes.data(), 0), Error::NotPsb);

    // mdf (zlib) wrapper around a plain container
    uLongf cap = compressBound((uLong)b.bytes.size());
    Bytes z(8 + cap);
    std::memcpy(z.data(), "mdf\0", 4);
    uint32_t n = (uint32_t)b.bytes.size();
    std::memcpy(z.data() + 4, &n, 4);
    compress(z.data() + 8, &cap, b.bytes.data(), (uLong)b.bytes.size());
    z.resize(8 + cap);
    CHECK_OK(d.Load(z.data(), z.size()));
    CHECK(d.mdf_wrapped());
    Bytes zbad = z;
    zbad[12] ^= 0xFF;
    CHECK(!d.Load(zbad.data(), zbad.size()).ok());
}

static void TestCorruption() {
    Built b = Build(SmallRoot(), 2, {Bytes(40, 7)});
    Bytes enc = Encrypt(b.bytes, Neko());
    int ok = 0, err = 0;
    Document d;
    for (size_t n = 0; n < enc.size(); n += 3) {
        Status st = d.Load(enc.data(), n);
        st.ok() ? ok++ : err++;
    }
    for (size_t i = 0; i < enc.size(); i++) {
        for (uint8_t mask : {0x01, 0x80, 0xFF}) {
            Bytes c = enc;
            c[i] ^= mask;
            Status st = d.Load(c.data(), c.size());
            st.ok() ? ok++ : err++;
            if (st.ok()) CHECK(d.root().is_object());
        }
    }
    CHECK(err > 0);
    std::printf("  corruption: %d loads rejected, %d accepted, no crash\n", err, ok);
}

// ---------------------------------------------------------------- adapter

static Value Kf(int t, Value v, int easing) {
    return O({{"content", O({{"easing", I(easing)}, {"value", std::move(v)}})}, {"time", I(t)}, {"type", I(2)}});
}
static Value End(int t) { return O({{"content", N()}, {"time", I(t)}, {"type", I(0)}}); }

static Value Content(bool pair, const std::string& path, const std::string& src, const std::string& icon) {
    std::vector<Value::Member> m;
    if (pair) {
        m.push_back({"src", S(src)});
        if (!icon.empty()) m.push_back({"icon", S(icon)});
    } else {
        m.push_back({"src", S(path)});
    }
    m.push_back({"coord", A({I(1), D(2.5), I(0)})});
    m.push_back({"angle", I(-8)});
    m.push_back({"mask", I(33554433)});
    m.push_back({"mesh", O({{"bp", N()}, {"cc", N()}})});
    m.push_back({"ox", I(0)});
    m.push_back({"oy", I(0)});
    return O(std::move(m));
}

static Value LayerV(const std::string& label, std::vector<Value> frames, std::vector<Value> children = {}) {
    return O({{"label", S(label)},
              {"type", I(0)},
              {"parameterize", N()},
              {"meshDivision", I(10)},
              {"meshTransform", I(0)},
              {"inheritMask", I(2044)},
              {"transformOrder", A({I(0), I(3), I(2), I(1)})},
              {"frameList", A(std::move(frames))},
              {"children", A(std::move(children))}});
}

static Value Frame(int t, Value content) { return O({{"content", std::move(content)}, {"time", I(t)}, {"type", I(3)}}); }

static Value EmoteRoot(bool pair) {
    Value main_layers = A({LayerV("L1",
                                  {Frame(0, Content(pair, "src/tex/eye", "tex", "eye")), End(10)},
                                  {LayerV("L2", {Frame(0, Content(pair, "motion/body/sub", "body", "sub"))})})});
    Value sub_layers = A({LayerV("L3", {Frame(0, Content(pair, "blank/32:32:16:16", "blank", "32:32:16:16"))}),
                          LayerV("L4", {Frame(0, Content(pair, "motion/body/main", "body", "main"))})});
    auto motion = [](Value layers, std::vector<Value> params) {
        return O({{"lastTime", I(61)}, {"loopTime", I(-1)}, {"layer", std::move(layers)},
                  {"parameter", A(std::move(params))}, {"priority", A({})}, {"type", I(0)}});
    };
    Value param = O({{"discretization", I(0)}, {"division", I(20)}, {"enabled", I(1)}, {"id", S("head_UD")},
                     {"rangeBegin", I(-10)}, {"rangeEnd", I(10)}});
    Value tex_source = O({{"type", I(0)},
                          {"metadata", N()},
                          {"icon", O({{"eye", O({{"width", I(4)}, {"height", I(2)}, {"originX", I(2)}, {"originY", I(1)},
                                                 {"left", I(0)}, {"top", I(0)}, {"attr", I(0)}})},
                                      {"solo", O({{"width", I(3)}, {"height", I(3)}, {"originX", D(1.5)}, {"originY", D(1.5)},
                                                  {"compress", S("RL")}, {"pixel", R(1)},
                                                  {"clip", O({{"left", D(0.25)}, {"top", I(0)}, {"right", I(1)}, {"bottom", I(1)}})}})}})},
                          {"texture", O({{"type", S("RGBA8")}, {"width", I(2)}, {"height", I(2)}, {"truncated_width", I(2)},
                                         {"truncated_height", I(2)}, {"pixel", R(0)}, {"mipMapLevel", I(1)}, {"mipMap", A({})}})}});
    Value variables = A({O({{"label", S("head_LR")},
                            {"frameList", A({O({{"label", S("left")}, {"frame", I(-30)}}),
                                             O({{"label", S("right")}, {"frame", I(30)}})})}}),
                         O({{"label", S("face_LR")},
                            {"frameList", A({O({{"label", S("left")}, {"frame", I(-5)}}),
                                             O({{"label", S("right")}, {"frame", I(5)}})})}})});
    Value timelines = A({
        O({{"label", S("wave")}, {"diff", I(0)}, {"loopBegin", I(0)}, {"loopEnd", I(20)}, {"lastTime", I(-1)},
           {"variableList", A({O({{"label", S("head_LR")}, {"frameList", A({Kf(0, I(0), 0), Kf(10, I(30), 0), Kf(20, I(0), 0), End(30)})}}),
                               O({{"label", S("face_LR")}, {"frameList", A({Kf(0, S("right"), 0), End(5)})}}),
                               O({{"label", S("mouth")}, {"frameList", A({Kf(0, S("20"), 0), End(5)})}})})}}),
        O({{"label", S("nod")}, {"diff", I(1)}, {"loopBegin", I(-1)}, {"loopEnd", I(-1)}, {"lastTime", I(-1)},
           {"variableList", A({O({{"label", S("head_UD")}, {"frameList", A({Kf(0, I(0), 0), Kf(10, D(5.5), 1), End(10)})}})})}}),
        O({{"label", S("once")}, {"loopBegin", I(-1)}, {"loopEnd", I(-1)}, {"lastTime", I(-1)},
           {"variableList", A({O({{"label", S("head_LR")}, {"frameList", A({Kf(0, I(0), 0), Kf(4, I(8), 0), End(4)})}})})}}),
    });
    return O({{"id", S("motion")},
              {"version", D((double)3.03f)},
              {"label", S("template")},
              {"spec", S(pair ? "win" : "krkr")},
              {"screenSize", O({{"width", I(800)}, {"height", I(1080)}, {"originX", I(0)}, {"originY", I(0)}})},
              {"metadata", O({{"format", S("emote")},
                              {"version", I(1)},
                              {"base", O({{"chara", S("body")}, {"motion", S("main")}})},
                              {"variableList", variables},
                              {"timelineControl", timelines},
                              {"eyeControl", A({O({{"label", S("face_LR")}, {"enabled", I(1)}, {"var_lr", S("head_LR")}})})},
                              {"mirrorControl", O({{"variableMatchList", A({S("_LR")})}})}})},
              {"object", O({{"body", O({{"metadata", N()}, {"type", I(0)},
                                       {"motion", O({{"main", motion(main_layers, {param})}, {"sub", motion(sub_layers, {})}})}})}})},
              {"source", O({{"tex", tex_source}})},
              {"stereovisionProfile", O({})},
              {"futureKey", I(5)}});
}

static void TestAdapter() {
    for (int pair = 0; pair < 2; pair++) {
        CharacterModel m;
        CHECK_OK(AdapterRegistry::Builtin().BuildModel(EmoteRoot(pair != 0), m));
        CHECK(m.adapter_id == "emote-v3.03");
        CHECK(m.spec == (pair ? "win" : "krkr") && m.format == "emote" && m.format_version == 1);
        CHECK(std::fabs(m.content_version - 3.03) < 1e-6);
        CHECK(m.canvas.width == 800 && m.canvas.height == 1080);
        CHECK(m.base_object == "body" && m.base_motion == "main");
        CHECK(m.unknown_root_keys.size() == 1 && m.unknown_root_keys[0] == "futureKey");
        const VariableDef* hl = m.FindVariable("head_LR");
        CHECK(hl && hl->poses.size() == 2 && hl->poses[1].label == "right" && hl->poses[1].value == 30);
        const VariableDef* ud = m.FindVariable("head_UD");  // only a motion parameter
        CHECK(ud && ud->has_range && ud->range_begin == -10 && ud->range_end == 10);
        const TimelineDef* wave = m.FindTimeline("wave");
        CHECK(wave && !wave->diff && wave->loop_begin == 0 && wave->loop_end == 20 && wave->tracks.size() == 3);
        CHECK(wave && wave->tracks[0].keys.size() == 3 && wave->tracks[0].has_end && wave->tracks[0].end_time == 30);
        CHECK(wave && wave->Duration() == 30);
        CHECK(wave && wave->tracks[1].keys[0].value.kind == KeyValue::Kind::Text);
        const TimelineDef* nod = m.FindTimeline("nod");
        CHECK(nod && nod->diff && nod->tracks[0].keys[1].easing_code == 1);
        CHECK(m.warnings.empty());

        const MotionDef* mo = m.FindMotion("body", "main");
        CHECK(mo && mo->layers.size() == 1 && mo->parameters.size() == 1 && mo->last_time == 61);
        const LayerDef& l1 = mo->layers[0];
        CHECK(l1.label == "L1" && l1.frames.size() == 2 && l1.children.size() == 1);
        CHECK(l1.frames[0].has_content && !l1.frames[1].has_content && l1.frames[1].raw_type == 0);
        const LayerFrameContent& c = l1.frames[0].content;
        CHECK(c.src.kind == SourceRef::Kind::Icon && c.src.a == "tex" && c.src.b == "eye");
        CHECK(c.has_coord && c.coord[0] == 1 && c.coord[1] == 2.5 && c.has_angle && c.angle == -8);
        CHECK(c.has_mesh && c.mesh_bp.empty() && c.mask == 33554433);
        const LayerFrameContent& c2 = l1.children[0].frames[0].content;
        CHECK(c2.src.kind == SourceRef::Kind::Motion && c2.src.a == "body" && c2.src.b == "sub");
        const MotionDef* sub = m.FindMotion("body", "sub");
        CHECK(sub && sub->layers[0].frames[0].content.src.kind == SourceRef::Kind::Blank &&
              sub->layers[0].frames[0].content.src.detail == "32:32:16:16");

        const SourceDef* s = m.FindSource("tex");
        CHECK(s && s->has_texture && s->texture.format == "RGBA8" && s->texture.pixel_resource == 0);
        const IconDef* eye = m.FindIcon("tex", "eye");
        CHECK(eye && eye->in_atlas && eye->width == 4 && eye->origin_x == 2);
        const IconDef* solo = m.FindIcon("tex", "solo");
        CHECK(solo && !solo->in_atlas && solo->compress == "RL" && solo->pixel_resource == 1 && solo->has_clip &&
              solo->clip_left == 0.25);

        bool eye_ctrl = false, mirror = false;
        for (const ControllerDef& cd : m.controllers) {
            if (cd.kind == "eye") eye_ctrl = cd.variables.size() == 2;  // label face_LR + var_lr head_LR
            if (cd.kind == "mirror") mirror = cd.variables.empty();
        }
        CHECK(eye_ctrl && mirror);
    }

    CharacterModel m;
    CHECK_ERR(AdapterRegistry::Builtin().BuildModel(SmallRoot(), m), Error::UnsupportedSchema);
    Value v2 = EmoteRoot(false);
    for (Value::Member& mem : v2.members())
        if (mem.first == "metadata")
            for (Value::Member& mm : mem.second.members())
                if (mm.first == "version") mm.second = I(2);
    CHECK_ERR(AdapterRegistry::Builtin().BuildModel(v2, m), Error::UnsupportedSchema);
    // Unseen content version or asset spec: refused, not guessed.
    for (int what = 0; what < 3; what++) {
        Value v = EmoteRoot(false);
        for (Value::Member& mem : v.members()) {
            if (what == 0 && mem.first == "version") mem.second = D(3.5);
            if (what == 1 && mem.first == "spec") mem.second = S("ems");
            if (what == 2 && mem.first == "version") mem.second = S("3.03");
        }
        Status st = AdapterRegistry::Builtin().BuildModel(v, m);
        CHECK_ERR(st, Error::UnsupportedSchema);
    }

    SourceRef r = ParseSourceRef("strange/thing", nullptr, m);
    CHECK(r.kind == SourceRef::Kind::Unknown);
    CHECK(ParseSourceRef("", nullptr, m).kind == SourceRef::Kind::None);
    CHECK(ParseSourceRef("layout", nullptr, m).kind == SourceRef::Kind::Layout);
    CHECK(ParseSourceRef("shape/point", nullptr, m).kind == SourceRef::Kind::Shape);
    std::string ic = "x";
    CHECK(ParseSourceRef("nowhere", &ic, m).kind == SourceRef::Kind::Unknown);
}

// ---------------------------------------------------------------- easing / runtime

static void TestEasing() {
    const Curve all[] = {Curve::Linear, Curve::QuadIn, Curve::QuadOut, Curve::QuadInOut, Curve::CubicIn,
                         Curve::CubicOut, Curve::CubicInOut, Curve::SineIn, Curve::SineOut, Curve::SineInOut};
    for (Curve c : all) {
        CHECK_NEAR(EvalCurve(c, 0), 0);
        CHECK_NEAR(EvalCurve(c, 1), 1);
        bool mono = true;
        double prev = 0;
        for (int i = 1; i <= 100; i++) {
            double v = EvalCurve(c, i / 100.0);
            if (v + 1e-12 < prev) mono = false;
            prev = v;
        }
        CHECK(mono);
    }
    CHECK(EvalCurve(Curve::Step, 0.99) == 0 && EvalCurve(Curve::Step, 1) == 1);
    CHECK_NEAR(EvalCurve(Curve::Linear, -1), 0);
    CHECK_NEAR(EvalCurve(Curve::Linear, std::nan("")), 0);
    CHECK_NEAR(EvalCurve(Curve::QuadIn, 0.5), 0.25);
    EasingMap m;
    m.Set(0, Curve::Linear, false);
    m.Set(0, Curve::QuadOut, true);
    CHECK(m.entries().size() == 1 && m.Find(0)->curve == Curve::QuadOut && m.Find(0)->verified);
    CHECK(m.Find(7) == nullptr);
}

static CharacterModel ModelFor(bool pair) {
    CharacterModel m;
    AdapterRegistry::Builtin().BuildModel(EmoteRoot(pair), m);
    return m;
}

static void TestRuntimeVariables() {
    CharacterModel m = ModelFor(false);
    AnimationState st(RuntimeOptions(), AdapterRegistry::Builtin().Find("emote-v3.03")->Easing());
    CHECK_OK(st.Bind(m));
    double v = -1;
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK(v == 0);
    CHECK_OK(st.GetVariable("head_UD", v));  // range -10..10 contains 0
    CHECK(v == 0);
    CHECK_ERR(st.GetVariable("nope", v), Error::NotFound);
    CHECK_ERR(st.SetVariable("nope", 1), Error::NotFound);
    CHECK_ERR(st.SetVariable("head_LR", std::nan("")), Error::InvalidArgument);

    CHECK_OK(st.SetVariable("head_LR", 10));
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK(v == 10);
    CHECK_OK(st.SetVariable("head_LR", 20, 10, 0));  // linear tween 10 -> 20 over 10 frames
    st.AdvanceFrames(5);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 15);
    st.AdvanceFrames(50);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 20);
    CHECK_OK(st.SetVariableCurve("head_LR", 0, 10, Curve::QuadIn));
    st.AdvanceFrames(5);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 20 - 20 * 0.25);
    CHECK_OK(st.SetVariableText("head_LR", "left"));
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK(v == -30);
    CHECK_OK(st.SetVariableText("head_LR", "12.5"));
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK(v == 12.5);
    CHECK_ERR(st.SetVariableText("head_LR", "sideways"), Error::InvalidArgument);

    // Unknown easing: linear + diagnostics by default, an error when asked.
    CHECK_OK(st.SetVariable("head_LR", 0, 4, 99));
    CHECK(st.diagnostics().unknown_easing_codes.size() == 1 && st.diagnostics().unknown_easing_codes[0] == 99);
    RuntimeOptions strict;
    strict.unknown_easing = UnknownEasingPolicy::Error;
    AnimationState st2(strict, EasingMap());
    st2.Bind(m);
    CHECK_ERR(st2.SetVariable("head_LR", 1, 4, 0), Error::UnsupportedFeature);  // empty map: 0 unknown too
    CHECK_OK(st2.SetVariable("head_LR", 1));                                    // immediate needs no curve
    CHECK_ERR(st2.PlayTimeline("wave"), Error::UnsupportedFeature);

    // Milliseconds use the configured frame rate.
    RuntimeOptions fps30;
    fps30.frames_per_second = 30;
    AnimationState st3(fps30, AdapterRegistry::Builtin().Find("emote-v3.03")->Easing());
    st3.Bind(m);
    st3.AdvanceMs(1000);
    CHECK_NEAR(st3.total_frames(), 30);

    // Clamping to the parameter range.
    RuntimeOptions clamp;
    clamp.clamp_to_range = true;
    AnimationState st4(clamp, EasingMap());
    st4.Bind(m);
    st4.SetVariable("head_UD", 50);
    CHECK_OK(st4.GetVariable("head_UD", v));
    CHECK(v == 10);
    CHECK_OK(st4.GetBaseVariable("head_UD", v));
    CHECK(v == 50);
}

static void TestRuntimeTimelines() {
    CharacterModel m = ModelFor(true);
    EasingMap em = AdapterRegistry::Builtin().Find("emote-v3.03")->Easing();
    AnimationState st(RuntimeOptions(), em);
    CHECK_OK(st.Bind(m));
    CHECK(st.TimelineLabels().size() == 3);
    CHECK_ERR(st.PlayTimeline("missing"), Error::NotFound);
    CHECK_ERR(st.StopTimeline("wave"), Error::NotFound);

    CHECK_OK(st.PlayTimeline("wave"));
    CHECK(st.IsTimelinePlaying("wave"));
    CHECK(st.HasVariable("mouth"));  // auto-defined from the track
    CHECK(st.diagnostics().auto_defined_variables.size() == 1);
    double v = 0;
    st.AdvanceFrames(5);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 15);
    CHECK_OK(st.GetVariable("face_LR", v));  // text key "right" = pose value 5
    CHECK(v == 5);
    CHECK_OK(st.GetVariable("mouth", v));  // text key "20" = number
    CHECK(v == 20);
    st.AdvanceFrames(20);  // frame 25 -> loops to 5
    CHECK_NEAR(st.TimelineFrame("wave"), 5);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 15);
    CHECK_OK(st.GetBaseVariable("head_LR", v));
    CHECK(v == 0);  // timeline does not touch the base value

    // Additive (diff) timeline adds on top; unknown easing code 1 reported.
    st.SetVariable("head_UD", 1);
    CHECK_OK(st.PlayTimeline("nod"));
    CHECK(st.diagnostics().unknown_easing_codes.size() == 1 && st.diagnostics().unknown_easing_codes[0] == 1);
    st.AdvanceFrames(4);
    CHECK_OK(st.GetVariable("head_UD", v));
    CHECK_NEAR(v, 1 + 5.5 * 0.4);
    st.AdvanceFrames(6);  // reaches its end: additive contribution is dropped
    CHECK(!st.IsTimelinePlaying("nod"));
    CHECK_OK(st.GetVariable("head_UD", v));
    CHECK_NEAR(v, 1);

    // Non-looping override timeline: holds its final value as the new base.
    CHECK_OK(st.StopTimeline("wave"));
    CHECK_OK(st.PlayTimeline("once"));
    st.AdvanceFrames(2);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 4);
    st.AdvanceFrames(10);
    CHECK(!st.IsTimelinePlaying("once"));
    CHECK_OK(st.GetBaseVariable("head_LR", v));
    CHECK(v == 8);

    // Weight and explicit blend mode.
    PlayOptions half;
    half.weight = 0.5;
    half.loop = false;
    st.SetVariable("head_LR", 10);
    CHECK_OK(st.PlayTimeline("wave", half));
    st.AdvanceFrames(10);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 10 * 0.5 + 30 * 0.5);
    st.StopAllTimelines();
    CHECK(st.PlayingTimelines().empty());

    // Two characters do not share state.
    AnimationState other(RuntimeOptions(), em);
    other.Bind(m);
    other.SetVariable("head_LR", -7);
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK(v == 10);

    // Without auto-definition an undeclared track variable is an error.
    RuntimeOptions noauto;
    noauto.auto_define_variables = false;
    AnimationState st5(noauto, em);
    st5.Bind(m);
    CHECK_ERR(st5.PlayTimeline("wave"), Error::NotFound);

    // A track on its own.
    TimelineTrack tr;
    tr.variable = "x";
    tr.keys.push_back(Keyframe());
    Keyframe k;
    k.time = 10;
    k.value.number = 100;
    tr.keys.push_back(k);
    CHECK(st.EvalTrack(tr, -5, v) && v == 0);
    CHECK(st.EvalTrack(tr, 2.5, v) && std::fabs(v - 25) < 1e-9);
    CHECK(st.EvalTrack(tr, 99, v) && v == 100);
    TimelineTrack empty;
    CHECK(!st.EvalTrack(empty, 0, v));
}

// ---------------------------------------------------------------- scene / render

static void TestScene() {
    CharacterModel m = ModelFor(false);
    SceneSnapshot s;
    CHECK_ERR(EvaluateStaticPose(m, "body", "missing", 0, s), Error::NotFound);
    CHECK_OK(EvaluateStaticPose(m, "body", "main", 0, s));
    // L1, L1/L2, (sub) L3, L4 -> main again: cycle cut
    CHECK(s.nodes.size() == 4);
    CHECK(s.truncated == 1);
    CHECK(s.unresolved == 0);
    CHECK(s.nodes[0].visible && s.nodes[0].icon && s.nodes[0].icon->name == "eye");
    CHECK(s.nodes[1].src.kind == SourceRef::Kind::Motion && s.nodes[1].src_resolved);
    CHECK(s.nodes[2].depth == 2 && s.nodes[2].src.kind == SourceRef::Kind::Blank);
    CHECK_OK(EvaluateStaticPose(m, "body", "main", 10, s));
    CHECK(!s.nodes[0].visible);  // frame type 0 at 10: nothing drawn
    CHECK(ActiveLayerFrame(m.FindMotion("body", "main")->layers[0], -1) == nullptr);

    ParameterDef p;
    p.range_begin = -30;
    p.range_end = 30;
    p.division = 60;
    CHECK_NEAR(ParameterToFrame(p, -30), 0);
    CHECK_NEAR(ParameterToFrame(p, 0), 30);
    CHECK_NEAR(ParameterToFrame(p, 99), 60);
    p.division = 0;
    CHECK_NEAR(ParameterToFrame(p, 5), 0);
}

static void TestRenderContract() {
    RecordingBackend rb;
    TextureDesc d;
    d.width = 2;
    d.height = 2;
    uint8_t px[16] = {0};
    TextureHandle h = rb.CreateTexture(d, px, sizeof px);
    CHECK(h != kNoTexture);
    CHECK(rb.CreateTexture(d, px, 15) == kNoTexture);
    rb.BeginFrame(800, 600);
    DrawCommand c;
    c.texture = h;
    c.vertices.resize(3);
    c.indices = {0, 1, 2};
    rb.Submit(c);
    DrawCommand bad = c;
    bad.indices = {0, 1, 5};
    rb.Submit(bad);
    rb.EndFrame();
    CHECK(rb.frames.size() == 1 && rb.frames[0].commands.size() == 2 && rb.frames[0].ended);
    CHECK(rb.errors.size() == 2);  // size mismatch + bad index
    rb.DestroyTexture(h);
    rb.DestroyTexture(h);
    CHECK(rb.errors.size() == 3);
    std::string why;
    DrawCommand stale = c;
    CHECK(!ValidateDrawCommand(stale, rb.live, &why) && why == "unknown texture handle");

    // Textures from a loaded synthetic character.
    Bytes rgba(16);
    for (size_t i = 0; i < rgba.size(); i++) rgba[i] = (uint8_t)i;
    Built b = Build(EmoteRoot(true), 2, {rgba, Bytes{9, 9, 9}});
    Bytes enc = Encrypt(b.bytes, Neko());
    LoadedCharacter ch;
    CHECK_OK(LoadCharacter(enc.data(), enc.size(), LoadOptions(), ch));
    TexturePixels tp;
    CHECK_OK(GetSourceTexture(ch.document, *ch.model.FindSource("tex"), tp));
    CHECK(tp.size == 16 && tp.data[5] == 5 && tp.desc.width == 2);
    CHECK(rb.CreateTexture(tp.desc, tp.data, tp.size) != kNoTexture);
    CHECK_ERR(GetIconPixels(ch.document, *ch.model.FindIcon("tex", "solo"), tp), Error::UnsupportedFeature);
    CHECK_ERR(GetIconPixels(ch.document, *ch.model.FindIcon("tex", "eye"), tp), Error::InvalidArgument);

    Built wrong = Build(EmoteRoot(true), 2, {Bytes(15), Bytes{9}});
    LoadedCharacter ch2;
    CHECK_OK(LoadCharacter(wrong.bytes.data(), wrong.bytes.size(), LoadOptions(), ch2));
    CHECK_ERR(GetSourceTexture(ch2.document, *ch2.model.FindSource("tex"), tp), Error::CorruptStructure);
}

static void TestEndToEnd() {
    Built b = Build(EmoteRoot(false), 2, {Bytes(16), Bytes{1}});
    Bytes enc = Encrypt(b.bytes, Neko());
    LoadedCharacter ch;
    LoadOptions o;
    o.binary_sha256 = {kNekoPlayerSha};
    CHECK_OK(LoadCharacter(enc.data(), enc.size(), o, ch));
    CHECK(ch.document.cipher().confidence == Confidence::BinaryEvidence);
    CHECK(ch.model.adapter_id == "emote-v3.03");
    CHECK(ch.easing.Find(0) && !ch.easing.Find(0)->verified);
    AnimationState st(RuntimeOptions(), ch.easing);
    CHECK_OK(st.Bind(ch.model));
    CHECK_OK(st.PlayTimeline("wave"));
    st.AdvanceMs(1000.0 / 6);  // 10 frames at 60 fps
    double v = 0;
    CHECK_OK(st.GetVariable("head_LR", v));
    CHECK_NEAR(v, 30);
    std::vector<AnimationState::VariableValue> snap = st.Snapshot();
    CHECK(snap.size() == st.Snapshot().size() && !snap.empty());

    LoadedCharacter plain;
    Built p = Build(SmallRoot(), 2, {Bytes{1}});
    CHECK_ERR(LoadCharacter(p.bytes.data(), p.bytes.size(), LoadOptions(), plain), Error::UnsupportedSchema);
}

int main() {
    struct {
        const char* name;
        void (*fn)();
    } tests[] = {
        {"keystream", TestKeystream},
        {"cipher-region", TestCipherRegion},
        {"load-plain-encrypted", TestLoadPlainAndEncrypted},
        {"unknown-custom-profiles", TestUnknownAndCustomProfiles},
        {"versions-layout", TestVersionsAndLayout},
        {"corruption", TestCorruption},
        {"adapter", TestAdapter},
        {"easing", TestEasing},
        {"runtime-variables", TestRuntimeVariables},
        {"runtime-timelines", TestRuntimeTimelines},
        {"scene", TestScene},
        {"render-contract", TestRenderContract},
        {"end-to-end", TestEndToEnd},
    };
    for (auto& t : tests) {
        int before = g_failed;
        t.fn();
        std::printf("%s %s\n", g_failed == before ? "PASS" : "FAIL", t.name);
    }
    std::printf("emote tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
