// kk4s E-mote runtime: easing curves.
//
// The curves are kk4s' own, defined mathematically. Which stored easing code
// means which curve is format knowledge and lives in an EasingMap supplied by
// the adapter; every entry says whether the mapping was verified.
#pragma once

#include <string>
#include <vector>

namespace kk4s {
namespace emote {

enum class Curve {
    Linear,
    Step,  // holds the start value until the end
    QuadIn, QuadOut, QuadInOut,
    CubicIn, CubicOut, CubicInOut,
    SineIn, SineOut, SineInOut,
};

// Progress for t in [0,1] (clamped). f(0) = 0 and f(1) = 1 for every curve
// except Step, which is 0 until t reaches 1.
double EvalCurve(Curve c, double t);
const char* CurveName(Curve c);

class EasingMap {
public:
    struct Entry {
        int code = 0;
        Curve curve = Curve::Linear;
        bool verified = false;  // false: inferred, not checked against the original player
    };
    void Set(int code, Curve curve, bool verified);
    const Entry* Find(int code) const;
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace emote
}  // namespace kk4s
