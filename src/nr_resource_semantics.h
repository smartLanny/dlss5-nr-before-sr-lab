#pragma once
#include "interop/nr_format_access.h"

// One result drives admission, descriptor selection and the F8 explanation.
// Candidate elimination is based on a declared contract or a successful device
// query. Lack of a codec implementation must never manufacture uniqueness.
namespace nr_resource_semantics {
using Format = nr_format_access::Format;
enum class Role { ColorInput, Output, Exposure };
enum class Source { None, TypedResource, ObservedNativeView, UniqueContract };
enum class Contract { None, SceneFloat, LdrEncoded, HdrDeclared, ExposureScale };
enum class Verdict { Admit, WaitObserver, Ambiguous, Unsupported, Conflict,
                     Stale, MissingContract, CapabilityUnknown };
struct Capability {
    bool queried = false;
    std::int32_t result = 0;
    bool texture2d = false, load = false, store = false;
    bool known() const noexcept { return queried && result >= 0; }
};
struct Candidate {
    Format view = 0, write_view = 0;
    bool contract_possible = true;
    bool layout_legal = false;
    bool codec_implemented = false;
    Capability capability{};
};
struct Request {
    Role role = Role::ColorInput;
    Format storage = 0, observed_view = 0;
    Contract contract = Contract::None;
    bool contract_known = false;
    bool observation_conflict = false, observation_stale = false;
    bool lifecycle_known = false, shape_valid = true;
    bool deny_read = false, allow_write = false;
    bool exact_copy_snapshot = false;
};
struct Decision {
    nr_format_access::Plan access{};
    Source source = Source::None;
    Contract contract = Contract::None;
    Verdict verdict = Verdict::WaitObserver;
    std::array<Candidate, 6> candidates{};
    unsigned candidate_count = 0, remaining_count = 0;
    bool definitive = false;
    const char* reason = "native-typed-view-not-observed";
    const char* missing_evidence = "current-resource-native-SRV-or-UAV-format";
    const char* next_action = "auto-retry";
    explicit operator bool() const noexcept { return verdict == Verdict::Admit; }
};
inline bool CodecImplemented(Format view) noexcept {
    return nr_format_access::IsFloatColor(view) ||
           view == nr_format_access::Rgb10a2Unorm || view == nr_format_access::Rgba8Unorm;
}
inline Format WriteView(Format view) noexcept {
    if (view == nr_format_access::Rgba8Srgb) return nr_format_access::Rgba8Unorm;
    if (view == nr_format_access::Bgra8Srgb) return nr_format_access::Bgra8Unorm;
    if (view == nr_format_access::Bgrx8Srgb) return nr_format_access::Bgrx8Unorm;
    return view;
}
inline void Reject(Decision& d, Verdict verdict, const char* reason,
                   const char* missing, bool definitive = true) noexcept {
    d.verdict = verdict; d.reason = reason; d.missing_evidence = missing;
    d.definitive = definitive;
    d.next_action = definitive ? "needs-code" : "auto-retry";
}

class ResourceSemanticResolver {
public:
    template <class Probe> static Decision Resolve(const Request& r, Probe&& probe) {
        Decision d; d.contract = r.contract;
        const bool write = r.role == Role::Output;
        const bool copy_snapshot = r.exact_copy_snapshot && write &&
                                   r.storage == nr_format_access::Rgba16Float;
        const bool typeless = nr_format_access::IsTypeless(r.storage);
        auto family = nr_format_access::FamilyViews(r.storage);
        if (!typeless && r.storage) family = {{{r.storage}}, 1};
        d.candidate_count = family.count;
        for (unsigned i = 0; i != family.count; ++i) {
            auto& c = d.candidates[i];
            c.view = family.values[i]; c.write_view = write ? WriteView(c.view) : 0;
            c.layout_legal = nr_format_access::LegalView(r.storage, c.view) &&
                (!write || nr_format_access::LegalView(r.storage, c.write_view));
            c.codec_implemented = CodecImplemented(c.view);
            // This is the existing explicit scene-FLOAT adapter contract,
            // not a new assumption that every HDR/linear image must be FLOAT.
            c.contract_possible = r.contract != Contract::SceneFloat ||
                                  nr_format_access::IsFloatColor(c.view);
        }
        if (r.observation_conflict) {
            Reject(d, Verdict::Conflict, "conflicting-native-typed-views",
                   "one-consistent-native-view-for-this-resource-lifetime");
            return d;
        }
        if (r.observation_stale) {
            Reject(d, Verdict::Stale, "native-view-lifetime-or-evaluate-invalid",
                   "successful-native-view-in-current-resource-and-feature-generation", false);
            return d;
        }
        if (!r.shape_valid) {
            Reject(d, Verdict::Unsupported, "unsupported-resource-shape", "none");
            return d;
        }
        if (r.deny_read && !copy_snapshot) {
            d.access.status = nr_format_access::Status::DeniedRead;
            Reject(d, Verdict::Unsupported, "resource-denies-shader-read", "none");
            return d;
        }
        if (write && !r.allow_write) {
            d.access.status = nr_format_access::Status::WritebackUnavailable;
            Reject(d, Verdict::Unsupported, "no-legal-output-writeback-route", "none");
            return d;
        }
        Format selected = 0;
        if (r.observed_view) {
            if (!r.lifecycle_known || !nr_format_access::LegalView(r.storage, r.observed_view)) {
                Reject(d, Verdict::Stale, "observed-view-does-not-match-live-resource",
                       "matching-device-resource-feature-and-subresource", false);
                return d;
            }
            selected = r.observed_view; d.source = Source::ObservedNativeView;
        } else if (!typeless && r.storage) {
            selected = r.storage; d.source = Source::TypedResource;
        } else {
            if (!r.lifecycle_known) {
                Reject(d, Verdict::Stale, "resource-lifecycle-identity-unavailable",
                       "current-device-resource-and-feature-generation", false);
                return d;
            }
            if (!family.count) {
                Reject(d, Verdict::Unsupported, "unknown-format-family", "format-layout-adapter");
                return d;
            }
            bool capability_unknown = false;
            for (unsigned i = 0; i != family.count; ++i) {
                auto& c = d.candidates[i];
                if (!c.contract_possible || !c.layout_legal) continue;
                c.capability = probe(c.view, c.write_view);
                if (!c.capability.known()) {
                    capability_unknown = true;
                    ++d.remaining_count; // Failed/unattempted queries do not eliminate a view.
                    continue;
                }
                if (!c.capability.texture2d || (!copy_snapshot && !c.capability.load) ||
                    (write && !c.capability.store)) continue;
                ++d.remaining_count; selected = c.view;
            }
            if (capability_unknown) {
                Reject(d, Verdict::CapabilityUnknown, "format-capability-query-incomplete",
                       "successful-device-query-for-every-remaining-interpretation", false);
                return d;
            }
            if (!d.remaining_count) {
                Reject(d, Verdict::Unsupported, "no-device-supported-contract-interpretation", "none");
                return d;
            }
            if (!r.contract_known || r.contract == Contract::None) {
                Reject(d, Verdict::MissingContract, "resource-color-contract-not-declared",
                       "DLSS-create-flags-and-consistent-color-exposure-contract", false);
                return d;
            }
            if (d.remaining_count != 1) {
                Reject(d, Verdict::Ambiguous, "multiple-legal-resource-interpretations",
                       write ? "current-native-Output-UAV-format" : "current-native-Color-SRV-format", false);
                return d;
            }
            d.source = Source::UniqueContract;
        }
        Candidate* chosen = nullptr;
        for (unsigned i = 0; i != d.candidate_count; ++i)
            if (d.candidates[i].view == selected) chosen = &d.candidates[i];
        if (!chosen || !chosen->layout_legal) {
            d.access.status = nr_format_access::Status::IllegalView;
            Reject(d, Verdict::Unsupported, "illegal-resource-view-pair", "none");
            return d;
        }
        d.access.srv = selected; d.access.uav = write ? chosen->write_view : 0;
        d.access.provenance = d.source == Source::TypedResource ? nr_format_access::Provenance::ExactTyped :
            d.source == Source::ObservedNativeView ? nr_format_access::Provenance::ObservedView :
                                                   nr_format_access::Provenance::FloatInterpretation;
        if (!chosen->codec_implemented) {
            d.access.status = nr_format_access::IsInteger(selected)
                ? nr_format_access::Status::IntegerShaderRequired : nr_format_access::Status::UnknownLayout;
            Reject(d, Verdict::Unsupported, "selected-interpretation-needs-codec-adapter", "none");
            return d;
        }
        if (!chosen->capability.queried) chosen->capability = probe(selected, chosen->write_view);
        if (!chosen->capability.known()) {
            Reject(d, Verdict::CapabilityUnknown, "selected-view-device-query-incomplete",
                   "successful-selected-view-device-query", false);
            return d;
        }
        if (!chosen->capability.texture2d || (!copy_snapshot && !chosen->capability.load) ||
            (write && !chosen->capability.store)) {
            d.access.status = write ? nr_format_access::Status::WritebackUnavailable :
                                      nr_format_access::Status::UnsupportedRead;
            Reject(d, Verdict::Unsupported, "selected-view-device-capability-unavailable", "none");
            return d;
        }
        d.access.status = nr_format_access::Status::Ready;
        d.verdict = Verdict::Admit;
        d.definitive = true;
        d.reason = "resource-view-resolved";
        d.missing_evidence = "none";
        d.next_action = "none";
        return d;
    }
};
inline const char* SourceName(Source v) noexcept {
    switch(v) {
    case Source::TypedResource: return "typed-resource";
    case Source::ObservedNativeView: return "observed-native-view";
    case Source::UniqueContract: return "unique-contract";
    default: return "none";
    }
}
inline const char* VerdictName(Verdict v) noexcept {
    switch(v) {
    case Verdict::Admit:return "admit";
    case Verdict::WaitObserver:return "wait-observer";
    case Verdict::Ambiguous:return "ambiguous";
    case Verdict::Unsupported:return "unsupported";
    case Verdict::Conflict:return "conflict";
    case Verdict::Stale:return "stale";
    case Verdict::MissingContract:return "missing-contract";
    case Verdict::CapabilityUnknown:return "capability-unknown";
    }
    return "unknown";
}
inline const char* ContractName(Contract v) noexcept {
    switch(v) {
    case Contract::SceneFloat:return "scene-float-contract";
    case Contract::LdrEncoded:return "declared-LDR-encoded";
    case Contract::HdrDeclared:return "declared-HDR";
    case Contract::ExposureScale:return "final-exposure-scale-red";
    default:return "unknown";
    }
}
} // namespace nr_resource_semantics
