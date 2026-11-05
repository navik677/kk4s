// kk4s E-mote runtime: scene evaluation. See scene.h.
#include "emote/scene.h"

#include <algorithm>

namespace kk4s {
namespace emote {

const LayerFrame* ActiveLayerFrame(const LayerDef& layer, double frame) {
    const LayerFrame* best = nullptr;
    for (const LayerFrame& f : layer.frames) {
        if (f.time <= frame)
            best = &f;
        else
            break;
    }
    return best;
}

namespace {

struct Walker {
    const CharacterModel& m;
    double frame;
    int max_depth;
    SceneSnapshot& out;
    std::vector<const MotionDef*> active;  // motions being expanded (cycle check)

    void Motion(const MotionDef& mo, const std::string& path, int depth) {
        if (depth > max_depth || std::find(active.begin(), active.end(), &mo) != active.end()) {
            out.truncated++;
            return;
        }
        active.push_back(&mo);
        for (const LayerDef& l : mo.layers) Layer(l, path, depth);
        active.pop_back();
    }

    void Layer(const LayerDef& l, const std::string& parent, int depth) {
        SceneNode n;
        n.path = parent + "/" + l.label;
        n.depth = depth;
        n.layer = &l;
        n.frame = ActiveLayerFrame(l, frame);
        n.visible = n.frame && n.frame->has_content;
        if (n.visible) {
            n.src = n.frame->content.src;
            switch (n.src.kind) {
            case SourceRef::Kind::Icon:
                n.icon = m.FindIcon(n.src.a, n.src.b);
                n.src_resolved = n.icon != nullptr;
                break;
            case SourceRef::Kind::Motion:
                n.src_resolved = m.FindMotion(n.src.a, n.src.b) != nullptr;
                break;
            case SourceRef::Kind::Unknown:
                n.src_resolved = false;
                break;
            default:
                n.src_resolved = true;  // nothing to look up
                break;
            }
            if (!n.src_resolved) out.unresolved++;
        }
        out.nodes.push_back(n);
        const SceneNode& self = out.nodes.back();
        if (self.visible && self.src.kind == SourceRef::Kind::Motion && self.src_resolved) {
            SourceRef ref = self.src;
            std::string p = self.path;
            Motion(*m.FindMotion(ref.a, ref.b), p, depth + 1);
        }
        std::string p = n.path;
        for (const LayerDef& c : l.children) Layer(c, p, depth + 1);
    }
};

}  // namespace

Status EvaluateStaticPose(const CharacterModel& model, const std::string& object, const std::string& motion,
                          double frame, SceneSnapshot& out, int max_depth) {
    out = SceneSnapshot();
    const MotionDef* mo = model.FindMotion(object, motion);
    if (!mo) return Status::Fail(Error::NotFound, "no motion " + object + "/" + motion);
    Walker w{model, frame, max_depth, out, {}};
    w.Motion(*mo, object + "/" + motion, 0);
    return Status::Ok();
}

double ParameterToFrame(const ParameterDef& p, double value) {
    double span = p.range_end - p.range_begin;
    if (span == 0 || p.division <= 0) return 0;
    double t = (value - p.range_begin) / span;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * p.division;
}

}  // namespace emote
}  // namespace kk4s
