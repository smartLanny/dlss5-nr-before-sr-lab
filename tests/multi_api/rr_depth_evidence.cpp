// Actual Windows memory-copy boundary, CPU only. No D3D device or model.
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <limits>
using NgxResult = int;
constexpr NgxResult kNgxSuccess = 1;
struct NgxParams { void *matrix; bool present = true; };
struct FeatureRecord { int rr_depth_type = 0; unsigned long long generation = 1; };
using GetVoidFn = NgxResult (*)(const NgxParams *, const char *, void **);
unsigned getters = 0;
NgxResult Get(const NgxParams *params, const char *key, void **out) {
    ++getters;
    assert(std::strcmp(key, "ViewToClipMatrix") == 0);
    *out = params->matrix;
    return params->present ? kNgxSuccess : 0;
}
template<class T> T ParamSlot(const NgxParams *, unsigned slot) {
    assert(slot == 8); return &Get;
}
namespace nr_post_settings {
struct Receipt {
    std::atomic<unsigned long long> sequence{0}, generation{0}, tick{0};
    std::atomic<int> state{0}; std::atomic<float> values[16]{};
} projection;
}
ULONGLONG test_tick = 10000;
ULONGLONG EvidenceTick() { return test_tick; }
#define GetTickCount64 EvidenceTick
#include "../../src/interop/nr_rr_depth_evidence.inl"
#undef GetTickCount64
int main() {
    float matrix[16]{1,0,0,0, 0,1,0,0, 0,0,0,1, 0,0,0.1f,0};
    NgxParams params{matrix}; FeatureRecord feature{};
    ObserveRrDepthProjection(&params, feature);
    auto &r = nr_post_settings::projection;
    assert(r.state == 4 && r.sequence == 2 && r.values[14] == 0.1f);
    for (unsigned i=0; i<5000; ++i) ObserveRrDepthProjection(&params, feature);
    assert(getters == 1); // at most once per second for an unchanged generation
    params.matrix = reinterpret_cast<void *>(1); ++feature.generation;
    ObserveRrDepthProjection(&params, feature);
    assert(r.state == 2);
    for (auto &v : r.values) assert(v == 0.0f);
    params.matrix = matrix; matrix[4] = std::numeric_limits<float>::quiet_NaN();
    ++feature.generation; ObserveRrDepthProjection(&params, feature);
    assert(r.state == 3 && r.values[14] == 0.0f);
    params.present = false; ++feature.generation;
    ObserveRrDepthProjection(&params, feature); assert(r.state == 1);
    const auto sequence = r.sequence.load();
    feature.rr_depth_type = 1; ObserveRrDepthProjection(&params, feature);
    assert(r.sequence == sequence && getters == 4);
    std::puts("PASS bounded RR projection copy, invalid pointer, nonfinite, absent and HW-depth paths; no GPU");
}
