#include "../src/nr_guide_contract.h"

#include <cstdio>
#include <initializer_list>

namespace {
constexpr unsigned kFp16 = 10;
constexpr unsigned kR11Float = 26;
constexpr unsigned kR10Pq = 24;
constexpr unsigned kR8 = 28;

bool Check(bool condition, const char *case_name) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", case_name);
    return condition;
}

NrGuideSignature Base() {
    NrGuideSignature s{};
    s.depth_format = 20;
    s.motion_format = 34;
    s.color_format = kFp16;
    s.motion_bound_width = 1120;
    s.motion_bound_height = 632;
    s.normalized_create_flags = 73;
    s.resample_motion = true;
    return s;
}
}

int main() {
    const auto fp16 = Base();
    auto r11 = fp16;
    r11.color_format = kR11Float;
    bool ok = true;
    ok &= Check(SameNrGuideStorageContract(fp16, r11) &&
                SameNrGuideStorageContract(r11, fp16),
                "R11 and FP16 original color share Feature 18 storage");
    ok &= Check(!SameNrGuideMappingContract(fp16, r11),
                "original color change still resets history");
    NrGuideHistory history{};
    ok &= Check(history.NeedsReset(fp16), "first frame resets");
    history.CommitOnEvaluate(fp16, true);
    ok &= Check(history.NeedsReset(r11), "R11 switch resets");
    history.CommitOnEvaluate(r11, false);
    ok &= Check(history.NeedsReset(r11), "failed Reset does not commit");
    history.CommitOnEvaluate(r11, true);
    ok &= Check(!history.NeedsReset(r11), "successful Reset stabilizes");
    ok &= Check(history.NeedsReset(fp16), "FP16 return resets");
    for (unsigned format : {kR8, kR10Pq}) {
        auto other = fp16;
        other.color_format = format;
        ok &= Check(!SameNrGuideStorageContract(fp16, other),
                    "R8 or PQ original color remains distinct storage");
    }
    auto other = fp16;
    ++other.depth_format;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "depth format remains distinct");
    other = fp16;
    ++other.motion_format;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "motion format remains distinct");
    other = fp16;
    ++other.motion_bound_width;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "motion bound remains distinct");
    other = fp16;
    other.resample_motion = false;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "motion conversion remains distinct");
    other = fp16;
    ++other.normalized_create_flags;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "Create flags remain distinct");
    other = fp16;
    other.rr_depth_placeholder = true;
    ok &= Check(!SameNrGuideStorageContract(fp16, other), "depth placeholder remains distinct");
    return ok ? 0 : 1;
}
