#pragma once

// Output-only additions to the existing 60-DWORD codec ABI. RegionFlags bits
// 0..5 belong to region/generic controls. These bits never change model history.
namespace nr_reconstruction {
constexpr unsigned kModeMask = 3u << 8;
constexpr unsigned kChromaFlag = 1u << 10;
static_assert(((kModeMask | kChromaFlag) & 63u) == 0u);
constexpr int Normalize(int value) noexcept { return value == 1 || value == 2 ? value : 0; }
enum class Reason : int { Off=0, Recorded=1, NotCombine=2, UnknownEncoding=3,
    RawDiagnostic=4, EqualSize=5, LegacySampling=6, OtherEditField=7, NotMapped=8,
    ColourInactive=9, Preview=10 };
struct Selection {
    unsigned flags=0;
    int mode=0;
    bool chroma=false;
    Reason reconstruction=Reason::Off, protection=Reason::Off;
};
constexpr Selection Select(int request, bool guard, bool combine, bool encoder_known,
    bool raw, bool v2, bool scaled, bool old_edge, bool other_field,
    bool colour_active, bool preview) noexcept {
    Selection out{};
    request=Normalize(request);
    const Reason blocked=!combine?Reason::NotCombine:!encoder_known?Reason::UnknownEncoding:
        raw?Reason::RawDiagnostic:preview?Reason::Preview:Reason::Recorded;
    if(request) {
        out.reconstruction=blocked;
        if(blocked==Reason::Recorded) {
            out.reconstruction=!v2?Reason::NotMapped:!scaled?Reason::EqualSize:
                old_edge?Reason::LegacySampling:other_field?Reason::OtherEditField:Reason::Recorded;
            if(out.reconstruction==Reason::Recorded)out.mode=request;
        }
    }
    if(guard) {
        out.protection=blocked;
        if(blocked==Reason::Recorded) {
            out.protection=colour_active?Reason::Recorded:Reason::ColourInactive;
            out.chroma=colour_active;
        }
    }
    out.flags=(static_cast<unsigned>(out.mode)<<8)|(out.chroma?kChromaFlag:0u);
    return out;
}
constexpr const char* ReasonName(Reason r) noexcept {
    switch(r) {
    case Reason::Off:return "off";
    case Reason::Recorded:return "recorded-not-pixel-or-GPU-proof";
    case Reason::NotCombine:return "not-final-combine";
    case Reason::UnknownEncoding:return "missing-same-call-encoding";
    case Reason::RawDiagnostic:return "raw-or-neutral-diagnostic";
    case Reason::EqualSize:return "equal-size-original-sampling";
    case Reason::LegacySampling:return "legacy-edge-experiment-selected";
    case Reason::OtherEditField:return "other-low-grid-field-selected";
    case Reason::NotMapped:return "nonmapped-legacy-compositor";
    case Reason::ColourInactive:return "effective-colour-zero";
    case Reason::Preview:return "diagnostic-preview";
    }
    return "unknown";
}
constexpr const char* ModeName(int mode) noexcept {
    return mode==1?"source-guided-paired-4":mode==2?"source-guided-paired-16":"existing-sampling";
}
} // namespace nr_reconstruction
