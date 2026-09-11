#include "policy.hpp"
#include <iostream>
#include <cstdlib>
using namespace nr::fullres;
static unsigned checks = 0;
static void check(bool v, const char *why) {
    ++checks; if (!v) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
int main() {
    Settings settings;
    check(!settings.enabled && settings.fallback == Backend::amd, "off/AMD defaults");
    Frame f{1, 1, 10, {3840, 2160}, false};
    NativeMotion n{true, true, true, true, true, f, 9};
    check(select_motion(settings, f, n, 9) == MotionSource::native, "native while experiment off");
    auto reset_frame=f; reset_frame.reset=true;
    auto reset_native=n; reset_native.frame=reset_frame; reset_native.reference_sequence=0;
    check(native_admissible(reset_native,reset_frame,0), "native reset with no history is admissible");
    check(!native_admissible(n,reset_frame,9), "native reset metadata mismatch rejected");
    settings.enabled = true;
    for (Backend b : {Backend::amd, Backend::nvidia}) {
        settings.fallback = b;
        check(select_motion(settings, f, n, 9) == MotionSource::native, "native before either OF");
        for (int mutation = 0; mutation < 11; ++mutation) {
            auto bad = n;
            switch (mutation) {
            case 0: bad.available = false; break;
            case 1: bad.current_call = false; break;
            case 2: bad.lifetime_known = false; break;
            case 3: bad.convention_known = false; break;
            case 4: bad.region_known = false; break;
            case 5: ++bad.frame.stream; break;
            case 6: ++bad.frame.epoch; break;
            case 7: ++bad.frame.sequence; break;
            case 8: ++bad.frame.extent.width; break;
            case 9: ++bad.frame.extent.height; break;
            case 10: ++bad.reference_sequence; break;
            }
            check(!native_admissible(bad, f, 9), "reject native mismatch");
            check(select_motion(settings, f, bad, 9) ==
                (b == Backend::amd ? MotionSource::amd : MotionSource::nvidia), "exact requested fallback");
        }
    }
    settings.fallback = Backend::amd;
    settings.force_synthetic_diagnostic = true;
    check(select_motion(settings, f, n, 9) == MotionSource::amd, "explicit diagnostic bypass");
    settings.enabled = false;
    check(select_motion(settings, f, n, 9) == MotionSource::native, "disabled experiment preserves native despite old diagnostic choice");
    settings.enabled = true;
    settings.fallback = static_cast<Backend>(99);
    check(select_motion(settings, f, {}, 9) == MotionSource::unavailable, "invalid backend");
    check(!completed(UINT64_MAX, 1), "device removed is not completion");
    check(!completed(9, 10) && !completed(10, 0) && completed(10, 10), "exact real fence target");
    for (const Extent e : {Extent{3840,2160}, Extent{2560,1440}, Extent{3440,1440},
                           Extent{1921,1081}, Extent{128,128}, Extent{7680,4320}}) {
        for (unsigned g : {1u, 2u, 4u, 8u}) {
            check(grid_extent(e.width, g) * g >= e.width, "ceil block coverage width");
            check((grid_extent(e.width, g) - 1) * g < e.width, "minimal width grid");
            check(grid_extent(e.height, g) * g >= e.height, "ceil block coverage height");
            check(e == Extent{e.width,e.height}, "input extent unchanged");
        }
    }
    check(grid_extent(UINT32_MAX, 8) == 536870912u, "ceil without integer overflow");
    History h;
    auto p = h.plan(f, Backend::amd);
    check(p.accepted && p.reset && !p.reference, "first frame primes");
    check(!h.commit(f, Backend::amd, false), "unsubmitted capture not accepted");
    check(h.plan(f, Backend::amd).reset, "failed first capture leaves no history");
    check(h.commit(f, Backend::amd, true), "actual submitted frame committed");
    check(!h.plan(f, Backend::amd).accepted, "duplicate rejected");
    f.sequence = 15;
    p = h.plan(f, Backend::amd);
    check(p.accepted && !p.reset && p.reference == 10, "gap pairs with last accepted, not last captured");
    check(!h.commit(f, Backend::amd, false), "dropped capture does not advance");
    f.sequence = 16;
    check(h.plan(f, Backend::amd).reference == 10, "reference retained after backpressure");
    check(h.commit(f, Backend::amd, true), "next submitted frame");
    check(h.plan(Frame{1,1,14,f.extent,false}, Backend::amd).accepted == false, "out of order");
    for (int i = 0; i != 5; ++i) {
        auto cut = f; ++cut.sequence;
        Backend b = Backend::amd;
        if (i == 0) cut.reset = true;
        if (i == 1) ++cut.epoch;
        if (i == 2) ++cut.stream;
        if (i == 3) ++cut.extent.width;
        if (i == 4) b = Backend::nvidia;
        p = h.plan(cut, b);
        check(p.accepted && p.reset && !p.reference, "cut/epoch/stream/extent/backend resets");
    }
    check(!h.plan({}, Backend::amd).accepted, "invalid frame");
    h.clear(); check(h.plan(f, Backend::amd).reset, "clear history");
    std::cout << "PASS " << checks << " portable policy assertions; no GPU or game used\n";
}
