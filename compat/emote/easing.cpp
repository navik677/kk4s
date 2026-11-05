// kk4s E-mote runtime: easing curves. See easing.h.
#include "emote/easing.h"

#include <cmath>

namespace kk4s {
namespace emote {

namespace {
const double kPi = 3.14159265358979323846;
}

double EvalCurve(Curve c, double t) {
    if (!(t > 0)) t = 0;  // also maps NaN to 0
    if (t > 1) t = 1;
    switch (c) {
    case Curve::Linear: return t;
    case Curve::Step: return t >= 1 ? 1 : 0;
    case Curve::QuadIn: return t * t;
    case Curve::QuadOut: return t * (2 - t);
    case Curve::QuadInOut: return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
    case Curve::CubicIn: return t * t * t;
    case Curve::CubicOut: { double u = 1 - t; return 1 - u * u * u; }
    case Curve::CubicInOut: { double u = 1 - t; return t < 0.5 ? 4 * t * t * t : 1 - 4 * u * u * u; }
    case Curve::SineIn: return 1 - std::cos(t * kPi / 2);
    case Curve::SineOut: return std::sin(t * kPi / 2);
    case Curve::SineInOut: return 0.5 * (1 - std::cos(t * kPi));
    }
    return t;
}

const char* CurveName(Curve c) {
    switch (c) {
    case Curve::Linear: return "linear";
    case Curve::Step: return "step";
    case Curve::QuadIn: return "quad-in";
    case Curve::QuadOut: return "quad-out";
    case Curve::QuadInOut: return "quad-in-out";
    case Curve::CubicIn: return "cubic-in";
    case Curve::CubicOut: return "cubic-out";
    case Curve::CubicInOut: return "cubic-in-out";
    case Curve::SineIn: return "sine-in";
    case Curve::SineOut: return "sine-out";
    case Curve::SineInOut: return "sine-in-out";
    }
    return "?";
}

void EasingMap::Set(int code, Curve curve, bool verified) {
    for (Entry& e : entries_)
        if (e.code == code) {
            e.curve = curve;
            e.verified = verified;
            return;
        }
    Entry e;
    e.code = code;
    e.curve = curve;
    e.verified = verified;
    entries_.push_back(e);
}

const EasingMap::Entry* EasingMap::Find(int code) const {
    for (const Entry& e : entries_)
        if (e.code == code) return &e;
    return nullptr;
}

}  // namespace emote
}  // namespace kk4s
